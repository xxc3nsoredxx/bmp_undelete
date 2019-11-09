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
 * The file to link inode
 * test_land.bmp    81924
 */

#define BYTES_PER_BLOCK     (4 * 1024)
#define BLOCKS_PER_GROUP    (8 * BYTES_PER_BLOCK)
#define BYTES_PER_GROUP     (BLOCKS_PER_GROUP * BYTES_PER_BLOCK)
#define GROUP0_PAD          (1024)
#define ROOT_DIR            (2)
#define FILE_TYPE           (0x01)

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
unsigned int target_inum = 0;
char *target_name = 0;
unsigned int r_inum = 0;
unsigned int r_ipg = 0;
unsigned int r_igroup = 0;
unsigned int r_iindex = 0;
size_t r_ioff = 0;
unsigned int ngroups = 0;
struct sb_s *s = 0;
struct gd_s *g = 0;
struct inode_s *r_i = 0;
uint32_t r_bnums [12] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};
char *r_blocks [12] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};

void usage () {
    printf("Usage: ./link [device] [inode] [file name]\n");
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
void init (const char *fname, const char *i, const char *tname) {
    /* Attempt to open the device */
    devf = open(fname, O_RDWR);
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
    dev = mmap(0, dev_size, PROT_READ|PROT_WRITE, MAP_SHARED, devf, 0);
    if (dev == MAP_FAILED) {
        printf("Unable to mmap device: %s\n", fname);
        perror("mmap");
        cleanup(-1);
    }

    /* Get the target indode number */
    sscanf(i, " %u", &target_inum);

    /* Get the target file name */
    target_name = calloc (strlen(tname) + 1, 1);
    strcpy(target_name, tname);

    /* Set the inode number for the root directory */
    r_inum = ROOT_DIR;
}

int main (int argc, char **argv) {
    unsigned int cx;

    /* Test args */
    if (argc != 4) {
        usage();

        exit(-1);
    }

    /* Test if running as root */
    if (getuid()) {
        usage();

        exit(-1);
    }

    /* Initialize */
    init(*(argv + 1), *(argv + 2), *(argv + 3));

    /* Calculate the number of block groups */
    ngroups = dev_size / BYTES_PER_GROUP;

    /* Get the superblock */
    s = (struct sb_s*)(dev + GROUP0_PAD);

    /* Calculate the group number and offset of the root inode */
    r_ipg = s->s_inodes_per_group;
    r_igroup = (r_inum - 1) / r_ipg;
    r_iindex = (r_inum - 1) % r_ipg;
    r_ioff = r_iindex * s->s_inode_size;

    /* Get the group descriptor */
    g = (struct gd_s*)(dev + BYTES_PER_BLOCK + (r_igroup * sizeof(*g)));

    /* Get the root inode */
    r_i = (struct inode_s*)
        (dev + g->bg_inode_table_lo * BYTES_PER_BLOCK + r_ioff);

    /* Get the directory blocks */
    for (cx = 0; cx < 12; cx++) {
        memcpy(r_bnums + cx,
            r_i->i_block + cx * sizeof(*r_bnums),
            sizeof(*r_bnums));
    }
    for (cx = 0; cx < 12; cx++) {
        if (*(r_bnums + cx) == 0) {
            break;
        }

        /* Save pointers to the relevant blocks */
        *(r_blocks + cx) = (char*)(dev + (*(r_bnums + cx) * BYTES_PER_BLOCK));
    }

    /* Parse the directory listing */
    for (cx = 0; cx < 12; cx++) {
        struct dir_ent_s *de;
        int entered = 0;

        /* Stop once the referenced blocks have been parsed */
        if (*(r_blocks + cx) == 0) {
            break;
        }

        de = (struct dir_ent_s*)*(r_blocks + cx);
        for (;;) {
            uint16_t new_rec_len = sizeof(de->inode) +
                sizeof(de->rec_len) +
                sizeof(de->name_len) +
                sizeof(de->file_type) +
                strlen(target_name);
            uint16_t real_rec_len = sizeof(de->inode) +
                sizeof(de->rec_len) +
                sizeof(de->name_len) +
                sizeof(de->file_type) +
                de->name_len;

            /* Round up to nearest multiple of 4 */
            real_rec_len += 4 - (real_rec_len % 4);
            new_rec_len += 4 - (new_rec_len % 4);

            /* Fancy pointer math to detrmine if the current entry is the last one */
            if ((char*)de - *(r_blocks + cx) + de->rec_len == BYTES_PER_BLOCK) {
                /* Test if new directory entry would fit */
                if (de->rec_len - real_rec_len >= new_rec_len) {
                    /* Set new_rec_len to go to the end of the block */
                    new_rec_len = de->rec_len - real_rec_len;
                    /* Set the rec_len to go to the start of the new record */
                    de->rec_len = real_rec_len;

                    /* Fancy pointer math to get the next entry */
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

            /* Fancy pointer math to get the next entry */
            de = (struct dir_ent_s*)((char*)de + de->rec_len);
        }

        /* Done if directory entry entered */
        if (entered) {
            break;
        }
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
