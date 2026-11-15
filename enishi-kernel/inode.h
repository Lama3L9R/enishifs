#ifndef ENFS_KO_INODE_H
#define ENFS_KO_INODE_H

#include <linux/fs.h>

#define ENFS_INODE_ROOT_ID 0l

struct enfs_inode {
    struct inode* delegation;

    struct inode this;
};

void enfs_inode_init(void);
void enfs_inode_deinit(void);

struct inode* enfs_inode_alloc_root(struct super_block* super);
struct inode* enfs_inode_alloc(struct super_block* super);
struct inode* enfs_inode_get_or_create(struct super_block* super, struct inode* delegation);
void enfs_inode_evict(struct inode* inode);

void enfs_inode_free(struct inode* inode);

static inline struct enfs_inode* enfs_inode_wrap(struct inode* inode) {
    return container_of(inode, struct enfs_inode, this);
}
static inline struct inode* enfs_inode_unwrap(struct enfs_inode* wrapped) {
    return &wrapped->this;
}

#endif