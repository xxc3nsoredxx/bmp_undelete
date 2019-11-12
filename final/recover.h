#ifndef RECOVER_H_20191111_183020
#define RECOVER_H_20191111_183020

enum status_level_e {
    BAD, INFO, GOOD
};

/* THIS MUST BE IMPLEMENTED ON THE CLIENT */
void status (enum status_level_e sl, const char *fmt, ...);

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
 */

void init (const char *fname);
int scan ();
void collect ();

#endif /* RECOVER_H_20191111_183020 */
