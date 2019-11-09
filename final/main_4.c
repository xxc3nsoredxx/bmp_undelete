#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "main.h"

#define BYTES_PER_BLOCK     (4 * 1024)
#define BLOCKS_PER_GROUP    (8 * BYTES_PER_BLOCK)
#define BYTES_PER_GROUP     (BLOCKS_PER_GROUP * BYTES_PER_BLOCK)
#define GROUP0_PAD          (1024)

/* Block groups containing superblock clones */
const off_t SUPER_GROUPS [] = {
    0, 1, 3, 5,
    7, 9, 25, 49,
    27, 125, 243, 343
};

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
unsigned int ngroups = 0;
unsigned int nsuperblocks = 0;
struct sb_s *s = 0;

void usage () {
    printf("Usage: ./superblock [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* Cleanup tasks */
void cleanup (int exit_code) {
    if (s) {
        free(s);
    }
    if (dev != MAP_FAILED) {
        munmap(dev, dev_size);
    }
    if (devf >= 0) {
        close(devf);
    }

    exit(exit_code);
}

/* Initialization tasks */
int init (const char *fname) {
    /* Attempt to open the device */
    devf = open(fname, O_RDONLY);
    if (devf < 0) {
        printf("Unable to open: %s\n", fname);
        cleanup(-1);
    }
    
    /* Get size of device */
    if (ioctl(devf, BLKGETSIZE, &dev_size) == -1) {
        printf("Unable to get size of device: %s\n", fname);
        cleanup(-1);
    }
    /* ioctl call gives number 512 byte sectors */
    dev_size *= 512;

    /* Attempt to mmap the device */
    dev = mmap(0, dev_size, PROT_READ, MAP_SHARED, devf, 0);
    if (dev == MAP_FAILED) {
        printf("Unable to mmap device: %s\n", fname);
        perror("mmap");
        cleanup(-1);
    }

    return 0;
}

int main (int argc, char **argv) {
    unsigned int cx;

    /* Test args */
    if (argc != 2) {
        usage();

        return -1;
    }

    /* Test if running as root */
    if (getuid()) {
        usage();
    }

    /* Initialize */
    init(*(argv + 1));

    /* Calculate the number of block groups */
    ngroups = dev_size / BYTES_PER_GROUP;

    /* Calculate the number of superblocks to go through */
    for (cx = 0; cx < sizeof(SUPER_GROUPS) / sizeof(*SUPER_GROUPS); cx++) {
        if (ngroups < *(SUPER_GROUPS + cx)) {
            nsuperblocks = cx;
            break;
        }
    }
    if (nsuperblocks == 0) {
        printf("Failed to get the number of superblocks\n");
        cleanup(-1);
    }

    /* Allocate space for superblocks */
    s = calloc(nsuperblocks, sizeof(*s));
    if (!s) {
        printf("Failed to allocate space for superblocks\n");
        cleanup(-1);
    }

    /* Copy the superblocks */
    for (cx = 0; cx < nsuperblocks; cx++) {
        off_t off = *(SUPER_GROUPS + cx) * BYTES_PER_GROUP;

        if (cx == 0) {
            off += GROUP0_PAD;
        }

        memcpy((s + cx), (dev + off), sizeof(*s));
    }

    /* Print superblock info */
    for (cx = 0; cx < nsuperblocks; cx++) {
        printf("Suprblock %u in group %lu\n", cx, *(SUPER_GROUPS + cx));
        printf("  Free blocks: %u\n", (s + cx)->s_free_blocks_count_lo);
        printf("  Free inodes: %u\n", (s + cx)->s_free_inodes_count);
        printf("  Inode size:  %u\n", (s + cx)->s_inode_size);
        printf("  Magic:       %04X\n", (s + cx)->s_magic);
        printf("  Goup number: %u\n", (s + cx)->s_block_group_nr);
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
