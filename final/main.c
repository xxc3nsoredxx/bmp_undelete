#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "ext.h"

#define BYTES_PER_BLOCK     (4 * 1024)
#define BLOCKS_PER_GROUP    (8 * BYTES_PER_BLOCK)
#define BYTES_PER_GROUP     (BLOCKS_PER_GROUP * BYTES_PER_BLOCK)
#define GROUP0_PAD          (1024)

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;

void usage () {
    printf("Usage: ./recover [device]\n");
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

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
