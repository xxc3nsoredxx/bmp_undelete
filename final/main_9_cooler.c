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
#define SIN_IND             (12)
#define DBL_IND             (13)
#define TRI_IND             (14)
#define NOP()               do { \
                                (void)0; \
                            } while (0)

/* 0xC03B3998 in big endian */
const uint32_t JOURNAL_MAGIC            = 0x98393BC0;
/* 0x03 in big endian */
const uint32_t JOURNAL_SUPERBLOCK_TYPE1 = 0x03000000;
/* 0x04 in big endian */
const uint32_t JOURNAL_SUPERBLOCK_TYPE2 = 0x04000000;

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

struct inode_s *inode = 0;
uint32_t *ignores = 0;
size_t ign_count = 0;

void usage () {
    printf("Usage: ./indir [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* Cleanup tasks */
void cleanup (int exit_code) {
    if (ignores) {
        free(ignores);
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

/*
 * Tests if a block address is in the ignore list
 * Uses binary search based on the pseudocode found
 * on the Wikipedia page for binary search,
 * 2019-22-04 23:29:30 CST
 * ret 1 if in list, 0 otherwise
 */
int is_ignored (uint32_t addr){
    int l = 0;
    int r = ign_count - 1;
    int m;

    while (l <= r) {
        m = (l + r) / 2;
        if (*(ignores + m) < addr) {
            l = m + 1;
        } else if (*(ignores + m) > addr) {
            r = m - 1;
        } else {
            return 1;
        }
    }

    return 0;
}

int main (int argc, char **argv) {
    size_t cx;
    size_t cx2;
    size_t cx3;
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

    /* Get the group descriptor for group 0 */
    gd = (struct gd_s*)(dev + BYTES_PER_BLOCK);

    /* Go to start of inode table */
    inode = (struct inode_s*)(dev + (gd->bg_inode_table_lo * BYTES_PER_BLOCK));
    
    /*
     * Go through the first 11 inodes and ignore any indirect blocks
     * cx really means inode cx + 1 here due to the fact that there
     * is no inode 0
     */
    for (cx = 0; cx < 11; ) {
        uint32_t *blocks = (uint32_t*)inode->i_block;
        uint32_t *di = 0;
        uint32_t *ti = 0;
        struct journal_superblock_s *j;

        /* Ignore the journal (inode 8) */
        if (cx == 7) {
            j = (struct journal_superblock_s*)(dev + (*blocks * BYTES_PER_BLOCK));
            /* Verify the header magic */
            if (memcmp(&(j->s_header.h_magic), &JOURNAL_MAGIC, 4)) {
                printf("Journal magic mismatch\n");
                printf("In header: %08X\n", j->s_header.h_magic);
                printf("Real: %08X\n", JOURNAL_MAGIC);
            }
            /* Verify the type */
            if (memcmp(&(j->s_header.h_blocktype),
                &JOURNAL_SUPERBLOCK_TYPE1, 4) &&
                memcmp(&(j->s_header.h_blocktype),
                &JOURNAL_SUPERBLOCK_TYPE2, 4)) {
                printf("Journal type mismatch\n");
                printf("In header: %08X\n", j->s_header.h_blocktype);
                printf("Real v1: %08X\n", JOURNAL_SUPERBLOCK_TYPE1);
                printf("Real v2: %08X\n", JOURNAL_SUPERBLOCK_TYPE2);
            }
        }

        /* Log single indirect if used */
        if (*(blocks + SIN_IND) != 0) {
            ign_count++;
            ignores = realloc(ignores, ign_count * sizeof(*ignores));
            *(ignores + (ign_count - 1)) = *(blocks + SIN_IND);
        }

        /* Log double indirect if used */
        if (*(blocks + DBL_IND) != 0) {
            /* Increment by one to account for the double indirect in the inode */
            ign_count++;
            ignores = realloc(ignores, ign_count * sizeof(*ignores));
            *(ignores + (ign_count - 1)) = *(blocks + DBL_IND);

            /* Parse the double indirect block for any single indirects */
            di = (uint32_t*)(dev + (*(blocks + DBL_IND) * BYTES_PER_BLOCK));

            /* Test if the first entry is not 0 */
            if (*di != 0) {
                cx2 = 0;
            }
            /* Allow the first entry to be 0 for logging purposes */
            else if (*di == 0 && *(di + 1) != 0) {
                cx2 = 1;
            }
            /* If both are zero, skip any copying */
            else {
                goto skip_double;
            }
            for ( ; cx2 < BYTES_PER_BLOCK / sizeof(*di); cx2++) {
                /* Leave once a 0 has been found */
                if (*(di + cx2) == 0) {
                    break;
                }
            }
            /* Increment cx2 by 1 to get the count of blocks to add */
            cx2++;

            /* Copy the block addresses into the list */
            ignores = realloc(ignores, (ign_count + cx2) * sizeof(*ignores));
            memcpy(ignores + ign_count, di, cx2 * sizeof(*di));
            ign_count += cx2;
        skip_double:
            NOP();
        }

        /* Log triple indirect if used */
        if (*(blocks + TRI_IND) != 0) {
            /* Increment by one to account for the triple indirect in the inode */
            ign_count++;
            ignores = realloc(ignores, ign_count * sizeof(*ignores));
            *(ignores + (ign_count - 1)) = *(blocks + TRI_IND);

            /* Parse the triple indirect block for any double indirects */
            ti = (uint32_t*)(dev + (*(blocks + TRI_IND) * BYTES_PER_BLOCK));

            /* Test if the first entry is not 0 */
            if (*ti != 0) {
                cx3 = 0;
            }
            /* Allow the first entry to be 0 for logging purposes */
            else if (*ti == 0 && *(ti + 1) != 0) {
                cx3 = 1;
            }
            /* If both are zero, skip the loop */
            else {
                goto skip_triple;
            }
            for ( ; cx3 < BYTES_PER_BLOCK / sizeof(*ti); cx3++) {
                /* Leave once a 0 has been found */
                if (*(ti + cx3) == 0) {
                    break;
                }

                /*
                 * Parse each double indirect in the triple indirect for single indirects
                 * These addresses don't need to be copied over here because they are
                 * saved once the entire triple indirect is done parsing
                 */
                di = (uint32_t*)(dev + (*(ti + cx3) * BYTES_PER_BLOCK));

                /* Test if the first entry is not 0 */
                if (*di != 0) {
                    cx2 = 0;
                }
                /* Allow the first entry to be 0 for logging purposes */
                else if (*di == 0 && *(di + 1) != 0) {
                    cx2 = 1;
                }
                /* If both are zero, skip the loop */
                else {
                    goto skip_triple_double;
                }
                for ( ; cx2 < BYTES_PER_BLOCK / sizeof(*di); cx2++) {
                    /* Leave once a 0 has been found */
                    if (*(di + cx2) == 0) {
                        break;
                    }
                }
                /* Increment cx2 by 1 to get the count of blocks to add */
                cx2++;

                /* Copy the single indirect block addresses into the list */
                ignores = realloc(ignores, (ign_count + cx2) * sizeof(*ignores));
                memcpy(ignores + ign_count, di, cx2 * sizeof(*di));
                ign_count += cx2;
            skip_triple_double:
                NOP();
            }
            /* Increment cx3 by 1 to get the count of double indirect blocks to add */
            cx3++;

            /* Copy the block addresses into the list */
            ignores = realloc(ignores, (ign_count + cx3) * sizeof(*ignores));
            memcpy(ignores + ign_count, ti, cx3 * sizeof(*ti));
            ign_count += cx3;
        skip_triple:
            NOP();
        }

        /* Go to the next inode */
        cx++;
        inode = (struct inode_s*)(dev +
            /* Add the byte offset of the inode table */
            (gd->bg_inode_table_lo * BYTES_PER_BLOCK) +
            /* Add the byte offset for inode cx + 1 */
            (cx * sb->s_inode_size));
    }

    /*
     * Do an insertion sort on the list of ignores
     * This will allow binary search to be used when
     * testing if a block is to be ignored
     * Based on the pseudocode found on the Wikipedia
     * page for insertion sort, 2019-11-04 23:12:05 CST
     */
    cx = 1;
    while (cx < ign_count) {
        cx2 = cx;
        while (cx2 > 0 && *(ignores + cx2 - 1) > *(ignores + cx2)) {
            *(ignores + cx2 - 1) ^= *(ignores + cx2);
            *(ignores + cx2) ^= *(ignores + cx2 - 1);
            *(ignores + cx2 - 1) ^= *(ignores + cx2);
            cx2--;
        }
        cx++;
    }

    printf("Indirect block First block Last block\n");
    printf("=====================================\n");
    /* Parse each block of the drive and test for potential indirect block */
    for (cx = 0; cx < nblocks; cx++) {
        size_t cur_percent = cx * 100 / nblocks;
        uint32_t *cur_block = 0;
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

        /* If the current block is to be ignored, skip */
        if (is_ignored(cx)) {
            goto skip_ind_test;
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

    skip_ind_test:
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
