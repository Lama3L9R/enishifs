#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs_context.h>


#include "kenfs.h"
#include "inode.h"

MODULE_AUTHOR("lamadaemon");
MODULE_DESCRIPTION("Enishi File System Support KO Module");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");

static void enfs_kill_superblock(struct super_block* sb) {
    kill_block_super(sb);

    KDebugPrint("Unmounted superblock")
}

static void enfs_super_put(struct super_block* super) {
    struct enfs_super* info = enfs_super_of(super);

    if (info == NULL) {
        return;
    }

    enfs_musubi_table_destroy(&info->musubi);
    kfree(info);
    super->s_fs_info = NULL;
}

static struct super_operations super_ops = {
    .alloc_inode = enfs_inode_alloc,
    .free_inode = enfs_inode_free,
    .evict_inode = enfs_inode_evict,
    .drop_inode = generic_delete_inode,
    .put_super = enfs_super_put,
};

static int enfs_super_init(struct super_block* super, struct fs_context* ctx) {
    super->s_magic = ENFS_MAGIC; // fs identifier
    super->s_maxbytes = MAX_LFS_FILESIZE; // Allow large file
    super->s_time_gran = 1; // Enable ns level time
    super->s_blocksize = PAGE_SIZE;
    super->s_blocksize_bits = PAGE_SHIFT;

    super->s_op = &super_ops;

    super->s_root = d_make_root(enfs_inode_alloc_root(super));
    super->s_fs_info = kzalloc(sizeof(struct enfs_super), GFP_KERNEL);

    if (super->s_fs_info == NULL) {
        return -ENOMEM;
    }

    enfs_musubi_table_init(&enfs_super_of(super)->musubi);

    return 0;
}

static int enfs_get_tree(struct fs_context* ctx) {
    return get_tree_nodev(ctx, enfs_super_init);
}

static const struct fs_context_operations enfs_ctx_ops = {
    .get_tree = enfs_get_tree
};

static int enishi_init_fs_context(struct fs_context* ctx)
{
	ctx->ops = &enfs_ctx_ops;
	return 0;
}

static struct file_system_type enishifs_type = {
    .owner = THIS_MODULE,
    .name = "enishifs",
    .kill_sb = kill_anon_super,
    .init_fs_context = enishi_init_fs_context,
};

static int __init enfs_ko_init(void) {
    KDebugPrint("Enishifs 1.0 (linux) loading...")

    enfs_inode_init();
    register_filesystem(&enishifs_type);

    KDebugPrint("Enishifs 1.0 (linux) loaded")
    return 0;
}

static void __exit enfs_ko_exit(void) {
    KDebugPrint("Enishifs 1.0 (linux) unloading...")

    unregister_filesystem(&enishifs_type);
    enfs_inode_deinit();

    KDebugPrint("Enishifs 1.0 (linux) unloaded")
}

module_init(enfs_ko_init);
module_exit(enfs_ko_exit);
