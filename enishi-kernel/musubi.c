#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/minmax.h>

#include "kenfs.h"
#include "musubi.h"

/* ========================================================================
 * path strings
 * ======================================================================== */

/**
 * Step to the next component of a '/' separated path.
 * Empty components (leading, trailing and repeated '/') are skipped.
 */
static bool enfs_path_next(const char** cursor, const char* end, const char** name, size_t* len) {
    const char* at = *cursor;

    while (at < end && *at == '/') {
        at++;
    }

    *name = at;

    while (at < end && *at != '/') {
        at++;
    }

    *len = at - *name;
    *cursor = at;

    return *len != 0;
}

static bool enfs_path_is_dots(const char* name, size_t len) {
    return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
}

/**
 * Check a path that is about to be stored in the tree.
 */
static int enfs_path_check(const char* path) {
    const char* end;
    const char* name;
    size_t len;

    if (path == NULL) {
        return -EINVAL;
    }

    end = path + strlen(path);

    while (enfs_path_next(&path, end, &name, &len)) {
        if (enfs_path_is_dots(name, len)) {
            return -EINVAL;
        }

        if (len > NAME_MAX) {
            return -ENAMETOOLONG;
        }
    }

    return 0;
}

static int enfs_name_compare(const char* a, size_t a_len, const char* b, size_t b_len) {
    int diff = memcmp(a, b, min(a_len, b_len));

    if (diff != 0) {
        return diff;
    }

    if (a_len == b_len) {
        return 0;
    }

    return a_len < b_len ? -1 : 1;
}

/* ========================================================================
 * tree nodes
 *
 * Everything in here expects table->lock to be held by the caller.
 * ======================================================================== */

static struct enfs_musubi_node* enfs_musubi_node_new(const char* name, size_t len) {
    struct enfs_musubi_node* node = kzalloc(sizeof(struct enfs_musubi_node), GFP_KERNEL);

    if (node == NULL) {
        return NULL;
    }

    node->name = kmemdup_nul(name, len, GFP_KERNEL);

    if (node->name == NULL) {
        kfree(node);
        return NULL;
    }

    node->name_len = len;
    node->children = RB_ROOT;

    return node;
}

static struct enfs_musubi_node* enfs_musubi_node_child(
    struct enfs_musubi_node* parent,
    const char* name,
    size_t len
) {
    struct rb_node* at = parent->children.rb_node;

    while (at != NULL) {
        struct enfs_musubi_node* node = rb_entry(at, struct enfs_musubi_node, sibling);
        int diff = enfs_name_compare(name, len, node->name, node->name_len);

        if (diff < 0) {
            at = at->rb_left;
        } else if (diff > 0) {
            at = at->rb_right;
        } else {
            return node;
        }
    }

    return NULL;
}

/**
 * Put node under parent. The caller made sure the name is not taken.
 */
static void enfs_musubi_node_link(struct enfs_musubi_node* parent, struct enfs_musubi_node* node) {
    struct rb_node** link = &parent->children.rb_node;
    struct rb_node* above = NULL;

    while (*link != NULL) {
        struct enfs_musubi_node* other = rb_entry(*link, struct enfs_musubi_node, sibling);

        above = *link;

        if (enfs_name_compare(node->name, node->name_len, other->name, other->name_len) < 0) {
            link = &above->rb_left;
        } else {
            link = &above->rb_right;
        }
    }

    rb_link_node(&node->sibling, above, link);
    rb_insert_color(&node->sibling, &parent->children);
    node->parent = parent;
}

/**
 * Free node and its ancestors for as long as they carry nothing:
 * no musubi and no children. The root always stays.
 */
static void enfs_musubi_node_prune(struct enfs_musubi_node* node) {
    while (node->parent != NULL && node->musubi == NULL && RB_EMPTY_ROOT(&node->children)) {
        struct enfs_musubi_node* parent = node->parent;

        rb_erase(&node->sibling, &parent->children);
        kfree(node->name);
        kfree(node);

        node = parent;
    }
}

/**
 * Get the node at path, NULL if the tree does not reach that far.
 */
