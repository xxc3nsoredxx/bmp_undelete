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
 * Relevant files       inodes      indirect blocks first block hex first block dec last block hex  last block dec
 * test_land.bmp        inode 13    block 17050     0x0000440D      17421           0x000043C0      17344
 * test_mandelbrot.bmp  inode 14    block 17051     0x0000441D      17437           0x000046AF      18095
 */

#define BYTES_PER_BLOCK     (4 * 1024)
#define BLOCKS_PER_GROUP    (8 * BYTES_PER_BLOCK)
#define BYTES_PER_GROUP     (BLOCKS_PER_GROUP * BYTES_PER_BLOCK)
#define GROUP0_PAD          (1024)

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
size_t nblocks;
struct sb_s *sb = 0;
size_t ipg;
size_t ipb;
uint32_t cur_group = 0;
struct gd_s *gd = 0;
uint32_t furthest_meta = 0;

void usage () {
    printf("Usage: ./indir [device]\n");
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

    /* Get the superblock */
    sb = (struct sb_s*)(dev + GROUP0_PAD);

    /* Get the number of inodes per group */
    ipg = sb->s_inodes_per_group;

    /* Get the number of inodes per block */
    ipb = BYTES_PER_BLOCK / sb->s_inode_size;

    printf("Indirect block First block Last block\n");
    printf("=====================================\n");
    /* Parse each block of the drive and test for potential indirect block */
    for (cx = 0; cx < nblocks; cx++) {
        size_t cur_percent = cx * 100 / nblocks;
        uint32_t *cur_block = 0;
        unsigned int cx2;
        /* uint32_t cur_num = 0; */
        uint32_t first_num = 0;
        uint32_t last_num = 0;
        int pot_indir = 0;
        int zero = 0;

        /* Every new group, get the relevant group info and move to the first data block */
        if (cx % BLOCKS_PER_GROUP == 0) {
            /* Get the current group number */
            cur_group = cx / BLOCKS_PER_GROUP;
            /* Get the group descriptor */
            gd = (struct gd_s*)(dev + BYTES_PER_BLOCK + (cur_group * sizeof(*gd)));
            /* Start with the furthest being the block bitmap */
            furthest_meta = gd->bg_block_bitmap_lo;
            /* If the inode bitmap is further, set to that */
            furthest_meta = (gd->bg_inode_bitmap_lo > furthest_meta) ? gd->bg_inode_bitmap_lo : furthest_meta;
            /* If the inode table is further, set to that */
            furthest_meta = (gd->bg_inode_table_lo > furthest_meta) ? gd->bg_inode_table_lo : furthest_meta;
            /* If the snapshot exclude bitmap is further, set to that */
            furthest_meta = (gd->bg_exclude_bitmap_lo > furthest_meta) ? gd->bg_exclude_bitmap_lo : furthest_meta;
            /* Set cx to the first actual data block */
            cx = furthest_meta;
            /* If the inode table is the furthest, calculate where the inode table ends */
            if (cx == gd->bg_inode_table_lo) {
                cx += (ipg / ipb);
                /* Account for overflow of a few inodes into a new block */
                cx += (ipg % ipb) ? 1 : 0;
            }
            /* Else, jump past the bitmap we're at */
            else {
                cx++;
            }
        }

        /* Get pointer to current block */
        cur_block = (uint32_t*)(dev + (cx * BYTES_PER_BLOCK));

        /* Test all the block numbers or until the rest is zeros for increment */
        for (cx2 = 0; cx2 < BYTES_PER_BLOCK / sizeof(*cur_block); cx2++) {
            /* Testing for only zeros at the end */
            if (zero) {
                /* if zeros found and then other numbers, not an ndirect block */
                if (*(cur_block + cx2) != 0) {
                    pot_indir = 0;
                    break;
                }
                continue;
            }

            /* Test if block listing ends on first of a set of 4 */
            if (cx2 % 4 == 0 && *(cur_block + cx2) == 0) {
                /* If on very first block, zero is an invalid entry */
                if (cx2 == 0) {
                    pot_indir = 0;
                    break;
                }
                /* If not on first block, then initially save previous as the last number */
                if (!zero) {
                    last_num = *(cur_block + (cx2 - 1));
                }
                zero = 1;
                continue;
            }

            /* Initial block is saved as a reference point */
            /*
            if (cx2 % 4 == 0) {
                cur_num = *cur_block;
            }
            */

            /* Test if chunks of 4 are consecutive or found a zero */
            /* Test if 2nd block is zero */
            if (*(cur_block + (cx2 + 1)) == 0) {
                pot_indir = 1;
                zero = 1;
                /* Save 1st in set of 4 */
                last_num = *(cur_block + (cx2 + 0));
                cx2 += 1;
                continue;
            }
            /* Test if 2nd block is 1st + 1 */
            if (*(cur_block + (cx2 + 1)) == *(cur_block + (cx2 + 0)) + 1) {
                pot_indir = 1;
            } else {
                pot_indir = 0;
                break;
            }
            /* Test if 3rd block is zero */
            if (*(cur_block + (cx2 + 2)) == 0) {
                zero = 1;
                /* Save 2nd in set of 4 */
                last_num = *(cur_block + (cx2 + 1));
                cx2 += 2;
                continue;
            }
            /* Test if 3rd block is 2nd + 1 */
            if (*(cur_block + (cx2 + 2)) == *(cur_block + (cx2 + 1)) + 1) {
                pot_indir = 1;
            } else {
                pot_indir = 0;
                break;
            }
            /* Test if 4th block is zero */
            if (*(cur_block + (cx2 + 3)) == 0) {
                zero = 1;
                /* Save 3rd in set of 4 */
                last_num = *(cur_block + (cx2 + 2));
                cx2 += 3;
                continue;
            }
            /* Test if 4th block is 3rd + 1 */
            if (*(cur_block + (cx2 + 3)) == *(cur_block + (cx2 + 2)) + 1) {
                pot_indir = 1;
                cx2 += 3;
            } else {
                pot_indir = 0;
                break;
            }
                /*
                pot_indir = 1;
                cur_num = *(cur_block + cx2);
                */
            /* Othrwise not an indirect block */
            /*
            else {
                pot_indir = 0;
                break;
            }
            */
        }
        /* Save the first number */
        if (pot_indir) {
            first_num = *cur_block;
        }

        /* If the block is filled all the way through, get the last entry */
        if (pot_indir && !zero && last_num == 0) {
            last_num = *(cur_block + (BYTES_PER_BLOCK / sizeof(*cur_block)) - 1);
        }

        /* Log potential indirect block */
        if (pot_indir) {
            printf("%-15lu%-12u%-10u\n", cx, first_num, last_num);
        }

        /* Log percentage through disk */
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
