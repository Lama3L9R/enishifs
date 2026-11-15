#include <linux/fs.h>
#include "kenfs.h"
#include "inode.h"

/* Every unimplemented op announces itself in dmesg, so you can see what the VFS actually hit. */
#define ENFS_STUB() pr_warn_ratelimited("enishifs: stub: %s\n", __func__)

/* ========================================================================
 * inode_operations
 * ======================================================================== */

/* ---- all inode types ---- */

static int enfs_inode_permission(struct mnt_idmap* idmap, struct inode* inode, int mask) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_getattr(
    struct mnt_idmap* idmap,
    const struct path* path,
    struct kstat* stat,
    u32 request_mask,
    unsigned int query_flags
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_setattr(struct mnt_idmap* idmap, struct dentry* dentry, struct iattr* attr) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static ssize_t enfs_inode_listxattr(struct dentry* dentry, char* list, size_t size) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

/* ---- directory only ---- */

static struct dentry* enfs_inode_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags) {
    ENFS_STUB();
    return ERR_PTR(-EOPNOTSUPP);
}

static int enfs_inode_create(
    struct mnt_idmap* idmap,
    struct inode* dir,
    struct dentry* dentry,
    umode_t mode,
    bool excl
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_link(struct dentry* old_dentry, struct inode* dir, struct dentry* new_dentry) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_unlink(struct inode* dir, struct dentry* dentry) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_symlink(
    struct mnt_idmap* idmap,
    struct inode* dir,
    struct dentry* dentry,
    const char* target
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_mkdir(struct mnt_idmap* idmap, struct inode* dir, struct dentry* dentry, umode_t mode) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_rmdir(struct inode* dir, struct dentry* dentry) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_mknod(
    struct mnt_idmap* idmap,
    struct inode* dir,
    struct dentry* dentry,
    umode_t mode,
    dev_t dev
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_inode_rename(
    struct mnt_idmap* idmap,
    struct inode* old_dir,
    struct dentry* old_dentry,
    struct inode* new_dir,
    struct dentry* new_dentry,
    unsigned int flags
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

/* ---- symlink only ---- */

static const char* enfs_inode_get_link(struct dentry* dentry, struct inode* inode, struct delayed_call* done) {
    ENFS_STUB();
    return ERR_PTR(-EOPNOTSUPP);
}

/* ========================================================================
 * file_operations
 * ======================================================================== */

/* ---- directory and regular file ---- */

static int enfs_file_open(struct inode* inode, struct file* file) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_file_release(struct inode* inode, struct file* file) {
    ENFS_STUB();
    return 0;
}

static loff_t enfs_file_llseek(struct file* file, loff_t offset, int whence) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_file_fsync(struct file* file, loff_t start, loff_t end, int datasync) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static long enfs_file_unlocked_ioctl(struct file* file, unsigned int cmd, unsigned long arg) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

/* ---- directory only ---- */

static int enfs_file_iterate_shared(struct file* file, struct dir_context* ctx) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

/* ---- regular file only ---- */

static ssize_t enfs_file_read_iter(struct kiocb* iocb, struct iov_iter* to) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static ssize_t enfs_file_write_iter(struct kiocb* iocb, struct iov_iter* from) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_file_mmap(struct file* file, struct vm_area_struct* vma) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static int enfs_file_flush(struct file* file, fl_owner_t id) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static long enfs_file_fallocate(struct file* file, int mode, loff_t offset, loff_t len) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static ssize_t enfs_file_splice_read(
    struct file* in,
    loff_t* ppos,
    struct pipe_inode_info* pipe,
    size_t len,
    unsigned int flags
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

static ssize_t enfs_file_splice_write(
    struct pipe_inode_info* pipe,
    struct file* out,
    loff_t* ppos,
    size_t len,
    unsigned int flags
) {
    ENFS_STUB();
    return -EOPNOTSUPP;
}

/* ========================================================================
 * ops tables
 *
 * One table per inode type: the VFS decides "is this a symlink" by checking
 * whether i_op->get_link is set, so a single shared table would turn every
 * regular file into a symlink.
 * ======================================================================== */

static const struct inode_operations dir_inode_ops = {
    .lookup     = enfs_inode_lookup,
    .create     = enfs_inode_create,
    .link       = enfs_inode_link,
    .unlink     = enfs_inode_unlink,
    .symlink    = enfs_inode_symlink,
    .mkdir      = enfs_inode_mkdir,
    .rmdir      = enfs_inode_rmdir,
    .mknod      = enfs_inode_mknod,
    .rename     = enfs_inode_rename,
    .permission = enfs_inode_permission,
    .getattr    = enfs_inode_getattr,
    .setattr    = enfs_inode_setattr,
    .listxattr  = enfs_inode_listxattr,
};

static const struct inode_operations reg_inode_ops = {
    .permission = enfs_inode_permission,
    .getattr    = enfs_inode_getattr,
    .setattr    = enfs_inode_setattr,
    .listxattr  = enfs_inode_listxattr,
};

static const struct inode_operations symlink_inode_ops = {
    .get_link   = enfs_inode_get_link,
    .permission = enfs_inode_permission,
    .getattr    = enfs_inode_getattr,
    .setattr    = enfs_inode_setattr,
    .listxattr  = enfs_inode_listxattr,
};

static const struct file_operations dir_file_ops = {
    .open           = enfs_file_open,
    .release        = enfs_file_release,
    .llseek         = enfs_file_llseek,
    .read           = generic_read_dir, /* read(2) on a directory -> -EISDIR */
    .iterate_shared = enfs_file_iterate_shared,
    .fsync          = enfs_file_fsync,
    .unlocked_ioctl = enfs_file_unlocked_ioctl,
};

static const struct file_operations reg_file_ops = {
    .open           = enfs_file_open,
    .release        = enfs_file_release,
    .llseek         = enfs_file_llseek,
    .read_iter      = enfs_file_read_iter,
    .write_iter     = enfs_file_write_iter,
    .mmap           = enfs_file_mmap,
    .flush          = enfs_file_flush,
    .fsync          = enfs_file_fsync,
    .fallocate      = enfs_file_fallocate,
    .splice_read    = enfs_file_splice_read,
    .splice_write   = enfs_file_splice_write,
    .unlocked_ioctl = enfs_file_unlocked_ioctl,
};

static struct kmem_cache* enfs_inode_cache;

static void enfs_inode_ctor(void* obj) {
    struct enfs_inode* inode = obj;

    inode_init_once(enfs_inode_unwrap(inode));
}

void enfs_inode_init(void) {
    enfs_inode_cache = kmem_cache_create(
        "Enishi-Inode-Cache",
        sizeof(struct enfs_inode),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
        enfs_inode_ctor
    );
}

void enfs_inode_deinit(void) {
    rcu_barrier();
    kmem_cache_destroy(enfs_inode_cache);
}

struct inode* enfs_inode_alloc_root(struct super_block* super) {
    struct inode* inode = new_inode(super);

    if (!inode) {
        return ERR_PTR(-ENOMEM);
    }

    inode->i_ino = ENFS_INODE_ROOT_ID;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_op = &dir_inode_ops;
	inode->i_fop = &dir_file_ops;

    set_nlink(inode, 2);
	simple_inode_init_ts(inode);

    return inode;
}

struct inode* enfs_inode_alloc(struct super_block* super) {
    struct enfs_inode* inode = alloc_inode_sb(super, enfs_inode_cache, GFP_KERNEL);

    if (inode == NULL) {
        return NULL;
    }

    inode->delegation = NULL;
    return enfs_inode_unwrap(inode);
}

static int enfs_inode_test(struct inode *inode, void *data) {
	return enfs_inode_wrap(inode)->delegation == data;
}

static int enfs_inode_set(struct inode *inode, void *data) {
	enfs_inode_wrap(inode)->delegation = data;
	return 0;
}

struct inode* enfs_inode_get_or_create(struct super_block* super, struct inode* delegation) {
    if (!igrab(delegation)) {
        return ERR_PTR(-ESTALE);
    }

    struct inode* inode = iget5_locked(super, delegation->i_ino, enfs_inode_test, enfs_inode_set, delegation);

    if (!inode) {
		iput(delegation);
		return ERR_PTR(-ENOMEM);
	}
    
	if (!(inode->i_state & I_NEW)) {
		iput(delegation);
		return inode;
	}

    inode->i_ino = get_next_ino();
	fsstack_copy_attr_all(inode, delegation);
	fsstack_copy_inode_size(inode, delegation);

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &dir_inode_ops;
	    inode->i_fop = &dir_file_ops;
    } else if (S_ISLNK(inode->i_mode)) {
        inode->i_op = &symlink_inode_ops;
    } else {
	    inode->i_op = &reg_inode_ops;
	    inode->i_fop = &reg_file_ops;
    }

	unlock_new_inode(inode);

    return inode;
}

void enfs_inode_evict(struct inode* inode) {
    struct enfs_inode* wrapper = enfs_inode_wrap(inode);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

    if (likely(wrapper->delegation != NULL)) {
        iput(wrapper->delegation);
        wrapper->delegation = NULL;
    } else {
        KDebugPrint("<Ino/Evict:DelegateNull> Evil iput call prevented, but how is this even possible?")
    }
}

void enfs_inode_free(struct inode* inode) {
    kmem_cache_free(enfs_inode_cache, enfs_inode_wrap(inode));
}