static struct enfs_musubi_node* enfs_musubi_node_find(
    struct enfs_musubi_table* table,
    const char* path,
    const char* end
) {
    struct enfs_musubi_node* node = &table->root;
    const char* name;
    size_t len;

    while (node != NULL && enfs_path_next(&path, end, &name, &len)) {
        node = enfs_musubi_node_child(node, name, len);
    }

    return node;
}

/**
 * Get the node at path, creating whatever is missing on the way.
 * On failure the tree is left as it was.
 */
static struct enfs_musubi_node* enfs_musubi_node_make(
    struct enfs_musubi_table* table,
    const char* path,
    const char* end
) {
    struct enfs_musubi_node* node = &table->root;
    const char* name;
    size_t len;

    while (enfs_path_next(&path, end, &name, &len)) {
        struct enfs_musubi_node* child = enfs_musubi_node_child(node, name, len);

        if (child == NULL) {
            child = enfs_musubi_node_new(name, len);

            if (child == NULL) {
                enfs_musubi_node_prune(node);
                return ERR_PTR(-ENOMEM);
            }

            enfs_musubi_node_link(node, child);
        }

        node = child;
    }

    return node;
}

/**
 * Write the path of node at the end of buffer and return where it starts.
 * No leading '/', the root is "".
 */
static char* enfs_musubi_node_path(const struct enfs_musubi_node* node, char* buffer, size_t size) {
    char* at = buffer + size - 1;
    bool first = true;

    *at = '\0';

    for (; node->parent != NULL; node = node->parent) {
        size_t need = node->name_len + (first ? 0 : 1);

        if ((size_t) (at - buffer) < need) {
            return ERR_PTR(-ENAMETOOLONG);
        }

        if (!first) {
            *--at = '/';
        }

        at -= node->name_len;
        memcpy(at, node->name, node->name_len);
        first = false;
    }

    return at;
}

/**
 * Length of what enfs_musubi_node_path writes, without the terminator.
 */
static size_t enfs_musubi_node_path_len(const struct enfs_musubi_node* node) {
    size_t len = 0;

    for (; node->parent != NULL; node = node->parent) {
        len += node->name_len + (len == 0 ? 0 : 1);
    }

    return len;
}

/**
 * Is there a saved musubi at top or anywhere below it?
 * Not recursive: the tree can be as deep as a path is long.
 */
static bool enfs_musubi_node_has_saved(struct enfs_musubi_node* top) {
    struct enfs_musubi_node* node = top;

    for (;;) {
        struct rb_node* next;

        if (node->musubi != NULL && !node->musubi->unsaved) {
            return true;
        }

        /* Down to the first child. Without one, on to the next sibling, of node or of the closest ancestor that has one. */
        next = rb_first(&node->children);

        while (next == NULL && node != top) {
            next = rb_next(&node->sibling);
            node = node->parent;
        }

        if (next == NULL) {
            return false;
        }

        node = rb_entry(next, struct enfs_musubi_node, sibling);
    }
}

/* ========================================================================
 * musubi entries
 * ======================================================================== */

/**
 * Copy a target for storing: trailing '/' dropped, but "/" stays "/".
 */
static char* enfs_musubi_target_dup(const char* to, size_t* len) {
    char* dup;
    size_t n;

    if (to == NULL) {
        return ERR_PTR(-EINVAL);
    }

    n = strlen(to);

    while (n > 1 && to[n - 1] == '/') {
        n--;
    }

    if (n == 0) {
        return ERR_PTR(-EINVAL);
    }

    dup = kmemdup_nul(to, n, GFP_KERNEL);

    if (dup == NULL) {
        return ERR_PTR(-ENOMEM);
    }

    *len = n;
    return dup;
}

/**
 * Build "to/rest", with rest cleaned of empty components.
 */
