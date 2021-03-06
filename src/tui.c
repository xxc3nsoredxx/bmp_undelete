#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curses.h>

#include "ext.h"
#include "recover.h"

#define CURS_HID    0
#define CURS_VIS    1

#define STR_LEN(S)  S, strlen(S)

#define BOX_TL      ACS_ULCORNER
#define BOX_TR      ACS_URCORNER
#define BOX_BL      ACS_LLCORNER
#define BOX_BR      ACS_LRCORNER
#define BOX_HOR     ACS_HLINE
#define BOX_VER     ACS_VLINE
#define SHADOW      ACS_CKBOARD

#define COLOR_ERROR 1
#define COLOR_WARN  2
#define COLOR_PROG  3
#define COLOR_GOOD  4

#define DT_BLK      6

struct win_s {
    WINDOW *win;
    chtype *title;
    int title_len;
    int text_w;
    int text_h;
    int cur_x;
    int cur_y;
};

struct prog_win_s {
    WINDOW *win;
    chtype *title;
    int title_len;
    chtype *scan_msg;
    int scan_msg_len;
    int text_w;
    int text_h;
    int prog_x;
    int prog_y;
    int prog_h;
    int bar_w;
    int percent;
    int segment_inc;
    chtype *percent_prog;
    chtype *prog_bar;
    chtype *bar_perim_top;
    chtype *bar_perim_bot;
    int cur_x;
    int cur_y;
};

struct pot_block_s {
    uint32_t *blocks;
    uint32_t count;
};

struct file_info_s {
    char *name;
    uint32_t inum;
    uint32_t first_dir;
    uint32_t last_dir;
    uint32_t indirs [3];
};

struct win_s op;
struct win_s cmds;
struct win_s err;
struct win_s err_shadow;
struct win_s blk_dev;
struct win_s blk_dev_shadow;
struct prog_win_s prog;
struct prog_win_s prog_shadow;

struct pot_block_s pots [4] = {
    { (uint32_t*)0, 0 },
    { (uint32_t*)0, 0 },
    { (uint32_t*)0, 0 },
    { (uint32_t*)0, 0 }
};

/*
 * 0: not started
 * 1: ongoing
 * 2: finished
 */
int drive_selected = 0;
int drive_scanned = 0;
int files_rebuilt = 0;

char **block_devices_str = 0;
chtype **block_devices = 0;
int n_block_devices = 0;
int block_dev_choice = -1;
int file_count = 0;
struct file_info_s *files = 0;

/*
 * Turns a char* into a chtype*
 * Returns malloc'ed address if created, 0 otherwise
 */
chtype* strchtype (chtype *dest, const char *src, size_t len) {
    chtype *ret = 0;
    size_t cx;

    /* Do nothing for zero length input string */
    if (len > 0) {
        ret = calloc(len + 1, sizeof(*ret));

        for (cx = 0; cx < len && cx < strlen(src); cx++) {
            *(ret + cx) = *(src + cx);
        }
    }

    if (dest) {
        free(dest);
    }

    return ret;
}

/*
 * Sets the cursor's coordinates
 */
void move_to(struct win_s *w, int x, int y) {
    w->cur_x = x;
    w->cur_y = y;
    wmove(w->win, w->cur_y, w->cur_x);
}

void close_win (WINDOW *w) {
    wclear(w);
    wnoutrefresh(w);
    delwin(w);
}

/*
 * Generate a popup for ERROR and WARN
 */
void create_error (enum status_code_e s, const char *fmt, va_list ap) {
    char *title;
    char message_str [100];
    chtype *message = 0;
    int message_len;
    int x;
    int y;
    int width;
    int height;
    attr_t attr = A_REVERSE;
    short pair = 0;

    if (s == ERROR) {
        title = "ERROR!";
        attr = attr | A_UNDERLINE;
        pair = COLOR_ERROR;
    } else {
        title = "Warning!";
        pair = COLOR_WARN;
    }

    err.title_len = strlen(title);
    err.title = strchtype(err.title, title, err.title_len);

    /* Convert the message into it's string form */
    message_len = sizeof(message_str) / sizeof(*message_str);
    memset(message_str, 0, message_len);
    message_len = vsprintf(message_str, fmt, ap);
    message = strchtype(message, message_str, message_len);

    /* Center the popup horizontally */
    width = message_len + 6;
    width = (COLS / 3 > width) ? COLS / 3 : width;
    err.text_w = width - 2;
    x = (COLS - width) / 2;

    /* Center the popup vertically */
    height = LINES / 3;
    err.text_h = height - 2;
    y = height;

    /* Setup shadow */
    err_shadow.win = newwin(height, width, y, x + 1);
    wborder(err_shadow.win,
        SHADOW, SHADOW, SHADOW, SHADOW,
        SHADOW, SHADOW, SHADOW, SHADOW);

    /* Set up popup */
    err.win = newwin(height, width, y - 1, x);
    move_to(&err, (err.text_w - message_len) / 2, err.text_h / 3);
    waddchstr(err.win, message);
    /* Set up border */
    wborder(err.win,
        /* Left, right, top, bottom sides */
        BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
        BOX_TL, BOX_TR, BOX_BL, BOX_BR);
    move_to(&err, 1, 0);
    waddchstr(err.win, err.title);
    wbkgd(err.win, COLOR_PAIR(pair) | A_REVERSE);
    wchgat(err.win, err.title_len, attr, pair, NULL);

    wnoutrefresh(err_shadow.win);
    wnoutrefresh(err.win);
    doupdate();
}

