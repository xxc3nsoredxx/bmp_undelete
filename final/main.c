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
#define BAD(S)  "" RED "[-] " S RESET
#define INFO(S) "" YELLOW "[!] " RESET S
#define GOOD(S) "" GREEN "[+] " S RESET

int devf = -1;
size_t dev_size;
uint8_t *dev = MAP_FAILED;
uint32_t nblocks;
uint32_t ngroups;
struct sb_s *sb = 0;
size_t ipg;
size_t ipb;
uint32_t *used_blocks = 0;
uint32_t n_used_blocks = 0;
uint32_t *bmp_starts = 0;
size_t n_bmp_starts = 0;
uint32_t *indirects [3] = {
    0, 0, 0
};
size_t n_indirects [3] = {
    0, 0, 0
};
struct inode_s *i = 0;

void usage () {
    printf("Usage: ./recover [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* 
 * Cleanup tasks
 */
void cleanup (int exit_code) {
    uint32_t cx;

    for (cx = 0; cx < 3; cx++) {
        if ((indirects + cx)) {
            free(*(indirects + cx));
        }
    }
    if (bmp_starts) {
        free(bmp_starts);
    }
    if (used_blocks) {
        free(used_blocks);
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
    devf = open(fname, O_RDWR);
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
    dev = mmap(0, dev_size, PROT_READ|PROT_WRITE, MAP_SHARED, devf, 0);
    if (dev == MAP_FAILED) {
        printf(BAD("Unable to mmap device: %s\n"), fname);
        cleanup(-1);
    }

    /* Calculate the number of blocks on the drive */
    nblocks = dev_size / BYTES_PER_BLOCK;

    /* Calculate the number of groups on the drive */
    ngroups = dev_size / BYTES_PER_GROUP;

    /* Get the superblock */
    sb = (struct sb_s*)(dev + SB_OFF);

    /* Get the number of inodes per group */
    ipg = sb->s_inodes_per_group;

    /* Calculate the number of inodes per block */
    ipb = BYTES_PER_BLOCK / sb->s_inode_size;
}

/*
 * Do an insertion sort on list of length len
 * Based on the pseudocode found on the Wikipedia page for insertion sort,
 * 2019-11-09 19:09:20 CST
 */
void sort (uint32_t *list, size_t len) {
    uint32_t cx;
    uint32_t cx2;

    for (cx = 1; cx < len; cx++) {
        for (cx2 = cx; cx2 > 0 && *(list + cx2 - 1) > *(list + cx2); cx2--) {
            *(list + cx2 - 1) ^= *(list + cx2);
            *(list + cx2) ^= *(list + cx2 - 1);
            *(list + cx2 - 1) ^= *(list + cx2);
        }
    }
}

/*
 * Do a binary search to test if list of length len contains block b
 * Based on the pseudocode found on the Wikipedia page for binary search,
 * 2019-11-09 19:18:35 CST
 * Return 1 if b in list, 0 otherwise
 */
int search (uint32_t *list, size_t len, uint32_t b) {
    int l;
    int r;
    int m;

    for (l = 0, r = len - 1; l <= r; ) {
        m = (l + r) / 2;
        if (*(list + m) < b) {
            l = m + 1;
        } else if (*(list + m) > b) {
            r = m - 1;
        } else {
            return 1;
        }
    }

    return 0;
}

/*
 * Go through every group's data block bitmap and log all used blocks
 */
void get_used_data () {
    uint32_t cx;
    uint32_t cx2;
    struct gd_s *gd;
    uint8_t *bitmap;

    /* Parse the drive group by group */
    printf(INFO("Parsing block bitmap for group:"));
    for (cx = 0; cx < ngroups; cx++) {
        printf(" %u", cx);

        /* Get the group descriptor */
        gd = (struct gd_s*)(dev + GD_OFF(cx));

        used_blocks = realloc(used_blocks,
            (n_used_blocks + BLOCKS_PER_GROUP - gd->bg_free_blocks_count_lo) *
                sizeof(*used_blocks));

        /* Get the block bitmap */
        bitmap = (uint8_t*)(dev + BLOCK_OFF(gd->bg_block_bitmap_lo));

        /* Go through the block bitmap */
        for (cx2 = 0; cx2 < BLOCKS_PER_GROUP; cx2++) {
            if (BMP_BIT(bitmap, cx2) == 1) {
                *(used_blocks + n_used_blocks) = (cx * BLOCKS_PER_GROUP) + cx2;
                n_used_blocks++;
            }
        }
    }
    printf("\n");
    printf(INFO("Done!\n"));
    printf(INFO("%u in-use blocks found\n\n"), n_used_blocks);
}

/*
 * Test if a block is a potential BMP start block
 * Return similar to memcmp(3)
 */
int cmp_bmp (uint32_t block) {
    struct bmp_head_s *header;

    /* Get the block as a BMP file header */
    header = (struct bmp_head_s*)(dev + BLOCK_OFF(block));

    return memcmp(&(header->bmp_magic), BMP_MAGIC, 2);
}

/*
 * Test if a block is a potential 1x indirect block
 * Return 0 if potential indirect, nonzero if not
 */
int cmp_ind_sin (uint32_t block) {
    int ret = 1;
    uint32_t cx;
    uint32_t cx2;
    uint32_t *blk;
    int zero = 0;

    /* Get the block as an array of block numbers */
    blk = (uint32_t*)(dev + BLOCK_OFF(block));

    /* Test for increment or until zeros */
    for (cx = 0; cx < BYTES_PER_BLOCK / sizeof(*blk); cx++) {
        /* Test for only zeros at the end */
        if (zero) {
            if (*(blk + cx) != 0) {
                ret = 1;
                break;
            }
            continue;
        }

        /* Test if listing ends on first of a set of 4 */
        if (cx % 4 == 0 && *(blk + cx) == 0) {
            /* If very first listed block, zero is invalid */
            /*
            if (cx == 0) {
                pot_indir = 0;
                break;
            }
            */

            /* Begin tracking zeros */
            zero = 1;
            continue;
        }

        /* Test if chunks of 4 are consecutive or zero found */
        for (cx2 = 1; cx2 < 4; cx2++) {
            /* Test if next is zero */
            if (*(blk + (cx + cx2)) == 0) {
                ret = 0;
                zero = 1;
                cx += cx2;
                break;
            }

            /* Test if next is cur + 1 */
            if (*(blk + (cx + cx2)) == *(blk + (cx + cx2 - 1)) + 1) {
                ret = 0;
            } else {
                ret = 1;
                break;
            }
        }
        if (zero && !ret) {
            continue;
        } else if (ret) {
            break;
        } else {
            /* No end cases found, continue to next group of 4 */
            cx += 3;
        }
    }

    return ret;
}

/*
 * Test if a block is a potential 2x indirect block
 * Return 0 if potential 2x indirect, nonzero if not
 */
int cmp_ind_dbl (uint32_t block) {
    int ret = 1;
    uint32_t cx;
    uint32_t *blk;

    /* Get the block as an array of block numbers */
    blk = (uint32_t*)(dev + BLOCK_OFF(block));

    /* Test if the block is a potential indirect block */
    ret = cmp_ind_sin(block);
    if (!ret) {
        /* Test if each listed block is an indirect block */
        for (cx = 0; !ret && cx < BYTES_PER_BLOCK / sizeof(*blk); cx++) {
            /* Skip if first listed block is zero */
            if (cx == 0 && *(blk + cx) == 0) {
                continue;
            }

            ret = ret || cmp_ind_sin(*(blk + cx));
        }
    }

    return ret;
}

/*
 * Test if a block is a potential 3x indirect block
 * Return 0 if potential 3x indirect, nonzero if not
 */
int cmp_ind_tri (uint32_t block) {
    int ret = 1;
    uint32_t cx;
    uint32_t *blk;

    /* Get the block as an array of block numbers */
    blk = (uint32_t*)(dev + BLOCK_OFF(block));

    /* Test if the block is a potential indirect block */
    ret = cmp_ind_sin(block);
    if (!ret) {
        /* Test if each listed block is a 2x indirect block */
        for (cx = 0; !ret && cx < BYTES_PER_BLOCK / sizeof(*blk); cx++) {
            /* Skip if first listed block is zero */
            if (cx == 0 && *(blk + cx) == 0) {
                continue;
            }

            ret = ret || cmp_ind_dbl(*(blk + cx));
        }
    }

    return ret;
}

/*
 * Sets the requested bit in the bitmap
 */
void set_bmp_bit (uint8_t *bmp, uint32_t bit) {
    uint32_t byte_off;
    uint32_t bit_off;
    uint8_t value;

    /* Calculate the byte and bit offsets */
    byte_off = bit / 8;
    bit_off = bit % 8;

    /* Set the specified bit */
    value = *(bmp + byte_off);
    value = value | (0x01 << bit_off);
    *(bmp + byte_off) = value;
}

/*
 * Reserves an inode for the recovered file
 * Returns 1 if success, 0 if fail
 */
int res_inode (uint32_t inum) {
    uint32_t igroup;
    uint32_t iindex;
    uint32_t ioff;
    struct gd_s *gd;
    uint8_t *bitmap;

    /* Calculate location of the inode on disk */
    igroup = (inum - 1) / ipg;
    iindex = (inum - 1) % ipg;
    ioff = iindex * sb->s_inode_size;

    /* Get the group descriptor */
    gd = (struct gd_s*)(dev + GD_OFF(igroup));

    /* Get the inode bitmap */
    bitmap = (uint8_t*)(dev + BLOCK_OFF(gd->bg_inode_bitmap_lo));

    /* Reserve the inode if it is free */
    if (BMP_BIT(bitmap, iindex) == 0) {
        set_bmp_bit(bitmap, iindex);
        i = (struct inode_s*)(dev + BLOCK_OFF(gd->bg_inode_table_lo) + ioff);
        return 1;
    }

    return 0;
}

int main (int argc, char **argv) {
    uint32_t cx;
    size_t percent = 0;
    size_t cur_percent;

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

    /* Get all the used data blocks */
    get_used_data();
    if (!used_blocks) {
        printf(BAD("Error getting used data blocks, exiting...\n"));
        cleanup(-1);
    }

    printf(INFO("Searching drive for important blocks...\n"));

    /* Parse the drive for important blocks */
    for (cx = 0; cx < nblocks; cx++) {
        cur_percent = cx * 100 / nblocks;

        /* Skip blocks marked used */
        if (search(used_blocks, n_used_blocks, cx)) {
            goto skip_tests;
        }
        /* Test for BMP header */
        else if (!cmp_bmp(cx)) {
            printf(GOOD("Found potential BMP header at block %u!\n"), cx);
            n_bmp_starts++;
            bmp_starts = realloc(bmp_starts,
                n_bmp_starts * sizeof(*bmp_starts));
            *(bmp_starts + (n_bmp_starts - 1)) = cx;
        }
        /* Test for 3x indirect block */
        else if (!cmp_ind_tri(cx)) {
            printf(GOOD("Found potential 3x indirect at block %u!\n"), cx);
            *(n_indirects + 2) += 1;
            *(indirects + 2) = realloc(*(indirects + 2),
                *(n_indirects + 2) * sizeof(**(indirects + 2)));
            *(*(indirects + 2) + *(n_indirects + 2) - 1) = cx;
        }
        /* Test for 2x indirect block */
        else if (!cmp_ind_dbl(cx)) {
            printf(GOOD("Found potential 2x indirect at block %u!\n"), cx);
            *(n_indirects + 1) += 1;
            *(indirects + 1) = realloc(*(indirects + 1),
                *(n_indirects + 1) * sizeof(**(indirects + 1)));
            *(*(indirects + 1) + *(n_indirects + 1) - 1) = cx;
        }
        /* Test for 1x indirect block */
        else if (!cmp_ind_sin(cx)) {
            printf(GOOD("Found potential 1x indirect at block %u!\n"), cx);
            *n_indirects += 1;
            *indirects = realloc(*indirects,
                *n_indirects * sizeof(**indirects));
            *(*indirects + *n_indirects - 1) = cx;
        }
    skip_tests:
        /* Log percentage through disk */
        if (cur_percent % 10 == 0 && cur_percent != percent) {
            percent += 10;
            printf(INFO("%lu%% complete...\n"), percent);
        }
    }
    printf(INFO("Done!\n\n"));

    /* Exit if no potential BMP starting blocks found */
    if (!bmp_starts) {
        printf(BAD("No potential BMP start blocks found, exiting...\n"));
        cleanup(-1);
    }

    /* Create entries for the files */
    for (cx = 0; cx < n_bmp_starts; cx++) {
        /* Try to reserve inode 6969 */
        if (!res_inode(6969)) {
            /* Try to reserve inode 666 */
            if (!res_inode(666)) {
                /* Try to reserve inode 420 */
                if (!res_inode(420)) {
                    printf(BAD("Unable to reserve an inode, exiting...\n"));
                    cleanup(-1);
                } else {
                    printf(GOOD("Reserved inode 420!\n"));
                }
            } else {
                printf(GOOD("Reserved inode 666!\n"));
            }
        } else {
            printf(GOOD("Reserved inode 6969!\n"));
        }
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
