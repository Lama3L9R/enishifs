#ifndef ENFS_KO_MUSUBI_H
#define ENFS_KO_MUSUBI_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>

struct dentry;
struct path;

/*
 * 結び (musubi): a path alias, "from -> to".
 *
 *   from   a path inside enishifs, relative to its root. 
 *   to     where it really is. Kept as given (a leading '/' is meaningful),
 *          only trailing '/' are dropped.
 *
 * A musubi covers everything below it:
 *
 *   some/dir -> other/dir
 *   some/dir/file.txt  resolves to  other/dir/file.txt
 *
 * When musubi are nested the deepest one wins:
 *
 *   a     -> X
 *   a/b/c -> Y
 *   a/b/z    resolves to  X/b/z
 *   a/b/c/d  resolves to  Y/d
 *
 * Saving is done by userspace, the kernel only tells it what changed.
 * Every musubi is in one of two lists:
 *
 *   main    userspace has it on disk (it came from there, or was fetched)
 *   delta   made at runtime, userspace does not know about it yet
 *
 * Both resolve exactly the same. Userspace drains the delta one musubi at a
 * time, each one moves to main as it is handed out. A delta can only say
 * "this was added": once a musubi of main is removed, changed or moved the
 * delta is invalid, and userspace has to export the whole table instead.
 */

/**
 * One path component in the musubi tree.
 *
 * The tree mirrors the directory layout of every `from`:
 * "some/dir -> other/dir" becomes  root -> "some" -> "dir" (musubi).
 * A node without a musubi exists only because something below it has one,
 * it is removed as soon as that is no longer true.
 */
struct enfs_musubi_node {
    struct rb_node sibling;  /* in parent->children, ordered by name */
    struct rb_root children;
    struct enfs_musubi_node* parent; /* NULL for the root */

    struct enfs_musubi* musubi; /* Nullable */

    char* name; /* a single component, never contains '/' */
    size_t name_len;
};

/**
 * A musubi entry. `from` is not stored, it is the position of `node` in the tree.
 */
struct enfs_musubi {
    struct list_head link; /* enfs_musubi_table.main or .delta */

    struct enfs_musubi_node* node;

    char* to;
    size_t to_len;
    uint32_t flags; /* ENFS_MUSUBI_* */

    bool unsaved; /* true while in enfs_musubi_table.delta */
};

struct enfs_musubi_table {
    /* Protects everything below. All functions take it by themselves unless stated otherwise. */
    struct rw_semaphore lock;

    struct enfs_musubi_node root;

    struct list_head main;  /* saved, in the order they got saved */
    struct list_head delta; /* not saved yet, oldest first */
    size_t count;           /* main + delta */
    size_t delta_count;

    /* Main changed in a way the delta list can not express, only a full export gets userspace back in sync. */
    bool delta_invalid;

    /* Bumped on every change, lets a cached lookup result notice it went stale. */
    u64 generation;
};

/**
 * How a path relates to the musubi table.
 */
enum enfs_resolve_kind {
    /* Not below any musubi, and nothing in the table is below it either. */
    ENFS_RESOLVE_MISS,

    /* Not below any musubi, but a prefix of one: exists only as a directory of the tree. */
    ENFS_RESOLVE_VIRTUAL,

    /* The path is a musubi. */
    ENFS_RESOLVE_EXACT,

    /* The path is somewhere below a musubi. */
    ENFS_RESOLVE_INSIDE,
};

/**
 * Result of enfs_musubi_match. Points into the table and into the path that
 * was matched, so it is only valid while the lock is held and the path is alive.
 */
struct enfs_musubi_match {
    enum enfs_resolve_kind kind;

    /* The musubi that covers the path. NULL for MISS and VIRTUAL. */
    struct enfs_musubi* musubi;

    /*
     * The tree node sitting exactly at the path. Nullable.
     * Always set for EXACT and VIRTUAL. For INSIDE it is set when more musubi
     * are nested below the path (so the directory has extra children).
     */
    struct enfs_musubi_node* node;

    /* What is left of the path after the musubi, not normalized. Empty for EXACT. */
    const char* rest;
};

/* ---- lifecycle ---- */

void enfs_musubi_table_init(struct enfs_musubi_table* table);
void enfs_musubi_table_destroy(struct enfs_musubi_table* table);

/* ---- modify ---- */

/**
 * Add "from -> to", made at runtime (link, rename, ...): it goes to the delta
 * for userspace to pick up.
 *
 * -EEXIST   `from` is already a musubi (see enfs_musubi_update)
 * -EINVAL   `from` contains "." or "..", or `to` is empty
 */
int enfs_musubi_add(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags);

/**
 * Add "from -> to", handed down by userspace: it is saved already and goes
 * straight to main. Same errors as enfs_musubi_add.
 */
int enfs_musubi_load(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags);

/**
 * Remove the musubi at `from`. Musubi nested below it are kept.
 * Invalidates the delta if the musubi was saved.
 *
 * -ENOENT   `from` is not a musubi
 */
int enfs_musubi_remove(struct enfs_musubi_table* table, const char* from);

/**
 * Change target and flags of the musubi at `from`. Pass to = NULL to keep the target.
 * Invalidates the delta if the musubi was saved.
 *
 * -ENOENT   `from` is not a musubi
 */
int enfs_musubi_update(struct enfs_musubi_table* table, const char* from, const char* to, uint32_t flags);