static char* enfs_musubi_target_join(const struct enfs_musubi* musubi, const char* rest) {
    const char* end = rest + strlen(rest);
    const char* name;
    size_t len;
    size_t at = musubi->to_len;
    /* Every component of rest costs its length plus one '/', and rest already has all but the first '/'. */
    char* target = kmalloc(musubi->to_len + (end - rest) + 2, GFP_KERNEL);

    if (target == NULL) {
        return NULL;
    }

    memcpy(target, musubi->to, musubi->to_len);

    while (enfs_path_next(&rest, end, &name, &len)) {
        if (target[at - 1] != '/') {
            target[at++] = '/';
        }

        memcpy(target + at, name, len);
        at += len;
    }

    target[at] = '\0';
    return target;
}

/**
 * Detach a musubi from the tree and free it. Lock held for write.
 */
static void enfs_musubi_drop(struct enfs_musubi_table* table, struct enfs_musubi* musubi) {
    struct enfs_musubi_node* node = musubi->node;

    node->musubi = NULL;
    list_del(&musubi->link);
    table->count--;

    if (musubi->unsaved) {
        table->delta_count--; /* userspace never heard of it, nothing to tell */
    } else {
        table->delta_invalid = true;
    }

    kfree(musubi->to);
    kfree(musubi);

    enfs_musubi_node_prune(node);
}

/**
 * Move every musubi of the delta to main: userspace is in sync again.
 * Lock held for write.
 */
static void enfs_musubi_save_all(struct enfs_musubi_table* table) {
    struct enfs_musubi* musubi;

    list_for_each_entry(musubi, &table->delta, link) {
        musubi->unsaved = false;
    }

    list_splice_tail_init(&table->delta, &table->main);
    table->delta_count = 0;
    table->delta_invalid = false;
}

/**
 * Call fn for every musubi of one list, buffer is PATH_MAX of scratch space.
 * Returns 0 after getting through the whole list and 1 when fn stopped it.
 * Lock held.
 */
static int enfs_musubi_walk(struct list_head* list, char* buffer, enfs_musubi_entry_fn fn, void* arg) {
    struct enfs_musubi* musubi;

    list_for_each_entry(musubi, list, link) {
        char* from = enfs_musubi_node_path(musubi->node, buffer, PATH_MAX);

        if (IS_ERR(from)) {
            return PTR_ERR(from);
        }

        if (!fn(from, musubi->to, musubi->flags, arg)) {
            return 1;
        }
    }

    return 0;
}

/* ========================================================================
 * lifecycle
 * ======================================================================== */

void enfs_musubi_table_init(struct enfs_musubi_table* table) {
    init_rwsem(&table->lock);
    INIT_LIST_HEAD(&table->main);
    INIT_LIST_HEAD(&table->delta);

    /* The root has no name and no parent, it is never linked, compared or freed. */
    memset(&table->root, 0, sizeof(struct enfs_musubi_node));
    table->root.children = RB_ROOT;

    table->count = 0;
    table->delta_count = 0;
    table->delta_invalid = false;
    table->generation = 0;
}

void enfs_musubi_table_destroy(struct enfs_musubi_table* table) {
    enfs_musubi_clear(table);
}

/* ========================================================================
 * modify
 * ======================================================================== */

/**
 * The body of enfs_musubi_add and enfs_musubi_load.
 */
static int enfs_musubi_insert(
    struct enfs_musubi_table* table,
    const char* from,
    const char* to,
    uint32_t flags,
    bool saved
) {
    struct enfs_musubi* musubi;
    struct enfs_musubi_node* node;
    int err;

    err = enfs_path_check(from);

    if (err) {
        return err;
    }

    musubi = kzalloc(sizeof(struct enfs_musubi), GFP_KERNEL);

    if (musubi == NULL) {
        return -ENOMEM;
    }

    musubi->to = enfs_musubi_target_dup(to, &musubi->to_len);

    if (IS_ERR(musubi->to)) {
        err = PTR_ERR(musubi->to);
        kfree(musubi);
        return err;
    }

    musubi->flags = flags;

    down_write(&table->lock);

    node = enfs_musubi_node_make(table, from, from + strlen(from));

    if (IS_ERR(node)) {
        err = PTR_ERR(node);
    } else if (node->musubi != NULL) {
        err = -EEXIST;
    } else {
        musubi->node = node;
        musubi->unsaved = !saved;
        node->musubi = musubi;

        if (saved) {
            list_add_tail(&musubi->link, &table->main);
        } else {
            list_add_tail(&musubi->link, &table->delta);
            table->delta_count++;
        }

        table->count++;
        table->generation++;
    }

    up_write(&table->lock);

    if (err) {
        kfree(musubi->to);
        kfree(musubi);
    }

    return err;
}

