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
struct gd_s **gd = 0;
uint8_t **block_bmps = 0;
uint8_t **inode_bmps = 0;
uint32_t *bmp_starts = 0;
size_t n_bmp_starts = 0;
uint32_t *indirects [3] = {
    0, 0, 0
};
size_t n_indirects [3] = {
    0, 0, 0
};
struct inode_s *i = 0;
char target_name [100];

void usage () {
    printf("Usage: ./recover [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* 
 * Cleanup tasks run on program exit
 */
void cleanup () {
    uint32_t cx;

    printf(INFO("Cleaning up junk\n"));

    for (cx = 0; cx < 3; cx++) {
        if ((indirects + cx)) {
            free(*(indirects + cx));
        }
    }
    if (bmp_starts) {
        free(bmp_starts);
    }
    if (inode_bmps) {
        free(inode_bmps);
    }
    if (block_bmps) {
        free(block_bmps);
    }
    if (gd) {
        free(gd);
    }
    if (dev != MAP_FAILED) {
        munmap(dev, dev_size);
    }
    if (devf >= 0) {
        close(devf);
    }
}

/* 
 * Initialization tasks
 */
void init (const char *fname) {
    /* Attempt to open the device */
    devf = open(fname, O_RDWR);
    if (devf < 0) {
        printf(BAD("Unable to open: %s\n"), fname);
        exit(-1);
    }
    
    /* Get size of device */
    if (ioctl(devf, BLKGETSIZE, &dev_size) == -1) {
        printf(BAD("Unable to get size of device: %s\n"), fname);
        exit(-1);
    }
    /* ioctl call gives number 512 byte sectors */
    dev_size *= 512;

    /* Attempt to mmap the device */
    dev = mmap(0, dev_size, PROT_READ|PROT_WRITE, MAP_SHARED, devf, 0);
    if (dev == MAP_FAILED) {
        printf(BAD("Unable to mmap device: %s\n"), fname);
        exit(-1);
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
 * Go through every group and save information about them
 */
void get_group_info () {
    uint32_t cx;

    /* Allocate space for the group descriptor and bitmap pointers */
    gd = calloc(ngroups, sizeof(*gd));
    block_bmps = calloc(ngroups, sizeof(*block_bmps));
    inode_bmps = calloc(ngroups, sizeof(*inode_bmps));

    /* Parse the drive group by group */
    printf(INFO("Saving information about group:"));
    for (cx = 0; cx < ngroups; cx++) {
        printf(" %u", cx);

        /* Get the group descriptor */
        *(gd + cx) = (struct gd_s*)(dev + GD_OFF(cx));

        /* Get the bitmaps */
        *(block_bmps + cx) = (uint8_t*)
            (dev + BLOCK_OFF((*(gd + cx))->bg_block_bitmap_lo));
        *(inode_bmps + cx) = (uint8_t*)
            (dev + BLOCK_OFF((*(gd + cx))->bg_inode_bitmap_lo));
    }
    printf("\n");
    printf(INFO("Done!\n\n"));
}

/*
 * Test if a block is used
 * Return 1 if used, 0 otherwise
 */
int is_block_used (uint32_t block) {
    uint32_t bgroup;
    uint32_t bindex;
    uint8_t *bmp;

    /* Test for out of bounds */
    if (block >= nblocks) {
        return 0;
    }

    bgroup = block / BLOCKS_PER_GROUP;
    bindex = block % BLOCKS_PER_GROUP;
    bmp = *(block_bmps + bgroup);

    return BMP_BIT(bmp, bindex);
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
 * Mark a block as used in the bitmap, handles indirects
 * Returns the last direct block that was marked
 */
uint32_t mark_used (uint32_t block, uint32_t ind) {
    uint32_t ret;
    uint32_t cx;
    uint32_t bgroup = block / BLOCKS_PER_GROUP;
    uint32_t bindex = block % BLOCKS_PER_GROUP;
    uint8_t *bmp = *(block_bmps + bgroup);
    uint32_t *blk;

    /* Mark the block, return if direct block */
    set_bmp_bit(bmp, bindex);
    if (ind == 0) {
        return block;
    }

    /* Handle indirects */
    blk = (uint32_t*)(dev + BLOCK_OFF(block));
    for (cx = 0; cx < BYTES_PER_BLOCK / sizeof(*blk); cx++) {
        /* Only mark non-zero linked blocks */
        if (*(blk + cx) != 0) {
            ret = mark_used(*(blk + cx), ind - 1);
        }
    }

    return ret;
}

/*
 * Actually does the attempt at reserving
 * Returns inode number if success, 0 if fail
 */
uint32_t res_ino_helper (uint32_t inum) {
    uint32_t igroup = (inum - 1) / ipg;
    uint32_t iindex = (inum - 1) % ipg;
    uint32_t ioff = iindex * sb->s_inode_size;
    uint8_t *bmp = *(inode_bmps + igroup);

    /* Reserve the inode if it is free */
    if (BMP_BIT(bmp, iindex) == 0) {
        set_bmp_bit(bmp, iindex);
        i = (struct inode_s*)(dev +
            BLOCK_OFF((*(gd + igroup))->bg_inode_table_lo) + ioff);
        return inum;
    }

    return 0;
}

/*
 * Reserves an inode for the recovered file
 * Priority on 6969, 666, 420, then first available
 * Returns inode number if success, 0 if fail
 */
uint32_t res_ino () {
    uint32_t ret;
    uint32_t cx;

    /* Attempt the priority inodes */
    ret = res_ino_helper(6969);
    if (!ret) {
        ret = res_ino_helper(666);
    }
    if (!ret) {
        ret = res_ino_helper(420);
    }

    /* Get the first available inode if the above fail */
    for (cx = sb->s_first_ino + 1; !ret && cx < sb->s_inodes_count; cx++) {
        ret = res_ino_helper(cx);
    }

    return ret;
}

/*
 * Test if a block is a potential BMP start block
 * Return similar to memcmp(3)
 */
int cmp_bmp (uint32_t block) {
    struct bmp_head_s *header;

    /* Test out of bounds */
    if (block >= nblocks) {
        return 1;
    }

    /* Get the block as a BMP file header */
    header = (struct bmp_head_s*)(dev + BLOCK_OFF(block));

    return memcmp(&(header->bmp_magic), BMP_MAGIC, 2);
}

/*
 * Tests if a block is a valid indirect block
 * Handles 1x, 2x, and 3x
 * Return 0 if potential indirect, nonzero if not
 */
int cmp_ind (uint32_t block, uint32_t ind) {
    int ret = 0;
    uint32_t cx;
    uint32_t cx2;
    uint32_t *blk;
    int zero = 0;

    /* Test out of bounds */
    if (block >= nblocks) {
        return 1;
    }

    /* Get the block as an array of block numbers */
    blk = (uint32_t*)(dev + BLOCK_OFF(block));

    /* Handle 1x indirect */
    if (ind == 0) {
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
                /* Begin tracking zeros unless very first entry */
                if (cx != 0) {
                    zero = 1;
                    continue;
                } else {
                    ret = 1;
                    break;
                }
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
                return ret;
            } else {
                /* No end cases found, continue to next group of 4 */
                cx += 3;
            }
        }
    }

    /* Handle 2x and 3x indirect */
    else if (ind == 1 || ind == 2) {
        /* Test each listed block with one level of indirection less */
        for (cx = 0; !ret && cx < BYTES_PER_BLOCK / sizeof(*blk); cx++) {
            /* Skip if first listed block is zero */
            if (cx == 0 && *blk == 0) {
                /* If first two are zero, invalid */
                if (*(blk + 1) == 0) {
                    return 1;
                }
                continue;
            }
            /* If nonzero in zero streak, invalid */
            else if (zero && *(blk + cx) != 0) {
                return 1;
            }
            /* Non-zero streak */
            else if (*(blk + cx) != 0) {
                ret = ret || cmp_ind(*(blk + cx), ind - 1);
            }
            /* Zero spotted */
            else {
                zero = 1;
                continue;
            }
        }
    }

    return ret;
}

/*
 * Scan the drive for all BMP header blocks and indirect blocks
 */
void scan () {
    uint32_t cx;
    int cx2;
    uint32_t percent;
    uint32_t cur_percent;

    /* Scan the drive for important blocks */
    printf(INFO("Scanning drive for important blocks...\n"));
    percent = 0;
    for (cx = 0; cx < nblocks; cx++) {
        cur_percent = cx * 100 / nblocks;

        /* Skip blocks marked used */
        if (is_block_used(cx)) {
            goto skip_tests;
        }
        /* Test for indirect block */
        /* Start at 3x, go down to 1x */
        for (cx2 = 2; cx2 >= 0; cx2--) {
            if (!cmp_ind(cx, cx2)) {
                printf(GOOD("Found potential %ux indirect at block %u!\n"),
                    cx2 + 1, cx);
                *(n_indirects + cx2) += 1;
                *(indirects + cx2) = realloc(*(indirects + cx2),
                    *(n_indirects + cx2) * sizeof(**(indirects + cx2)));
                *(*(indirects + cx2) + *(n_indirects + cx2) - 1) = cx;
                goto skip_tests;
            }
        }
        /* Test for BMP header */
        if (!cmp_bmp(cx)) {
            printf(GOOD("Found potential BMP header at block %u!\n"), cx);
            n_bmp_starts++;
            bmp_starts = realloc(bmp_starts,
                n_bmp_starts * sizeof(*bmp_starts));
            *(bmp_starts + (n_bmp_starts - 1)) = cx;
        }
    skip_tests:
        /* Log percentage through disk */
        if (cur_percent % 10 == 0 && cur_percent != percent) {
            percent += 10;
            printf(INFO("%u%% complete...\n"), percent);
        }
    }
    printf(INFO("Done!\n\n"));
}

/*
 * Finds the indirect block that goes on from the last block
 * Return the block number if found, zero otherwise
 */
uint32_t find_next_ind (uint32_t last, uint32_t ind) {
    uint32_t cx;
    uint32_t bnum;
    uint32_t *blk = 0;

    /* Loop through all the indirects of a given type */
    for (cx = 0; cx < *(n_indirects + ind); cx++) {
        bnum = *(*(indirects + ind) + cx);

        /* Skip used blocks */
        if (!is_block_used(bnum)) {
            blk = (uint32_t*)(dev + BLOCK_OFF(bnum));

            /* 1x indirect, test if it starts after last block */
            if (ind == 0 && *blk == last + 1) {
                return bnum;
            }
            /* 2x and 3x indirect, test first listed block */
            else if (ind == 1 || ind == 2) {
                /* Ignore first listed if zero */
                if (*blk == 0) {
                    blk++;
                }
                if (find_next_ind(last, ind - 1) == *blk) {
                    return bnum;
                }
            }
        }
    }

    return 0;
}

/*
 * Links the given inode to the root directory
 * file_num is the running count of recovered files
 */
void link (uint32_t file_num, uint32_t target_inum) {
    struct inode_s *root = 0;
    uint32_t root_bnum = 0;
    uint8_t *root_block = 0;
    uint16_t new_rec_len;
    uint16_t real_rec_len;
    struct dir_ent_s *de = 0;
    int entered = 0;

    /* Get the root inode information */
    root = (struct inode_s*)(dev +
        BLOCK_OFF((*gd)->bg_inode_table_lo) +
        ((ROOT_INODE - 1) * sb->s_inode_size));
    root_bnum = *((uint32_t*)(root->i_block));
    root_block = (uint8_t*)(dev + BLOCK_OFF(root_bnum));

    /* Link to root */
    printf(INFO("Linking inode %u to root directory...\n"), target_inum);
    memset(target_name, 0, sizeof(target_name));
    sprintf(target_name, "recovered_%03u.bmp", file_num);
    de = (struct dir_ent_s*)root_block;
    for (;;) {
        new_rec_len = sizeof(de->inode) +
            sizeof(de->rec_len) +
            sizeof(de->name_len) +
            sizeof(de->file_type) +
            strlen(target_name);
        real_rec_len = sizeof(de->inode) +
            sizeof(de->rec_len) +
            sizeof(de->name_len) +
            sizeof(de->file_type) +
            de->name_len;

        /* Round up to nearest multiple of 4 */
        real_rec_len += 4 - (real_rec_len % 4);
        new_rec_len += 4 - (new_rec_len % 4);

        /* Calculate if the current entry is the last entry */
        if ((uint8_t*)de - root_block + de->rec_len == BYTES_PER_BLOCK) {
            /* Test if entry fits */
            if (de->rec_len - real_rec_len >= new_rec_len) {
                /* Set new_rec_len to go to the end of the block */
                new_rec_len = de->rec_len - real_rec_len;
                /* Set the rec_len to go to the start of the new record */
                de->rec_len = real_rec_len;

                /* Get the next entry */
                de = (struct dir_ent_s*)((char*)de + de->rec_len);
                /* Build the directory entry */
                de->inode = target_inum;
                de->rec_len = new_rec_len;
                de->name_len = strlen(target_name);
                strncpy(de->name, target_name, de->name_len);

                entered = 1;
                break;
            } else {
                break;
            }
        }

        /* Get the next entry */
        de = (struct dir_ent_s*)((char*)de + de->rec_len);
    }
    if (entered) {
        printf(INFO("Done!\n"));
        printf(GOOD("File name: %s\n\n"), target_name);
    } else {
        printf(BAD("Failed to link, exiting...\n"));
        exit(-1);
    }
}

int main (int argc, char **argv) {
    uint32_t cx;
    uint32_t cx2;

    /* Register the exit handler */
    if (atexit(cleanup)) {
        printf(BAD("Unable to register the exit handler!\n"));
        exit(-1);
    }

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

    /* Get information about each group */
    get_group_info();
    if (!gd || !block_bmps || !inode_bmps) {
        printf(BAD("Error getting group information, exiting...\n"));
        exit(-1);
    }

    /* Scan the drive */
    scan();
    if (!bmp_starts) {
        printf(BAD("No potential BMP start blocks found, exiting...\n"));
        exit(-1);
    }

    /* Create entries for the files found */
    for (cx = 0; cx < n_bmp_starts; cx++) {
        struct bmp_head_s *bmp_head = (struct bmp_head_s*)(dev +
            BLOCK_OFF(*(bmp_starts + cx)));
        uint32_t size = bmp_head->bmp_file_size;
        uint32_t size_blocks = size / BYTES_PER_BLOCK;
        uint32_t inum;
        uint32_t *iblocks;
        uint32_t bnum;
        uint32_t last;

        /* Skip used blocks */
        if (is_block_used(*(bmp_starts + cx))) {
            continue;
        }

        /* Ensure that overflow is accounted for */
        size_blocks += (size % BYTES_PER_BLOCK) ? 1 : 0;

        /* Try to reserve an inode*/
        if (!(inum = res_ino())) {
            printf(BAD("Unable to reserve an inode, exiting...\n"));
            exit(-1);
        }
        printf(GOOD("Reserved inode %u!\n"), inum);

        /* Populate the inode with required fields */
        printf(INFO("Populating inode %u...\n"), inum);
        i->i_mode = MODE_777 | TYPE_REG;
        i->i_size_lo = size;
        i->i_links_count = 1;
        iblocks = (uint32_t*)(i->i_block);
        /* Populate direct blocks */
        for (cx2 = 0; cx2 < size_blocks && cx2 < 12; cx2++) {
            bnum = *(bmp_starts + cx) + cx2;
            printf(GOOD("Direct %u: %u\n"), cx2, bnum);
            *(iblocks + cx2) = bnum;
            last = mark_used(bnum, 0);
        }
        /* Populate indirect blocks */
        for (cx2 = 0; cx2 < 3; cx2++) {
            if (*(n_indirects + cx2) > 0) {
                /* Find the indirect block that has the next block */
                bnum = find_next_ind(last, cx2);
                if (bnum != 0) {
                    *(iblocks + SIN_IND + cx2) = bnum;
                    printf(GOOD("%ux Indirect: %u\n"), cx2 + 1, bnum);
                    last = mark_used(bnum, cx2 + 1);
                    continue;
                }
            }
        }
        i->i_extra_isize = 32;
        printf(INFO("Done!\n\n"));

        /* Link the inode to the root directory */
        link(cx, inum);
    }

    exit(0);
}