/*
 * Update the progress display
 */
void update_progress (int new_p) {
    char percent_prog_str [6];
    /* Draw the shadow */
    wborder(prog_shadow.win,
        SHADOW, SHADOW, SHADOW, SHADOW,
        SHADOW, SHADOW, SHADOW, SHADOW);

    /* Draw the progress window */
    prog.cur_x = prog.prog_x;
    prog.cur_y = prog.prog_y;
    /* Write the message above the progress bar */
    wmove(prog.win, prog.cur_y, prog.cur_x);
    waddchstr(prog.win, prog.scan_msg);
    /* Test if the percent has gone up by the segment threshold */
    if (new_p >= prog.percent + prog.segment_inc) {
        prog.percent = new_p;
        prog.bar_w += 1;
        /* Update the increment threshold */
        prog.segment_inc = (100 - new_p) /
            (prog.text_w - (2 + 6 + 3 + prog.bar_w));
    }
    /* Create the percent string */
    sprintf(percent_prog_str, "% 3d%% ", new_p);
    prog.percent_prog = strchtype(prog.percent_prog, percent_prog_str, 5);
    prog.cur_y += 4;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    waddchstr(prog.win, prog.percent_prog);
    /* Create the border */
    wborder(prog.win,
        /* Left, right, top, bottom sides */
        BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
        BOX_TL, BOX_TR, BOX_BL, BOX_BR);
    /* Create the title */
    wmove(prog.win, 0, 1);
    waddchstr(prog.win, prog.title);

    /* Create the enclosure for the progress bar */
    /* Go to the top left corner */
    prog.cur_y -= 2;
    prog.cur_x += 5;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    /* Draw left line */
    wvline(prog.win, BOX_VER, 5);
    /* Draw top line */
    waddchstr(prog.win, prog.bar_perim_top);

    /* Go to the top right corner, down one */
    prog.cur_x = prog.text_w - 2;
    prog.cur_y += 1;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    /* Draw the right line */
    wvline(prog.win, BOX_VER, 4);

    /* Go to the bottom left corner */
    prog.cur_y += 3;
    prog.cur_x = 8;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    /* Draw the bottom line */
    waddchstr(prog.win, prog.bar_perim_bot);

    wbkgd(prog.win, COLOR_PAIR(COLOR_PROG));

    /* Create the progress bar itself */
    prog.cur_y -= 3;
    prog.cur_x += 1;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    wchgat(prog.win, prog.bar_w, A_REVERSE, COLOR_PROG, NULL);
    prog.cur_y += 1;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    wchgat(prog.win, prog.bar_w, A_REVERSE, COLOR_PROG, NULL);
    prog.cur_y += 1;
    wmove(prog.win, prog.cur_y, prog.cur_x);
    wchgat(prog.win, prog.bar_w, A_REVERSE, COLOR_PROG, NULL);

    wnoutrefresh(prog_shadow.win);
    wnoutrefresh(prog.win);
}

/*
 * Set up a progress window for the drive scan
 */