int enfs_musubi_add(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags) {
    return enfs_musubi_insert(table, from, to, flags, false);
}

int enfs_musubi_load(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags) {
    return enfs_musubi_insert(table, from, to, flags, true);
}

int enfs_musubi_remove(struct enfs_musubi_table* table, const char* from) {
    struct enfs_musubi_node* node;
    int err;

    err = enfs_path_check(from);

    if (err) {
        return err;
    }

    down_write(&table->lock);

    node = enfs_musubi_node_find(table, from, from + strlen(from));

    if (node == NULL || node->musubi == NULL) {
        err = -ENOENT;
    } else {
        enfs_musubi_drop(table, node->musubi);
        table->generation++;
    }

    up_write(&table->lock);

    return err;
}

int enfs_musubi_update(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags) {
    struct enfs_musubi_node* node;
    char* new_to = NULL;
    size_t new_len = 0;
    int err;

    err = enfs_path_check(from);

    if (err) {
        return err;
    }

    if (to != NULL) {
        new_to = enfs_musubi_target_dup(to, &new_len);

        if (IS_ERR(new_to)) {
            return PTR_ERR(new_to);
        }
    }

    down_write(&table->lock);

    node = enfs_musubi_node_find(table, from, from + strlen(from));

    if (node == NULL || node->musubi == NULL) {
        err = -ENOENT;
    } else {
        if (new_to != NULL) {
            swap(node->musubi->to, new_to); /* new_to is now the old target, freed below */
            node->musubi->to_len = new_len;
        }

        node->musubi->flags = flags;

        /* One that is still in the delta simply gets handed out with its new values */
        if (!node->musubi->unsaved) {
            table->delta_invalid = true;
        }

        table->generation++;
    }

    up_write(&table->lock);

    kfree(new_to);

    return err;
}

/**
 * The body of enfs_musubi_move. Lock held for write.
 */
static int enfs_musubi_move_locked(struct enfs_musubi_table* table, const char* from, const char* new_from) {
    struct enfs_musubi_node* node;
    struct enfs_musubi_node* old_parent;
    struct enfs_musubi_node* new_parent;
    struct enfs_musubi_node* above;
    const char* cursor = new_from;
    const char* end = new_from + strlen(new_from);
    const char* name;
    const char* last = NULL;
    size_t len;
    size_t last_len = 0;
    char* new_name;

    node = enfs_musubi_node_find(table, from, from + strlen(from));

    if (node == NULL) {
        return -ENOENT;
    }

    if (node->parent == NULL) {
        return -EINVAL;
    }

    /* Split new_from in "directory" + "last component" */
    while (enfs_path_next(&cursor, end, &name, &len)) {
        last = name;
        last_len = len;
    }

    if (last == NULL) {
        return -EEXIST; /* new_from is the root */
    }

    new_parent = enfs_musubi_node_make(table, new_from, last);

    if (IS_ERR(new_parent)) {
        return PTR_ERR(new_parent);
    }

    for (above = new_parent; above != NULL; above = above->parent) {
        if (above == node) {
            enfs_musubi_node_prune(new_parent);
            return -EINVAL;
        }
    }

    above = enfs_musubi_node_child(new_parent, last, last_len);

    if (above != NULL) {
        return above == node ? 0 : -EEXIST;
    }

    new_name = kmemdup_nul(last, last_len, GFP_KERNEL);

    if (new_name == NULL) {
        enfs_musubi_node_prune(new_parent);
        return -ENOMEM;
    }

    old_parent = node->parent;
    rb_erase(&node->sibling, &old_parent->children);

    kfree(node->name);
    node->name = new_name;
    node->name_len = last_len;

    enfs_musubi_node_link(new_parent, node);
    enfs_musubi_node_prune(old_parent);

    /* Unsaved musubi just get handed out with their new `from`, saved ones are now wrong on disk */
    if (enfs_musubi_node_has_saved(node)) {
        table->delta_invalid = true;
    }

    table->generation++;

    return 0;
}

