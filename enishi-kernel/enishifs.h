#ifndef ENISHIFS_ENISHIFS_H
#define ENISHIFS_ENISHIFS_H

#ifdef __KERNEL__
#include <linux/types.h>
#define ENFS_KERNELSPACE
#else
#include <stdint.h>
#define ENFS_USERSPACE
#endif

// 縁（えにし）fs
#define ENFS_MAGIC 0x000000007366017Eull

#define ENFS_KISHI_FL_RD (1ull << 0)
#define ENFS_KISHI_FL_RW (1ull << 1)
#define ENFS_KISHI_FL_FAILFAST (1ull << 2)

#define ENFS_DATAPOOL_NULL   0
#define ENFS_DATAPOOL_RAW    1
#define ENFS_DATAPOOL_STR    2
#define ENFS_DATAPOOL_KISHI  3
#define ENFS_DATAPOOL_MUSUBI 4

#define ENFS_MUSUBI_USER_CREATED  (1 << 0)
#define ENFS_MUSUBI_ALLOW_HANGING (1 << 1)

#define ENFS_MUSUBI_FL_DEFAULT (ENFS_MUSUBI_ALLOW_HANGING)

typedef struct KEnishiSuperBlock_St {
    const char* mountPoint;
    int rootfd;
} KEnishiSuperBlock;

typedef struct KEnishiKishi_St {
    char* device;
    char* mount;
    char* mountOptions;
    char* fsType;
    uint32_t flags;
} KEnishiKishi;

typedef struct KEnishiMusubi_St {
    char* from;
    char* to;
    uint32_t flags;
} KEnishiMusubi;

#ifdef ENFS_USERSPACE

typedef struct DataPoolReference_St {
    uint32_t type;
    uint32_t size;

    uint32_t offset;
} DataPoolReference;

typedef struct EnishiKishi_St {
    /**
     * Type of underlying file system
     */
    DataPoolReference type;

    /**
     * Mount point relative to enishifs mount point
     */
    DataPoolReference mount;

    /**
     * Device source path
     */
    DataPoolReference device;

    /**
     * Mount flags
     */
    uint64_t flags;
} EnishiKishi;


typedef struct EnishiMusubi_St {
    /**
     * Flags
     */
    uint64_t flags;
    DataPoolReference from;
    DataPoolReference to;
} EnishiMusubi;

typedef struct EnishiMetadata_St {
    /**
     * Always 0x000000007366017Eull in little-endian
     */
    uint64_t magic;

    /**
     * Size of this struct, including magic
     */
    uint64_t metadataSz;

    /**
     * Enishi Metadata Version
     */
    uint32_t version;

    /**
     * Raw data
     */
    uint8_t dataPool[];
} EnishiMetadata;

uint32_t EnishiGetMusubiCount(EnishiMetadata* meta);
EnishiMusubi* EnishiGetMusubi(EnishiMetadata* meta, uint32_t index);
uint32_t EnishiGetKishiCount(EnishiMetadata* meta);
EnishiKishi* EnishiGetKishi(EnishiMetadata* meta, uint32_t index);

/**
 * Add a kishi to the filesystem.
 */
uint32_t KEnishiAddKishi(KEnishiSuperBlock* fs, KEnishiKishi kishi);

/**
 * Add a musubi to the filesystem.
 */
uint32_t KEnishiAddMusubi(KEnishiSuperBlock* fs, KEnishiMusubi musubi);

/**
 * Attempt to export all data to given kishi and musubi buffer. 
 * All delta will be merged into the main table.
 * 
 * On your first call: KEnishiExportAll(fs, NULL, &kishiSz, NULL, &musubiSz) to obtain sizes
 * Then allocate buffers for them
 * Then call again with buffers: KEnishiExportAll(fs, buff1, &kishiSz, buff2, &musubiSz)
 * If between two calls, musubi/kishi has a change, buffer will not be written, and the new size is set.
 * You should reallocate a buffer for that, and
 * The process repeats until size of the buffer is large enough to fit all elements. 
 * A return code success will be returned to indicate a full success.
 * 
 * You only need to allocate one buffer, kishi and musubi pointer will be pointing to address in that buffer.
 * You also allocate buffer based on bufferSz only.
 * All pointers will be pointing to data in the buffer, so you should not do: free(musubi->from)
 */
uint32_t KEnishiExportAll(KEnishiSuperBlock* fs, void* buffer, size_t* bufferSz, KEnishiKishi* kishi, uint32_t* kishiSz, KEnishiMusubi* musubi, uint32_t* musubiSz);

/**
 * Check if musubi / kishi has a incremental change. If any delta exist, call KEnishiGetDeltaNext repeatedly.
 * If deltaInvalidFlag is set, you should call KEnishiExportAll instead.
 */
uint32_t KEnishiHasDelta(KEnishiSuperBlock* fs, uint32_t* deltas, uint8_t* deltaInvalidFlag);

/**
 * Get the next differnece. Dynamically added will be moved to static after a call.
 * This means, you must save the data and failing to do so will lost dynamically allocated links after unmount.
 */
uint32_t KEnishiGetDeltaNext(KEnishiSuperBlock* fs, KEnishiKishi* kishi, KEnishiMusubi* musubi);

/**
 * Merge delta table into the main table.
 */
uint32_t KEnishiClearDelta(KEnishiSuperBlock* fs);

/**
 * Deactivates the filesystem for any reason
 * 
 * This will cause all read/write/ls operations FAIL
 */
uint32_t KEnishiLock(KEnishiSuperBlock* fs);

/**
 * Reactivates the filesystem after a failure / initial mount
 */
uint32_t KEnishiUnlock(KEnishiSuperBlock* fs);

/**
 * Send raw ioctl commands
 */
uint32_t EnishiControl(KEnishiSuperBlock* fs, uint32_t controlCode, ...);
#endif

#endif