void setup_scan_progress () {
    const char *title = "Drive Scan";
    const char *scan_msg_p1 = "Scan of ";
    const char *scan_msg_p2 = " in progress...";
    char *scan_msg_str = calloc(strlen(scan_msg_p1) + strlen(scan_msg_p2) +
        strlen(fs_info.name) + 1, sizeof(*scan_msg_str));
    char *bar_perim_top_str;
    char *bar_perim_bot_str;
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    unsigned int lpad = 2;
    unsigned int percent_len = 5;
    unsigned int rpad = 2;
    unsigned int bar_len;

    if (!scan_msg_str) {
        status(ERROR, "Calloc failed on scan_msg_str");
        exit(-1);
    }
    
    /* Calculate the dimensions of the window */
    /*
     * +-------------------+ border top:   1 row
     * |                   | padding:      1 row
     * |  message          | message:      1 row
     * |                   | padding:      2 rows
     * |       +-      -+  |
     * |       |progress|  | progress:     3 rows
     * |  NNN% |progress|  | indicator
     * |       |progress|  |
     * |       +-      -+  | padding:      1 row
     * +-------------------+ border:       1 row
     * width: 19
     * text_w: 17
     * minus pad: 13
     * minus % string: 8
     */
    width = 3 * COLS / 5;
    height = 10;
    x = (COLS - width) / 2;
    y = (LINES - height) / 2;

    prog.win = newwin(height, width, y - 1, x);
    prog.text_w = width - 2;
    prog.text_h = height - 2;
    prog.prog_x = 3;
    prog.prog_y = 2;
    prog.prog_h = 6;
    prog.bar_w = 0;
    prog.percent = 0;
    prog.segment_inc = 100 / (prog.text_w - (2 + 6 + 3));
    bar_len = prog.text_w - (lpad + percent_len + rpad);
    bar_perim_top_str = calloc(bar_len + 1, sizeof(*bar_perim_top_str));
    memset(bar_perim_top_str, ' ', bar_len);
    prog.bar_perim_top = strchtype(prog.bar_perim_top,
        bar_perim_top_str, bar_len);
    *(prog.bar_perim_top + 0) = BOX_TL;
    *(prog.bar_perim_top + 1) = BOX_HOR;
    *(prog.bar_perim_top + bar_len - 2) = BOX_HOR;
    *(prog.bar_perim_top + bar_len - 1) = BOX_TR;

    bar_perim_bot_str = calloc(bar_len + 1, sizeof(*bar_perim_bot_str));
    memset(bar_perim_bot_str, ' ', bar_len);
    prog.bar_perim_bot = strchtype(prog.bar_perim_bot,
        bar_perim_bot_str, bar_len);
    *(prog.bar_perim_bot + 0) = BOX_BL;
    *(prog.bar_perim_bot + 1) = BOX_HOR;
    *(prog.bar_perim_bot + bar_len - 2) = BOX_HOR;
    *(prog.bar_perim_bot + bar_len - 1) = BOX_BR;

    prog.title_len = strlen(title);
    prog.title = strchtype(prog.title, title, prog.title_len);
    /* Build the message that goes abve the progress bar */
    prog.scan_msg_len = sprintf(scan_msg_str, "%s%s%s",
        scan_msg_p1, fs_info.name, scan_msg_p2);
    prog.scan_msg = strchtype(prog.scan_msg, scan_msg_str, prog.scan_msg_len);
    prog.percent_prog = strchtype(prog.percent_prog, "  0% ", 5);

    /* Set up the shadow */
    prog_shadow.win = newwin(height, width, y, x + 1);

    free(bar_perim_bot_str);
    free(bar_perim_top_str);
    free(scan_msg_str);

    update_progress(prog.percent);
}

/*
 * Log the findings on the output window
 */
void log_potential_blocks () {
    int y;
    int x;

    getyx(op.win, y, x);
    /* Print the statistics so far */
    mvwprintw(op.win, y + 1, 1,
        "Number of potential 3x indirect blocks found: %u",
        (pots + 3)->count);
    mvwprintw(op.win, y + 2, 1,
        "Number of potential 2x indirect blocks found: %u",
        (pots + 2)->count);
    mvwprintw(op.win, y + 3, 1,
        "Number of potential 1x indirect blocks found: %u",
        (pots + 1)->count);
    mvwprintw(op.win, y + 4, 1,
        "Number of potential BMP blocks found: %u",
        (pots + 0)->count);
    wmove(op.win, y, x);

    /* Schedule it to be shown */
    wnoutrefresh(op.win);
}

/*
 * Display a nice, columnized scan results page
 */
void display_scan_results () {
    uint32_t cx;
    uint32_t cx2;

    werase(op.win);

    /* List potential BMP header blocks */
    wmove(op.win, 1, 1);
    wprintw(op.win, "Potential BMP Headers");
    for (cx = 0; cx < pots->count; cx++) {
        wmove(op.win, cx + 2, 1);
        wprintw(op.win, "%u", *(pots->blocks + cx));
    }

    /* List potential indirect blocks */
    for (cx = 1; cx <= 3; cx++) {
        wmove(op.win, 1, op.text_w * cx / 4);
        wprintw(op.win, "Potential %ux Indirects", cx);

        for (cx2 = 0; cx2 < (pots + cx)->count; cx2++) {
            wmove(op.win, cx2 + 2, op.text_w * cx / 4);
            wprintw(op.win, "%u", *((pots + cx)->blocks + cx2));
        }
    }
}

/*
 * Show the reults of the file recovery
 */
