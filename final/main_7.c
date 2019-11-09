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

/*
 * Relevant files       inodes      blocks      (gotten from inode program)
 * test_2x2.bmp         inode 12    block 17414
 * test_land.bmp        inode 14    block 17427
 * test_mandelbrot.bmp  inode 13    block 17932
 */

#define BYTES_PER_BLOCK     (4 * 1024)
#define BLOCKS_PER_GROUP    (8 * BYTES_PER_BLOCK)
#define BYTES_PER_GROUP     (BLOCKS_PER_GROUP * BYTES_PER_BLOCK)
#define GROUP0_PAD          (1024)

const char BMP_MAGIC [] = {
    0x42, 0x4D
};

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
size_t nblocks;

void usage () {
    printf("Usage: ./list [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* Cleanup tasks */
void cleanup (int exit_code) {
    if (dev != MAP_FAILED) {
        munmap(dev, dev_size);
    }
    if (devf >= 0) {
        close(devf);
    }

    exit(exit_code);
}

/* Initialization tasks */
void init (const char *fname) {
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
}

int main (int argc, char **argv) {
    size_t cx;
    size_t percent = 0;

    /* Test args */
    if (argc != 2) {
        usage();

        exit(-1);
    }

    /* Test if running as root */
    if (getuid()) {
        usage();

        exit(-1);
    }

    /* Initialize */
    init(*(argv + 1));

    /* Calculate the number of blocks on the drive */
    nblocks = dev_size / BYTES_PER_BLOCK;

    /* Parse each block of the drive and test for BMP magic number */
    for (cx = 0; cx < nblocks; cx++) {
        size_t cur_percent = cx * 100 / nblocks;
        if (!memcmp((dev + (cx * BYTES_PER_BLOCK)), BMP_MAGIC, 2)) {
            printf("Found potential BMP start block: %lu\n", cx);
        }
        if (cur_percent % 10 == 0 && cur_percent != percent) {
            percent += 10;
            printf("%lu%% complete\n", percent);
        }
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