int enfs_musubi_move(struct enfs_musubi_table* table, const char* from, const char* new_from) {
    int err;

    err = enfs_path_check(from);

    if (err) {
        return err;
    }

    err = enfs_path_check(new_from);

    if (err) {
        return err;
    }

    down_write(&table->lock);
    err = enfs_musubi_move_locked(table, from, new_from);
    up_write(&table->lock);

    return err;
}

void enfs_musubi_clear(struct enfs_musubi_table* table) {
    struct enfs_musubi* musubi;
    struct enfs_musubi* next;

    down_write(&table->lock);

    /* Nodes only exist because of the musubi below them, so this empties the tree as well. */
    list_for_each_entry_safe(musubi, next, &table->main, link) {
        enfs_musubi_drop(table, musubi);
    }

    list_for_each_entry_safe(musubi, next, &table->delta, link) {
        enfs_musubi_drop(table, musubi);
    }

    table->generation++;

    up_write(&table->lock);
}

/* ========================================================================
 * query
 * ======================================================================== */

size_t enfs_musubi_count(struct enfs_musubi_table* table) {
    size_t count;

    down_read(&table->lock);
    count = table->count;
    up_read(&table->lock);

    return count;
}

int enfs_musubi_get(struct enfs_musubi_table* table, const char* from, char** to, uint32_t* flags) {
    struct enfs_musubi_node* node;
    int err;

    err = enfs_path_check(from);

    if (err) {
        return err;
    }

    down_read(&table->lock);

    node = enfs_musubi_node_find(table, from, from + strlen(from));

    if (node == NULL || node->musubi == NULL) {
        err = -ENOENT;
    } else {
        if (flags != NULL) {
            *flags = node->musubi->flags;
        }

        if (to != NULL) {
            *to = kmemdup_nul(node->musubi->to, node->musubi->to_len, GFP_KERNEL);

            if (*to == NULL) {
                err = -ENOMEM;
            }
        }
    }

    up_read(&table->lock);

    return err;
}

int enfs_musubi_children(
    struct enfs_musubi_table* table,
    const char* dir,
    size_t skip,
    enfs_musubi_child_fn fn,
    void* arg
) {
    struct enfs_musubi_node* node;
    struct rb_node* at;
    int err;

    err = enfs_path_check(dir);

    if (err) {
        return err;
    }

    down_read(&table->lock);

    node = enfs_musubi_node_find(table, dir, dir + strlen(dir));

    if (node == NULL) {
        err = -ENOENT;
    } else {
        for (at = rb_first(&node->children); at != NULL; at = rb_next(at)) {
            struct enfs_musubi_node* child = rb_entry(at, struct enfs_musubi_node, sibling);

            if (skip > 0) {
                skip--;
                continue;
            }

            if (!fn(child->name, child->name_len, child->musubi != NULL, arg)) {
                break;
            }
        }
    }

    up_read(&table->lock);

    return err;
}

int enfs_musubi_for_each(struct enfs_musubi_table* table, enfs_musubi_entry_fn fn, void* arg) {
    char* buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    int err;

    if (buffer == NULL) {
        return -ENOMEM;
    }

    down_read(&table->lock);

    err = enfs_musubi_walk(&table->main, buffer, fn, arg);

    if (err == 0) {
        err = enfs_musubi_walk(&table->delta, buffer, fn, arg);
    }

    up_read(&table->lock);

    kfree(buffer);

    return err < 0 ? err : 0; /* being stopped by fn is not an error */
}

static bool enfs_musubi_dump_one(const char* from, const char* to, uint32_t flags, void* arg) {
    pr_info("enishifs: musubi: /%s -> %s (flags %#x)%s\n", from, to, flags, (const char*) arg);
    return true;
}