void display_recovery_results () {
    int cx;
    int cx2;
    int y;
    int x;

    werase(op.win);
    y = 1;
    x = 1;

    for (cx = 0; cx < file_count; cx++) {
        /* Wrap if next entry doesn't fit */
        if (y + 7 > op.text_h) {
            y = 1;
            x += op.text_w / 3;
        }

        mvwprintw(op.win, y, x, "File: %s", (files + cx)->name);
        y++;
        x += 2;

        mvwprintw(op.win, y, x, "Inode: %u", (files + cx)->inum);
        y++;

        mvwprintw(op.win, y, x, "Directs:");
        getyx(op.win, y, x);
        for (cx2 = 0;
            (files + cx)->first_dir + cx2 <= (files + cx)->last_dir;
            cx2++) {
            wprintw(op.win, " %-9u", (files + cx)->first_dir + cx2);
            if (cx2 % 4 == 3) {
                y++;
                wmove(op.win, y, x);
            }
        }
        if (cx2 % 4 != 0) {
            y++;
        }
        x -= 8;

        for (cx2 = 0; cx2 < 3; cx2++) {
            mvwprintw(op.win, y, x, "%dx indirect: ", cx2 + 1);
            if (*((files + cx)->indirs + cx2)) {
                wprintw(op.win, "%u", *((files + cx)->indirs + cx2));
            } else {
                wprintw(op.win, "---");
            }
            y++;
        }

        y++;
        x -= 2;
    }
}

/* 
 * Recieve the broadcasted status
 */
void status (enum status_code_e sl, ...) {
    va_list ap;
    int y;
    int x;
    int var1;
    uint32_t var2;
    uint32_t var3;
    char *var4;

    switch (sl) {
    /* Handle methods */
    case CLEANUP:
        break;

    case GROUP_INFO:
        drive_selected = 1;
        werase(op.win);
        mvwprintw(op.win, 1, 1,
            "Gathering basic information about groups:");
        wnoutrefresh(op.win);
        break;
    case GROUP_PROG:
        va_start(ap, sl);
        var2 = va_arg(ap, uint32_t);
        va_end(ap);
        wprintw(op.win, " %u", var2);
        wnoutrefresh(op.win);
        break;

    case POP:
        va_start(ap, sl);
        /* Extract the inode number */
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "Populating inde %u", var2);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;
    case POP_DIR:
        va_start(ap, sl);
        /* Extract the first/last block number */
        var2 = va_arg(ap, uint32_t);
        var3 = va_arg(ap, uint32_t);
        va_end(ap);

        /* Put the direct blocks into the proper entry in the array */
        (files + file_count - 1)->first_dir = var2;
        (files + file_count - 1)->last_dir = var3;

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "  Direct blocks: %u -> %u", var2, var3);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;
    case POP_IND:
        va_start(ap, sl);
        /* Extract the indirect level and block number */
        var2 = va_arg(ap, uint32_t);
        var3 = va_arg(ap, uint32_t);
        va_end(ap);

        /* Put the indirect block into the proper entry in the array */
        *((files + file_count - 1)->indirs + var2 - 1) = var3;

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "  %ux indirect block: %u", var2, var3);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;

    case LINK:
        va_start(ap, sl);
        /* Extract the inode number */
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "Linking inde %u to root directory", var2);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;
    case RECOVERED:
        va_start(ap, sl);
        /* Extract the file name */
        var4 = va_arg(ap, char*);
        va_end(ap);

        /* Put the file name into the proper entry in the array */
        (files + file_count - 1)->name =
            calloc(strlen(var4) + 1,
                sizeof(*((files + file_count - 1)->name)));
        strcpy((files + file_count - 1)->name, var4);

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "Recovered into file %s", var4);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;

    case SCAN:
        drive_scanned = 1;
        setup_scan_progress();
        log_potential_blocks();
        wnoutrefresh(prog_shadow.win);
        wnoutrefresh(prog.win);
        break;
    case SCAN_IND:
        va_start(ap, sl);
        /* Extract the indirect level and block number */
        var1 = va_arg(ap, int);
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        /* Track the newcomer */
        (pots + var1)->count += 1;
        (pots + var1)->blocks = realloc((pots + var1)->blocks,
            (pots + var1)->count * sizeof(*(pots + var1)->blocks));
        *((pots + var1)->blocks + (pots + var1)->count - 1) = var2;

        log_potential_blocks();
        wnoutrefresh(prog_shadow.win);
        wnoutrefresh(prog.win);
        break;
    case SCAN_BMP:
        va_start(ap, sl);
        /* Extract the block number */
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        /* Track the newcomer */
        pots->count += 1;
        pots->blocks = realloc(pots->blocks,
            pots->count * sizeof(*pots->blocks));
        *(pots->blocks + pots->count - 1) = var2;

        log_potential_blocks();
        wnoutrefresh(prog_shadow.win);
        wnoutrefresh(prog.win);
        break;
    case SCAN_PROG:
        va_start(ap, sl);
        var2 = va_arg(ap, uint32_t);
        va_end(ap);
        update_progress(var2);
        break;

    case COLLECT:
        files_rebuilt = 1;
        werase(op.win);
        /* Set up border */
        wborder(op.win,
            /* Left, right, top, bottom sides */
            BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
            BOX_TL, BOX_TR, BOX_BL, BOX_BR);
        
        /* Draw title */
        mvwaddchstr(op.win, 0, 1, op.title);
        mvwprintw(op.win, 1, 1, "Beginning file recovery...");
        wmove(op.win, 2, 1);
        wnoutrefresh(op.win);
        break;
    case SANITY:
        va_start(ap, sl);
        /* Extract the block number */
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "Running sanity check on block %u", var2);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;
    case INODE:
        va_start(ap, sl);
        /* Extract the inode number */
        var2 = va_arg(ap, uint32_t);
        va_end(ap);

        /* Add an entry into the file array */
        file_count++;
        files = realloc(files, file_count * sizeof(*files));
        (files + file_count - 1)->inum = var2;
        /* Ensure the indirs are set to 0 */
        memset((files + file_count - 1)->indirs,
            0,
            3 * sizeof(*((files + file_count - 1)->indirs)));

        getyx(op.win, y, x);
        if (y > op.text_h) {
            y--;
            scroll(op.win);
            wmove(op.win, y, x);
        }
        wprintw(op.win, "Reserved inde %u", var2);
        y++;
        wmove(op.win, y, x);
        wnoutrefresh(op.win);
        break;

    case DONE:
        if (drive_selected == 1) {
            drive_selected = 2;
            close_win(blk_dev.win);
            close_win(blk_dev_shadow.win);
        } else if (drive_scanned == 1) {
            drive_scanned = 2;
            close_win(prog.win);
            close_win(prog_shadow.win);
        } else if (files_rebuilt == 1) {
            files_rebuilt = 2;
        }
        break;

    /* Handle error codes */
    case ERROR:
        va_start(ap, sl);
        create_error(sl, va_arg(ap, const char*), ap);
        va_end(ap);
        getch();
        close_win(err.win);
        close_win(err_shadow.win);
        break;
    case WARN:
        va_start(ap, sl);
        create_error(sl, va_arg(ap, const char*), ap);
        va_end(ap);
        getch();
        close_win(err.win);
        close_win(err_shadow.win);
        break;
    }

    doupdate();
}

