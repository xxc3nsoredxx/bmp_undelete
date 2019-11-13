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

struct win_s op;
struct win_s cmds;
struct win_s err;
struct win_s err_shadow;
struct win_s blk_dev;
struct win_s blk_dev_shadow;

int drive_selected = 0;
int drive_scanned = 0;
int files_rebuilt = 0;

char **block_devices_str = 0;
chtype **block_devices = 0;
int n_block_devices = 0;
int block_dev_choice = -1;

/*
 * Turns a char* into a chtype*
 * Returns malloc'ed address if created, 0 otherwise
 */
chtype* strchtype (const char *src, size_t len) {
    chtype *ret = 0;
    size_t cx;

    /* Do nothing for zero length input string */
    if (len > 0) {
        ret = calloc(len + 1, sizeof(*ret));

        for (cx = 0; cx < len; cx++) {
            *(ret + cx) = *(src + cx);
        }
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

/*
 * Generate a popup for ERROR and WARN
 */
void create_error (enum status_code_e s, const char *fmt, va_list ap) {
    char *title;
    char message_str [100];
    chtype *message;
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
    err.title = strchtype(title, err.title_len);

    /* Convert the message into it's string form */
    message_len = sizeof(message_str) / sizeof(*message_str);
    memset(message_str, 0, message_len);
    message_len = vsprintf(message_str, fmt, ap);
    message = strchtype(message_str, message_len);

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

    getch();
}

/* 
 * Recieve the broadcasted status
 */
void status (enum status_code_e sl, ...) {
    va_list ap;

    va_start(ap, sl);

    switch (sl) {
    /* Handle methods */
    case CLEANUP:
        break;

    case GROUP_INFO:
        break;
    case GROUP_PROG:
        break;

    case POP:
        break;
    case POP_DIR:
        break;
    case POP_IND:
        break;

    case LINK:
        break;
    case RECOVERED:
        break;

    case SCAN:
        break;
    case SCAN_IND:
        break;
    case SCAN_BMP:
        break;
    case SCAN_PROG:
        break;

    case COLLECT:
        break;
    case SANITY:
        break;
    case INODE:
        break;

    case DONE:
        break;

    /* Handle error codes */
    case ERROR:
        create_error(sl, va_arg(ap, const char*), ap);
        break;
    case WARN:
        create_error(sl, va_arg(ap, const char*), ap);
        break;
    }

    va_end(ap);
}

/*
 * Popup to choose a block device
 */
void block_dev_popup () {
    const char *title = "Block Device List";
    const char *inst1_str = "[Up/Down]: Choose";
    chtype *inst1 = strchtype(inst1_str, strlen(inst1_str));
    const char *inst2_str = "[Enter]: Confirm";
    chtype *inst2 = strchtype(inst2_str, strlen(inst2_str));
    const char *inst3_str = "[Q]: Cancel";
    chtype *inst3 = strchtype(inst3_str, strlen(inst3_str));
    int cx;
    size_t longest_name = 0;
    int x;
    int y;
    unsigned int width;
    int height;
    attr_t attr = A_REVERSE;

    blk_dev.title_len = strlen(title);
    blk_dev.title = strchtype(title, blk_dev.title_len);

    for (cx = 0; cx < n_block_devices; cx++) {
        /* Convert the names into chtype* */
        *(block_devices + cx) = strchtype(*(block_devices_str + cx),
            strlen(*(block_devices_str + cx)));

        /* Get the longest device name */
        longest_name = (longest_name > strlen(*(block_devices_str + cx)))
            ? longest_name
            : strlen(*(block_devices_str + cx));
    }

    /* Center horizontally */
    width = longest_name + 6;
    width = (COLS / 3 > width) ? COLS / 3 : width;
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
    wnoutrefresh(blk_dev.win);
    doupdate();

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
            free(*(block_devices_str + cx));
        }
        free(block_devices_str);
    }
    if (block_devices) {
        for (cx = 0; cx < n_block_devices; cx++) {
            free(*(block_devices + cx));
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
                malloc(strlen(dname) + strlen(de->d_name));
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
    w->title = strchtype(title, w->title_len);
    w->text_w = width - 2;
    w->text_h = height - 2;
    move_to(w, 1, 1);

    if (w->text_h > 1) {
        wsetscrreg(w->win, 1, w->text_h + 1);
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
    drive_prompt = strchtype(drive_prompt_str, strlen(drive_prompt_str));
    no_drive_msg = strchtype(no_drive_msg_str, strlen(no_drive_msg_str));
    f01 = strchtype(f01_str, strlen(f01_str));
    f03 = strchtype(f03_str, strlen(f03_str));
    f05 = strchtype(f05_str, strlen(f05_str));
    f07 = strchtype(f07_str, strlen(f07_str));
    f09 = strchtype(f09_str, strlen(f09_str));
    f11 = strchtype(f11_str, strlen(f11_str));

    /* Prepare drive stats */
    move_to(&cmds, 1, 1);
    waddchstr(cmds.win, drive_prompt);
    move_to(&cmds, cmds.cur_x + strlen(drive_prompt_str), cmds.cur_y);
    /* Actual drive stats if drive is selected */
    if (drive_selected) {
        sprintf(drive_stats_str,
            "%s    %u blocks * %u B/block = %u MiB",
            fs_info.name, *(fs_info.nblocks), BYTES_PER_BLOCK,
            (*(fs_info.nblocks) >> 10) * (BYTES_PER_BLOCK >> 10));
        drive_stats = strchtype(drive_stats_str, strlen(drive_stats_str));
        waddchstr(cmds.win, drive_stats);
    } else {
        waddchstr(cmds.win, no_drive_msg);
    }
    /* Show the command help */
    inc = cmds.text_w / 6;
    move_to(&cmds, 1, 2);
    waddchstr(cmds.win, f01);
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f03);
    /* Disable if no drive selected */
    if (!drive_selected) {
        wchgat(cmds.win, strlen(f03_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f05);
    /* Disable if drive not scanned */
    if (!drive_scanned) {
        wchgat(cmds.win, strlen(f05_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f07);
    /* Disable if drive not scanned */
    if (!drive_scanned) {
        wchgat(cmds.win, strlen(f07_str), A_UNDERLINE, COLOR_ERROR, NULL);
    } else {
        wchgat(cmds.win, strlen(f07_str), A_NORMAL, 0, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f09);
    /* Disable if files not rebuilt */
    if (!drive_scanned) {
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
        if (!drive_selected) {
            status(ERROR, "No drive selected!");
        } else {
            status(WARN, "Drive scanning not implemented.");
        }
        return 1;
    /* Scan Results */
    case KEY_F(5):
        /* Error if no drive not scanned */
        if (!drive_scanned) {
            status(ERROR, "No drive scanned!");
        } else {
            status(WARN, "Drive scanning not implemented.");
        }
        return 1;
    /* Rebuild Files */
    case KEY_F(7):
        /* Error if no drive not scanned */
        if (!drive_scanned) {
            status(ERROR, "No drive scanned!");
        } else {
            status(WARN, "File recovery not implemented.");
        }
        return 1;
    /* List Files */
    case KEY_F(9):
        /* Error if no rebuilt files */
        if (!files_rebuilt) {
            status(ERROR, "No files have been rebuilt yet!");
        } else {
            status(WARN, "File recovery not implemented.");
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

    do {
        if (fs_info.name) {
            drive_selected = 1;
        }
        /* Prep all the windows */
        wnoutrefresh(stdscr);
        prep_op();
        prep_cmds();
        doupdate();
    } while (parse_input(getch()));

    exit(0);
}
