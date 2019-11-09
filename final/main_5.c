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

int devf = -1;
size_t dev_size;
char *dev = MAP_FAILED;
unsigned int inum = 0;
unsigned int ipg = 0;
unsigned int igroup = 0;
unsigned int iindex = 0;
size_t ioff = 0;
unsigned int ngroups = 0;
struct sb_s *s = 0;
struct gd_s *g = 0;
struct inode_s *i = 0;
uint32_t bnums [12] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};
char *blocks [12] = {
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0
};

void usage () {
    printf("Usage: ./dir [device] [dir inode]\n");
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
void init (const char *fname, const char *i) {
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

    /* Get the indode number */
    sscanf(i, " %u", &inum);
}

int main (int argc, char **argv) {
    unsigned int cx;

    /* Test args */
    if (argc != 3) {
        usage();

        exit(-1);
    }

    /* Test if running as root */
    if (getuid()) {
        usage();

        exit(-1);
    }

    /* Initialize */
    init(*(argv + 1), *(argv + 2));

    /* Calculate the number of block groups */
    ngroups = dev_size / BYTES_PER_GROUP;

    /* Get the superblock */
    s = (struct sb_s*)(dev + GROUP0_PAD);

    /* Calculate the group number and offset of the given inode */
    ipg = s->s_inodes_per_group;
    igroup = (inum - 1) / ipg;
    iindex = (inum - 1) % ipg;
    ioff = iindex * s->s_inode_size;
    printf("Inodes per group: %u\n", ipg);
    printf("Group of inode %u: %u\n", inum, igroup);
    printf("Index of inode %u: %u\n", inum, iindex);
    printf("Offset of inode %u: %lu\n", inum, ioff);

    /* Get the group descriptor */
    g = (struct gd_s*)(dev + BYTES_PER_BLOCK + (igroup * sizeof(*g)));
    printf("Location of inode table: Block %u\n", g->bg_inode_table_lo);

    /* Get the requested inode */
    i = (struct inode_s*)(dev + g->bg_inode_table_lo * BYTES_PER_BLOCK + ioff);

    /* Test if the inode is a directory */
    printf("Directory: ");
    if (i->i_mode & 0x4000) {
        printf("YES\n");
    } else {
        printf("NO\n");
        cleanup(-1);
    }

    /* Get the directory blocks */
    for (cx = 0; cx < 12; cx++) {
        memcpy(bnums + cx, i->i_block + cx * sizeof(*bnums), sizeof(*bnums));
    }
    printf("Blocks used:\n");
    for (cx = 0; cx < 12; cx++) {
        if (*(bnums + cx) == 0) {
            break;
        }

        printf(" %u", *(bnums + cx));
        if (cx % 4 == 3 && cx != 11) {
            printf("\n");
        }
    }
    printf("\n");
    for (cx = 0; cx < 12; cx++) {
        if (*(bnums + cx) == 0) {
            break;
        }

        /* Save pointers to the relevant blocks */
        *(blocks + cx) = (char*)(dev + (*(bnums + cx) * BYTES_PER_BLOCK));
    }

    /* Print the directory listing */
    printf("Directory listing:\n");
    printf(" Inode     Name\n");
    printf(" -----     ----\n");
    for (cx = 0; cx < 12; cx++) {
        struct dir_ent_s *de;

        /* Stop once the referenced blocks have been parsed */
        if (*(blocks + cx) == 0) {
            break;
        }

        de = (struct dir_ent_s*)*(blocks + cx);
        for (;;) {
            if (de->inode != 0) {
                printf(" %-9u %s\n", de->inode, de->name);
            }
            /* Fancy pointer math to detrmine if the current entry is the last one */
            if ((char*)de - *(blocks + cx) + de->rec_len == BYTES_PER_BLOCK) {
                break;
            }
            /* Fancy pointer math to get the next entry */
            de = (struct dir_ent_s*)((char*)de + de->rec_len);
        }
    }

    /* Cleanup and exit */
    cleanup(0);

    /* Never actually gets here */
    return 0;
}