/*
 * Popup to choose a block device
 */
void block_dev_popup () {
    const char *title = "Block Device List";
    const char *inst1_str = "[Up/Down]: Choose";
    chtype *inst1 = 0;
    const char *inst2_str = "[Enter]: Confirm";
    chtype *inst2 = 0;
    const char *inst3_str = "[Q]: Cancel";
    chtype *inst3 = 0;
    int cx;
    size_t longest_name = 0;
    int x;
    int y;
    unsigned int width;
    int height;
    attr_t attr = A_REVERSE;

    inst1 = strchtype(inst1, inst1_str, strlen(inst1_str));
    inst2 = strchtype(inst2, inst2_str, strlen(inst2_str));
    inst3 = strchtype(inst3, inst3_str, strlen(inst3_str));

    blk_dev.title_len = strlen(title);
    blk_dev.title = strchtype(blk_dev.title, title, blk_dev.title_len);

    for (cx = 0; cx < n_block_devices; cx++) {
        /* Convert the names into chtype* */
        *(block_devices + cx) = strchtype(*(block_devices + cx),
            *(block_devices_str + cx),
            strlen(*(block_devices_str + cx)));

        /* Get the longest device name */
        longest_name = (longest_name > strlen(*(block_devices_str + cx)))
            ? longest_name
            : strlen(*(block_devices_str + cx));
    }

    /* Center horizontally */
    width = longest_name + 6;
    width = ((unsigned)(COLS / 3) > (unsigned)width)
        ? (unsigned)(COLS / 3)
        : (unsigned)width;
    width = (strlen(inst1_str) + strlen(inst2_str) + strlen(inst3_str)
        > width)
            ? strlen(inst1_str) + strlen(inst2_str) + strlen(inst3_str)
            : width;
    blk_dev.text_w = width - 2;
    x = (COLS - width) / 2;

    /* Center vertically */
    height = LINES / 3;
    height = (n_block_devices + 4 > height) ? n_block_devices + 4 : height;
    blk_dev.text_h = height - 2;
    y = (LINES - height) / 2;

    /* Setup shadow */
    blk_dev_shadow.win = newwin(height, width, y, x + 1);
    wborder(blk_dev_shadow.win,
        SHADOW, SHADOW, SHADOW, SHADOW,
        SHADOW, SHADOW, SHADOW, SHADOW);

    /* Setup popup */
    blk_dev.win = newwin(height, width, y - 1, x);
    /* Reuse x and y as the top left of the listing */
    x = (blk_dev.text_w - longest_name) / 2;
    y = (blk_dev.text_h - n_block_devices) / 2 + 1;
    /* Print the listing */
    for (cx = 0; cx < n_block_devices; cx++) {
        move_to(&blk_dev, x, y + cx);
        waddchstr(blk_dev.win, *(block_devices + cx));
    }
    /* Print instructions */
    move_to(&blk_dev, 1, y + cx);
    waddchstr(blk_dev.win, inst1);
    move_to(&blk_dev,
        strlen(inst1_str) +
            (((blk_dev.text_w - (strlen(inst1_str) + strlen(inst3_str))) -
            strlen(inst2_str)) / 2),
        y + cx);
    waddchstr(blk_dev.win, inst2);
    move_to(&blk_dev, blk_dev.text_w - strlen(inst3_str), y + cx);
    waddchstr(blk_dev.win, inst3);
    /* Set up border */
    wborder(blk_dev.win,
        /* Left, right, top, bottom sides */
        BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
        BOX_TL, BOX_TR, BOX_BL, BOX_BR);
    move_to(&blk_dev, 1, 0);
    waddchstr(blk_dev.win, blk_dev.title);
    wbkgd(blk_dev.win, attr);

    wnoutrefresh(blk_dev_shadow.win);

    /* Selection */
    for(cx = 0; ; ) {
        wchgat(blk_dev.win, longest_name, attr, 0, NULL);
        move_to(&blk_dev, x, y + cx);
        wchgat(blk_dev.win, longest_name, A_NORMAL, 0, NULL);
        wnoutrefresh(blk_dev.win);
        doupdate();

        switch(getch()) {
        case KEY_DOWN:
            cx++;
            cx = (cx >= n_block_devices) ? n_block_devices - 1 : cx;
            break;
        case KEY_UP:
            cx--;
            cx = (cx < 0) ? 0 : cx;
            break;
        case '\n':
            goto select;
        case 'Q':
        case 'q':
            return;
        default:
            break;
        }
    }
select:
    /* Initialize the recovery with the selected device */
    init(*(block_devices_str + cx));
}

