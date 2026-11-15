#ifndef ENFS_KO_KENFS_H
#define ENFS_KO_KENFS_H

#include <linux/fs.h>
#include "enishifs.h"
#include "musubi.h"

struct enfs_kishi {

};

/**
 * Superblock private data
 */
struct enfs_super {
    /* Tree + list + lock of all musubi, see musubi.h */
    struct enfs_musubi_table musubi;

    bool locked;
};

static inline struct enfs_super* enfs_super_of(struct super_block* super) {
    return super->s_fs_info;
}


#define KDebugPrint(fmt, ...) pr_info(fmt, ##__VA_ARGS__); pr_info("\n");
#define KWarningPrint(fmt, ...) pr_warn(fmt, ##__VA_ARGS__); pr_warn("\n");

#endif