/**
 * Rename `from` to `new_from`, taking everything nested below along.
 * `from` may also be a virtual directory, then only what is below it moves.
 * Invalidates the delta if anything that moved was saved.
 *
 * -ENOENT   nothing at `from`
 * -EEXIST   something is already at `new_from`
 * -EINVAL   `new_from` is below `from`, or `from` is the root
 */
int enfs_musubi_move(struct enfs_musubi_table* table, const char* from, const char* new_from);

/**
 * Remove everything. Invalidates the delta if anything was saved.
 */
void enfs_musubi_clear(struct enfs_musubi_table* table);

/* ---- query ---- */

size_t enfs_musubi_count(struct enfs_musubi_table* table);

/**
 * Get the musubi at exactly `from`. `to` and `flags` are both optional,
 * *to is a copy and must be kfree'd.
 *
 * -ENOENT   `from` is not a musubi
 */
int enfs_musubi_get(struct enfs_musubi_table* table, const char* from, char** to, uint32_t* flags);

/**
 * Return false to stop.
 * Called with the table lock held: do not call back into the table.
 */
typedef bool (*enfs_musubi_child_fn)(const char* name, size_t len, bool is_musubi, void* arg);
typedef bool (*enfs_musubi_entry_fn)(const char* from, const char* to, uint32_t flags, void* arg);

/**
 * List what the table puts directly inside directory `dir`, ordered by name,
 * skipping the first `skip` (to continue a readdir).
 *
 * -ENOENT   the table has nothing at or below `dir`
 */
int enfs_musubi_children(
    struct enfs_musubi_table* table,
    const char* dir,
    size_t skip,
    enfs_musubi_child_fn fn,
    void* arg
);

/**
 * Walk every musubi, main first and then the delta. `from` has no leading '/'.
 */
int enfs_musubi_for_each(struct enfs_musubi_table* table, enfs_musubi_entry_fn fn, void* arg);

/**
 * Print the whole table to dmesg.
 */
void enfs_musubi_dump(struct enfs_musubi_table* table);

/* ---- delta ---- */

/**
 * How many musubi wait in the delta. `invalid` is optional, when it comes back
 * true the delta is of no use and enfs_musubi_export is the way to go.
 */
size_t enfs_musubi_delta_count(struct enfs_musubi_table* table, bool* invalid);

/**
 * Hand out the oldest musubi of the delta and move it to main.
 * *from (no leading '/') and *to are copies and must be kfree'd.
 * On any error nothing is handed out and the delta stays as it was.
 *
 * -ENOENT   the delta is empty
 * -ESTALE   the delta is invalid
 */
int enfs_musubi_delta_next(struct enfs_musubi_table* table, char** from, char** to, uint32_t* flags);

/**
 * Move the whole delta to main and make it valid again, without handing
 * anything out: "what the table holds right now counts as saved".
 */
void enfs_musubi_delta_clear(struct enfs_musubi_table* table);

/**
 * What a full export is going to need: the number of musubi and the bytes
 * for all their `from` and `to` strings, terminators included.
 * Only a hint, the table can change before enfs_musubi_export runs.
 */
void enfs_musubi_export_size(struct enfs_musubi_table* table, size_t* count, size_t* bytes);

/**
 * Walk every musubi like enfs_musubi_for_each. If the walk gets through all of
 * them, the delta is moved to main and made valid again, in one step with the
 * walk: nothing can slip in between.
 *
 * -ECANCELED   `fn` returned false (ran out of room?), nothing was changed
 */
int enfs_musubi_export(struct enfs_musubi_table* table, enfs_musubi_entry_fn fn, void* arg);

/* ---- resolve ---- */

/**
 * Find what covers `path`. The caller must hold table->lock (read is enough).
 *
 * -EINVAL   `path` contains "." or ".."
 */
int enfs_musubi_match(struct enfs_musubi_table* table, const char* path, struct enfs_musubi_match* match);

/**
 * Translate a path inside enishifs to where it really is.
 *
 * Returns a new string to kfree, NULL when no musubi covers the path
 * (`kind` tells VIRTUAL from MISS), or an ERR_PTR. `kind` is optional.
 */
char* enfs_musubi_resolve_str(struct enfs_musubi_table* table, const char* path, enum enfs_resolve_kind* kind);

/**
 * Same, for a dentry of enishifs. Works on a negative dentry too, so it can be
 * used straight from ->lookup.
 */
char* enfs_musubi_resolve_dentry(struct dentry* dentry, enum enfs_resolve_kind* kind);

/**
 * Resolve, then look the result up (kern_path) and hand back a referenced
 * struct path, to be released with path_put.
 *
 * `lookup_flags` are LOOKUP_* (LOOKUP_FOLLOW to follow a symlink at the target).
 *
 * -ENOENT   no musubi covers the path (kind is MISS or VIRTUAL),
 *           or one does but its target is not there (kind is EXACT or INSIDE)
 * -ELOOP    (dentry variant only) the target is inside this enishifs
 */
int enfs_musubi_lookup_str(
    struct enfs_musubi_table* table,
    const char* path,
    unsigned int lookup_flags,
    struct path* out,
    enum enfs_resolve_kind* kind
);

int enfs_musubi_lookup_dentry(
    struct dentry* dentry,
    unsigned int lookup_flags,
    struct path* out,
    enum enfs_resolve_kind* kind
);

#endif