/*
 * Find all the block devices under /dev
 */
void find_block_devs () {
    int cx;
    const char *dname = "/dev/";
    DIR *dir;
    struct dirent *de;

    /* Clear the array */
    if (block_devices_str) {
        for (cx = 0; cx < n_block_devices; cx++) {
            if (*(block_devices_str + cx)) {
                free(*(block_devices_str + cx));
            }
        }
        free(block_devices_str);
    }
    if (block_devices) {
        for (cx = 0; cx < n_block_devices; cx++) {
            if (*(block_devices + cx)) {
                free(*(block_devices + cx));
            }
        }
        free(block_devices);
    }
    n_block_devices = 0;
    block_devices_str = 0;
    block_devices = 0;
    block_dev_choice = -1;

    /* Open a stream to /dev */
    dir = opendir(dname);
    if (!dir) {
        status(ERROR, "Unable to open directory: %s", dname);
        return;
    }

    /* Go through each entry */
    for (de = readdir(dir); de; de = readdir(dir)) {
        /* Test if it is a block device */
        if (de->d_type == DT_BLK) {
            n_block_devices++;
            block_devices_str = realloc(block_devices_str,
                n_block_devices * sizeof(*block_devices_str));
            *(block_devices_str + n_block_devices - 1) =
                calloc(strlen(dname) + strlen(de->d_name) + 1,
                sizeof(**(block_devices_str + n_block_devices - 1)));
            strcpy(*(block_devices_str + n_block_devices - 1), dname);
            strcpy((*(block_devices_str + n_block_devices - 1) +
                strlen(dname)), de->d_name);
        }
    }
    block_devices = calloc(n_block_devices, sizeof(*block_devices));

    block_dev_popup();

    closedir(dir);
}

/*
 * Cleanup tasks for ncurses run on program exit
 */
