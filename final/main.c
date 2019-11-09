#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "bmp.h"
#include "ext.h"

#define CSI     "\x1B["
#define RED     "" CSI "91m"
#define YELLOW  "" CSI "93m"
#define GREEN   "" CSI "92m"
#define RESET   "" CSI "0m"
#define BAD(A)  "" RED "[-] " A RESET
#define INFO(A) "" YELLOW "[!] " RESET A
#define GOOD(A) "" GREEN "[+] " A RESET

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
size_t nblocks;
struct sb_s *sb = 0;
size_t ipg;
size_t ipb;
uint32_t *bmp_starts = 0;
size_t n_bmp_starts = 0;
uint32_t *indirects [3] = {
    0, 0, 0
};
size_t n_indirects [3] = {
    0, 0, 0
};

void usage () {
    printf("Usage: ./recover [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* 
 * Cleanup tasks
 */
void cleanup (int exit_code) {
    int cx;

    for (cx = 0; cx < 3; cx++) {
        if ((indirects + cx)) {
            free(*(indirects + cx));
        }
    }
    if (bmp_starts) {
        free(bmp_starts);
    }
    if (dev != MAP_FAILED) {
        munmap(dev, dev_size);
    }
    if (devf >= 0) {
        close(devf);
    }

    exit(exit_code);
}

/* 
 * Initialization tasks
 */
void init (const char *fname) {
    /* Attempt to open the device */
    devf = open(fname, O_RDONLY);
    if (devf < 0) {
        printf(BAD("Unable to open: %s\n"), fname);
        cleanup(-1);
    }
    
    /* Get size of device */
    if (ioctl(devf, BLKGETSIZE, &dev_size) == -1) {
        printf(BAD("Unable to get size of device: %s\n"), fname);
        cleanup(-1);
    }
    /* ioctl call gives number 512 byte sectors */
    dev_size *= 512;

    /* Attempt to mmap the device */
    dev = mmap(0, dev_size, PROT_READ, MAP_SHARED, devf, 0);
    if (dev == MAP_FAILED) {
        printf(BAD("Unable to mmap device: %s\n"), fname);
        cleanup(-1);
    }

    /* Calculate the number of blocks on the drive */
    nblocks = dev_size / BYTES_PER_BLOCK;

    /* Get the superblock */
    sb = (struct sb_s*)(dev + SB_OFF);

    /* Get the number of inodes per group */
    ipg = sb->s_inodes_per_group;

    /* Calculate the number of inodes per block */
    ipb = BYTES_PER_BLOCK / sb->s_inode_size;
}

/*
 * Go through the drive and find all the potential BMP starting blocks
 */
void get_bmp_start () {
    uint32_t cx;
    size_t percent = 0;
    size_t cur_percent;

    printf(INFO("Searching for potential BMP start blocks...\n"));

    /* Parse each block of the drive and test for BMP magic number */
    for (cx = 0; cx < nblocks; cx++) {
        cur_percent = cx * 100 / nblocks;
        if (!memcmp((dev + BLOCK_OFF(cx)), BMP_MAGIC, 2)) {
            printf(GOOD("Found at block: %u!\n"), cx);
            n_bmp_starts++;
            bmp_starts = realloc(bmp_starts,
                n_bmp_starts * sizeof(*bmp_starts));
            *(bmp_starts + (n_bmp_starts - 1)) = cx;
        }

        /* Log percentage through disk */
        if (cur_percent % 10 == 0 && cur_percent != percent) {
            percent += 10;
            printf(INFO("%lu%% complete...\n"), percent);
        }
    }

    printf(INFO("Done!\n\n"));
}

/*
 * Go through the drive and find all the potential indirect blocks
 */
void get_indirects () {
    uint32_t cx;
    uint32_t cx2;
    uint32_t cx3;
    size_t percent = 0;
    size_t cur_percent;
    uint32_t *cur_block;
    uint32_t cur_group;
    struct gd_s *gd;
    int pot_indir;
    int zero;

    printf(INFO("Searching for potential indirect blocks...\n"));

    /* Parse each block of the drive and test for potential indirect blocks */
    for (cx = 0; cx < nblocks; cx++) {
        cur_percent = cx * 100 / nblocks;
        pot_indir = 0;
        zero = 0;

        /* Get info if a new group */
        if (cx % BLOCKS_PER_GROUP == 0) {
            cur_group = cx / BLOCKS_PER_GROUP;
            /* Get the group descriptor */
            gd = (struct gd_s*)(dev + GD_OFF(cur_group));
        }

        /* Get the current block */
        cur_block = (uint32_t*)(dev + BLOCK_OFF(cx));

        /* Test for increment or until zeros */
        for (cx2 = 0; cx2 < BYTES_PER_BLOCK / sizeof(*cur_block); cx2++) {
            /* Test for only zeros at the end */
            if (zero) {
                if (*(cur_block + cx2) != 0) {
                    pot_indir = 0;
                    break;
                }
                continue;
            }

            /* Test if listing ends on first of a set of 4 */
            if (cx2 % 4 == 0 && *(cur_block + cx2) == 0) {
                /* If very first block, zero is invalid */
                if (cx == 0) {
                    pot_indir = 0;
                    break;
                }

                /* Begin tracking zeros */
                zero = 1;
                continue;
            }

            /* Test if chunks of 4 are consecutive or zero found */
            for (cx3 = 1; cx3 < 4; cx3++) {
                /* Test if next is zero */
                if (*(cur_block + (cx2 + cx3)) == 0) {
                    pot_indir = 1;
                    zero = 1;
                    cx2 += cx3;
                    break;
                }

                /* Test if next is cur + 1 */
                if (*(cur_block + (cx2 + cx3)) ==
                    *(cur_block + (cx2 + cx3 - 1)) + 1) {
                    pot_indir = 1;
                } else {
                    pot_indir = 0;
                    break;
                }
            }
            if (zero && pot_indir) {
                continue;
            } else if (!pot_indir) {
                break;
            } else {
                /* No end cases found, continue to next group of 4 */
                cx2 += 3;
            }
        }

        /* Log potential indirect block */
        if (pot_indir) {
            *n_indirects += 1;
            *indirects = realloc(*indirects,
                *n_indirects * sizeof(**indirects));
            *(*indirects + *n_indirects - 1) = cx;
            printf(GOOD("Indirect found at block: %u\n"), cx);
        }

        /* Log percentage through disk */
        if (cur_percent % 10 == 0 && cur_percent != percent) {
            percent += 10;
            printf(INFO("%lu%% complete...\n"), percent);
        }
    }


    printf(INFO("Done!\n\n"));
}

int main (int argc, char **argv) {
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

    /* Gather all the BMP starting blocks */
    get_bmp_start();
    if (!bmp_starts) {
        printf(BAD("No potential BMP start blocks found, exiting...\n"));
        cleanup(-1);
    }

    /* Gather all the potential indirect blocks */
    get_indirects();
    if (!(*(indirects + 0) || *(indirects + 1) || *(indirects + 2))) {
        printf(INFO("No potential indirect blocks found...\n"));
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