void enfs_musubi_dump(struct enfs_musubi_table* table) {
    char* buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    int err;

    if (buffer == NULL) {
        pr_warn("enishifs: musubi: no memory to dump the table\n");
        return;
    }

    down_read(&table->lock);

    pr_info(
        "enishifs: musubi: %zu in table, %zu not saved%s\n",
        table->count,
        table->delta_count,
        table->delta_invalid ? ", delta invalid" : ""
    );

    err = enfs_musubi_walk(&table->main, buffer, enfs_musubi_dump_one, "");

    if (err == 0) {
        err = enfs_musubi_walk(&table->delta, buffer, enfs_musubi_dump_one, " (not saved)");
    }

    up_read(&table->lock);

    kfree(buffer);

    if (err) {
        pr_warn("enishifs: musubi: dump is incomplete\n");
    }
}

/* ========================================================================
 * delta
 * ======================================================================== */

size_t enfs_musubi_delta_count(struct enfs_musubi_table* table, bool* invalid) {
    size_t count;

    down_read(&table->lock);

    count = table->delta_count;

    if (invalid != NULL) {
        *invalid = table->delta_invalid;
    }

    up_read(&table->lock);

    return count;
}

int enfs_musubi_delta_next(struct enfs_musubi_table* table, char** from, char** to, uint32_t* flags) {
    struct enfs_musubi* musubi;
    char* buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    char* path;
    char* new_from = NULL;
    char* new_to = NULL;
    int err = 0;

    if (buffer == NULL) {
        return -ENOMEM;
    }

    down_write(&table->lock);

    if (table->delta_invalid) {
        err = -ESTALE;
    } else if (list_empty(&table->delta)) {
        err = -ENOENT;
    } else {
        musubi = list_first_entry(&table->delta, struct enfs_musubi, link);
        path = enfs_musubi_node_path(musubi->node, buffer, PATH_MAX);

        if (IS_ERR(path)) {
            err = PTR_ERR(path);
        } else {
            new_from = kmemdup_nul(path, strlen(path), GFP_KERNEL);
            new_to = kmemdup_nul(musubi->to, musubi->to_len, GFP_KERNEL);

            if (new_from == NULL || new_to == NULL) {
                err = -ENOMEM;
            } else {
                *flags = musubi->flags;

                /* Only now that nothing can fail anymore: it is saved */
                musubi->unsaved = false;
                list_move_tail(&musubi->link, &table->main);
                table->delta_count--;
            }
        }
    }

    up_write(&table->lock);

    kfree(buffer);

    if (err) {
        kfree(new_from);
        kfree(new_to);
        return err;
    }

    *from = new_from;
    *to = new_to;

    return 0;
}

void enfs_musubi_delta_clear(struct enfs_musubi_table* table) {
    down_write(&table->lock);
    enfs_musubi_save_all(table);
    up_write(&table->lock);
}

/**
 * Bytes needed for the `from` and `to` strings of a list, terminators included. Lock held.
 */
static size_t enfs_musubi_strings_size(struct list_head* list) {
    struct enfs_musubi* musubi;
    size_t bytes = 0;

    list_for_each_entry(musubi, list, link) {
        bytes += enfs_musubi_node_path_len(musubi->node) + 1 + musubi->to_len + 1;
    }

    return bytes;
}

void enfs_musubi_export_size(struct enfs_musubi_table* table, size_t* count, size_t* bytes) {
    down_read(&table->lock);

    *count = table->count;
    *bytes = enfs_musubi_strings_size(&table->main) + enfs_musubi_strings_size(&table->delta);

    up_read(&table->lock);
}

int enfs_musubi_export(struct enfs_musubi_table* table, enfs_musubi_entry_fn fn, void* arg) {
    char* buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    int err;

    if (buffer == NULL) {
        return -ENOMEM;
    }

    /* For write: the walk and marking everything saved have to be one step */
    down_write(&table->lock);

    err = enfs_musubi_walk(&table->main, buffer, fn, arg);

    if (err == 0) {
        err = enfs_musubi_walk(&table->delta, buffer, fn, arg);
    }

    if (err == 0) {
        enfs_musubi_save_all(table);
    }

    up_write(&table->lock);

    kfree(buffer);

    return err > 0 ? -ECANCELED : err;
}