void tui_cleanup () {
    int cx;

    if (files) {
        for (cx = 0; cx < file_count; cx++) {
            if ((files + cx)->name) {
                free((files + cx)->name);
            }
        }
        free(files);
    }
    for (cx = 0; cx < 4; cx++) {
        if ((pots + cx)->blocks) {
            free((pots + cx)->blocks);
        }
    }
    if (prog_shadow.win) {
        delwin(prog_shadow.win);
    }
    if (prog.bar_perim_bot) {
        free(prog.bar_perim_bot);
    }
    if (prog.bar_perim_top) {
        free(prog.bar_perim_top);
    }
    if (prog.prog_bar) {
        free(prog.prog_bar);
    }
    if (prog.scan_msg) {
        free(prog.scan_msg);
    }
    if (prog.title) {
        free(prog.title);
    }
    if (prog.win) {
        delwin(prog.win);
    }
    if (block_devices) {
        for (cx = 0; cx < n_block_devices; cx++) {
            free(*(block_devices + cx));
        }
        free(block_devices);
    }
    if (block_devices_str) {
        for (cx = 0; cx < n_block_devices; cx++) {
            free(*(block_devices_str + cx));
        }
        free(block_devices_str);
    }
    if (blk_dev_shadow.win) {
        delwin(blk_dev_shadow.win);
    }
    if (blk_dev.title) {
        free(blk_dev.title);
    }
    if (blk_dev.win) {
        delwin(blk_dev.win);
    }
    if (err_shadow.win) {
        delwin(err_shadow.win);
    }
    if (err.title) {
        free(err.title);
    }
    if (err.win) {
        delwin(err.win);
    }
    if (cmds.title) {
        free(cmds.title);
    }
    if (cmds.win) {
        delwin(cmds.win);
    }
    if (op.title) {
        free(op.title);
    }
    if (op.win) {
        delwin(op.win);
    }
    curs_set(CURS_VIS);
    endwin();
}

/*
 * Builds a window with the given parameters
 */
void build_win (struct win_s *w, const char *title, int x, int y,
    int width, int height) {
    w->win = newwin(height, width, y, x);
    w->title_len = strlen(title);
    w->title = strchtype(w->title, title, w->title_len);
    w->text_w = width - 2;
    w->text_h = height - 2;
    move_to(w, 1, 1);

    if (w->text_h > 1) {
        wsetscrreg(w->win, 1, w->text_h);
        scrollok(w->win, TRUE);
    } else {
        scrollok(w->win, FALSE);
    }
}

/*
 * Initialization tasks for ncurses
 */
void tui_init () {
    /* Register the exit handler */
    if (atexit(tui_cleanup)) {
        status(ERROR, "Unable to register the exit handler!\n");
        exit(-1);
    }

    /* Init ncurses */
    if (!initscr()) {
        status(ERROR, "Unable to start ncurses!\n");
        exit(-1);
    }
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(CURS_HID);
    start_color();

    init_pair(COLOR_ERROR, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_WARN, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_PROG, COLOR_BLUE, COLOR_WHITE);
    init_pair(COLOR_GOOD, COLOR_GREEN, COLOR_BLACK);

    /* Create the main output window */
    build_win(&op, "Output", 0, 0, COLS, LINES - 4);
    if (!op.win) {
        status(ERROR, "Unable to create output window!\n");
        exit(-1);
    }

    /* Create the command window */
    build_win(&cmds, "Commands", 0, LINES - 4, COLS, 4);
    if (!cmds.win) {
        status(ERROR, "Unable to create command window!\n");
        exit(-1);
    }
}

/* 
 * Prepare the components of the window that they all share
 */
void prep_win (struct win_s w) {
    /* Set up border */
    wborder(w.win,
        /* Left, right, top, bottom sides */
        BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
        BOX_TL, BOX_TR, BOX_BL, BOX_BR);
    
    /* Draw title */
    mvwaddchstr(w.win, 0, 1, w.title);
    wmove(w.win, w.cur_y, w.cur_x);
}

/*
 * Prepare op specific things
 */
void prep_op () {
    prep_win(op);

    wnoutrefresh(op.win);
}

/*
 * Prepare cmds specific things
 */
