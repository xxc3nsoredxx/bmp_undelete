#ifndef RECOVER_H_20191111_183020
#define RECOVER_H_20191111_183020

#include <stdint.h>

struct fs_info_s {
    const uint32_t *nblocks;
    const uint32_t *ngroups;
    const size_t *ipg;
    const size_t *ipb;
};

extern struct fs_info_s fs_info;

/*
 * Status codes Meaning                                 Args
 * --------------------------------------------------------------------------
 * CLEANUP      started cleanup routine                 ---
 * GROUP_INFO   started group info collection           ---
 * GROUP_PROG   current group                           gnum(u32)
 * POP          started populating inode                inum(u32)
 * POP_DIR      populated dir blocks                    first(u32), last(u32)
 * POP_IND      populated ind block                     level(u32), bnum(u32)
 * LINK         started linking inode to root           inum(u32)
 * RECOVERED    file sucessfully linked                 name(char*)
 * SCAN         started drive scan                      ---
 * SCAN_IND     found potential ind block               level(u32), bnum(u32)
 * SCAN_BMP     found potential bmp header              bnum(u32)
 * SCAN_PROG    percentage through disk (1% interval)   percent(u32)
 * COLLECT      started collecting files                ---
 * SANITY       running sanity check                    bnum(u32)
 * INODE        inode reserved                          inum(u32)
 * DONE         operation complete                      ---
 * ERROR        fatal error                             format(char*), ...
 * WARN         warning                                 format(char*), ...
 */

enum status_code_e {
    /* Method start code followed by relevant progress codes */
    CLEANUP,
    GROUP_INFO, GROUP_PROG,
    POP,        POP_DIR,    POP_IND,
    LINK,       RECOVERED,
    SCAN,       SCAN_IND,   SCAN_BMP,   SCAN_PROG,
    COLLECT,    SANITY,     INODE,
    /* General method done code */
    DONE,
    /* Error codes */
    ERROR,      WARN
};

/* THIS MUST BE IMPLEMENTED ON THE CLIENT */
void status (enum status_code_e sl, ...);

/*
 * Private methods:
 * void cleanup ()
 * void get_group_info ()
 * void set_bmp_bit (uint8_t *bmp, uint32_t bit)
 * int is_block_used (uint32_t block)
 * uint32_t mark_used (uint32_t block, uint32_t ind)
 * uint32_t find_next_ind (uint32_t last, uint32_t ind)
 * uint32_t res_ino_helper (uint32_t inum)
 * uint32_t res_ino ()
 * int cmp_bmp (uint32_t block)
 * int cmp_ind (uint32_t block, uint32_t ind)
 * void populate (uint32_t inum)
 * void link (uint32_t inum)
 */

void init (const char *fname);
int scan ();
void collect ();

#endif /* RECOVER_H_20191111_183020 */