/* ========================================================================
 * resolve
 * ======================================================================== */

int enfs_musubi_match(struct enfs_musubi_table* table, const char* path, struct enfs_musubi_match* match) {
    struct enfs_musubi_node* node = &table->root;
    struct enfs_musubi* musubi = table->root.musubi;
    const char* end;
    const char* rest = path;
    const char* name;
    size_t len;

    lockdep_assert_held(&table->lock);

    if (path == NULL) {
        return -EINVAL;
    }

    end = path + strlen(path);

    /*
     * Follow the path down the tree and remember the last musubi passed.
     * Once the path leaves the tree (node == NULL) nothing deeper can match,
     * the remaining components only get checked.
     */
    while (enfs_path_next(&path, end, &name, &len)) {
        if (enfs_path_is_dots(name, len)) {
            return -EINVAL;
        }

        if (node != NULL) {
            node = enfs_musubi_node_child(node, name, len);
        }

        if (node != NULL && node->musubi != NULL) {
            musubi = node->musubi;
            rest = path;
        }
    }

    match->musubi = musubi;
    match->node = node;

    if (musubi != NULL) {
        match->kind = musubi->node == node ? ENFS_RESOLVE_EXACT : ENFS_RESOLVE_INSIDE;
        match->rest = rest;
    } else {
        match->kind = node != NULL ? ENFS_RESOLVE_VIRTUAL : ENFS_RESOLVE_MISS;
        match->rest = end;
    }

    return 0;
}

char* enfs_musubi_resolve_str(struct enfs_musubi_table* table, const char* path, enum enfs_resolve_kind* kind) {
    struct enfs_musubi_match match;
    char* target = NULL;
    int err;

    down_read(&table->lock);

    err = enfs_musubi_match(table, path, &match);

    if (!err && match.musubi != NULL) {
        target = enfs_musubi_target_join(match.musubi, match.rest);

        if (target == NULL) {
            err = -ENOMEM;
        }
    }

    up_read(&table->lock);

    if (err) {
        return ERR_PTR(err);
    }

    if (kind != NULL) {
        *kind = match.kind;
    }

    return target;
}

char* enfs_musubi_resolve_dentry(struct dentry* dentry, enum enfs_resolve_kind* kind) {
    struct enfs_musubi_table* table = &enfs_super_of(dentry->d_sb)->musubi;
    char* buffer = kmalloc(PATH_MAX, GFP_KERNEL);
    char* path;
    char* target;

    if (buffer == NULL) {
        return ERR_PTR(-ENOMEM);
    }

    /* Path from the root of this superblock, so it does not matter where enishifs is mounted */
    path = dentry_path_raw(dentry, buffer, PATH_MAX);

    if (IS_ERR(path)) {
        target = ERR_CAST(path);
    } else {
        target = enfs_musubi_resolve_str(table, path, kind);
    }

    kfree(buffer);

    return target;
}

int enfs_musubi_lookup_str(
    struct enfs_musubi_table* table,
    const char* path,
    unsigned int lookup_flags,
    struct path* out,
    enum enfs_resolve_kind* kind
) {
    char* target = enfs_musubi_resolve_str(table, path, kind);
    int err;

    if (IS_ERR(target)) {
        return PTR_ERR(target);
    }

    if (target == NULL) {
        return -ENOENT;
    }

    err = kern_path(target, lookup_flags, out);
    kfree(target);

    return err;
}

int enfs_musubi_lookup_dentry(
    struct dentry* dentry,
    unsigned int lookup_flags,
    struct path* out,
    enum enfs_resolve_kind* kind
) {
    char* target = enfs_musubi_resolve_dentry(dentry, kind);
    int err;

    if (IS_ERR(target)) {
        return PTR_ERR(target);
    }

    if (target == NULL) {
        return -ENOENT;
    }

    err = kern_path(target, lookup_flags, out);
    kfree(target);

    if (err) {
        return err;
    }

    /* A target inside ourselves would send every operation on it straight back here */
    if (out->dentry->d_sb == dentry->d_sb) {
        path_put(out);
        return -ELOOP;
    }

    return 0;
}