void prep_cmds () {
    int inc = 0;
    const char *drive_prompt_str = "Drive: ";
    chtype *drive_prompt = 0;
    const char *no_drive_msg_str = "[no drive selected]";
    chtype *no_drive_msg = 0;
    char drive_stats_str [100];
    chtype *drive_stats = 0;
    const char *f01_str = "F1: Select Drive";
    chtype *f01 = 0;
    const char *f03_str = "F3: Scan Drive";
    chtype *f03 = 0;
    const char *f05_str = "F5: Scan Results";
    chtype *f05 = 0;
    const char *f07_str = "F7: Rebuild Files";
    chtype *f07 = 0;
    const char *f09_str = "F9: List Files";
    chtype *f09 = 0;
    const char *f11_str = "F11: Quit";
    chtype *f11 = 0;

    prep_win(cmds);

    /* Create the command window's related strings */
    drive_prompt = strchtype(drive_prompt,
        drive_prompt_str, strlen(drive_prompt_str));
    no_drive_msg = strchtype(no_drive_msg,
        no_drive_msg_str, strlen(no_drive_msg_str));
    f01 = strchtype(f01, f01_str, strlen(f01_str));
    f03 = strchtype(f03, f03_str, strlen(f03_str));
    f05 = strchtype(f05, f05_str, strlen(f05_str));
    f07 = strchtype(f07, f07_str, strlen(f07_str));
    f09 = strchtype(f09, f09_str, strlen(f09_str));
    f11 = strchtype(f11, f11_str, strlen(f11_str));

    /* Prepare drive stats */
    move_to(&cmds, 1, 1);
    waddchstr(cmds.win, drive_prompt);
    move_to(&cmds, cmds.cur_x + strlen(drive_prompt_str), cmds.cur_y);
    /* Actual drive stats if drive is selected */
    if (drive_selected == 2) {
        sprintf(drive_stats_str,
            "%s    %u blocks * %u B/block = %u MiB",
            fs_info.name, *(fs_info.nblocks), BYTES_PER_BLOCK,
            (*(fs_info.nblocks) >> 10) * (BYTES_PER_BLOCK >> 10));
        drive_stats = strchtype(drive_stats,
            drive_stats_str, strlen(drive_stats_str));
        waddchstr(cmds.win, drive_stats);
    } else {
        waddchstr(cmds.win, no_drive_msg);
    }
    /* Show the command help */
    inc = cmds.text_w / 6;
    move_to(&cmds, 1, 2);
    waddchstr(cmds.win, f01);
    /* Mark as done after drive selected */
    if (drive_selected == 2) {
        wchgat(cmds.win, strlen(f01_str), A_NORMAL, COLOR_GOOD, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f03);
    /* Disable if no drive selected */
    if (drive_selected < 2) {
        wchgat(cmds.win, strlen(f03_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    /* Mark as done after drive scanned */
    else if (drive_scanned == 2) {
        wchgat(cmds.win, strlen(f03_str), A_NORMAL, COLOR_GOOD, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f05);
    /* Disable if drive not scanned */
    if (drive_scanned < 2) {
        wchgat(cmds.win, strlen(f05_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f07);
    /* Disable if drive not scanned */
    if (drive_scanned < 2) {
        wchgat(cmds.win, strlen(f07_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    /* Mark as done after files recovered */
    else if (files_rebuilt == 2) {
        wchgat(cmds.win, strlen(f07_str), A_NORMAL, COLOR_GOOD, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f09);
    /* Disable if files not rebuilt */
    if (files_rebuilt < 2) {
        wchgat(cmds.win, strlen(f09_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f11);

    if (f11) {
        free(f11);
    }
    if (f09) {
        free(f09);
    }
    if (f07) {
        free(f07);
    }
    if (f05) {
        free(f05);
    }
    if (f03) {
        free(f03);
    }
    if (f01) {
        free(f01);
    }
    if (drive_stats) {
        free(drive_stats);
    }
    if (no_drive_msg) {
        free(no_drive_msg);
    }
    if (drive_prompt) {
        free(drive_prompt);
    }

    wnoutrefresh(cmds.win);
}

/*
 * Parse each command
 * Return 1 if continue, 0 if quit
 */
int parse_input (int key) {
    switch (key) {
    /* Select Drive */
    case KEY_F(1):
        find_block_devs();
        return 1;
    /* Scan Drive */
    case KEY_F(3):
        /* Error if no drive selected */
        if (drive_selected == 0) {
            status(ERROR, "No drive selected!");
        } else if (drive_scanned == 2) {
            status(WARN, "Drive %s already scanned.", fs_info.name);
        } else {
            scan();
        }
        return 1;
    /* Scan Results */
    case KEY_F(5):
        /* Error if no drive not scanned */
        if (drive_scanned == 0) {
            status(ERROR, "No drive scanned!");
        } else {
            display_scan_results();
        }
        return 1;
    /* Rebuild Files */
    case KEY_F(7):
        /* Error if no drive not scanned */
        if (drive_scanned == 0) {
            status(ERROR, "No drive scanned!");
        } else if (files_rebuilt == 2) {
            status(WARN, "Files already rebuilt.");
        } else {
            collect();
        }
        return 1;
    /* List Files */
    case KEY_F(9):
        /* Error if no rebuilt files */
        if (files_rebuilt == 0) {
            status(ERROR, "No files have been rebuilt yet!");
        } else {
            display_recovery_results();
        }
        return 1;
    /* Quit */
    case KEY_F(11):
        return 0;
    default:
        return 1;
    }
}

int main () {
    /* Test if running as root */
    if (getuid()) {
        status(ERROR, "Requires root permissions to run!\n");
        exit(-1);
    }

    /* ncurses related initialization */
    tui_init();

    /* Prep all the windows */
    wnoutrefresh(stdscr);
    do {
        prep_op();
        prep_cmds();
        doupdate();
    } while (parse_input(getch()));

    exit(0);
}
