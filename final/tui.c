#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curses.h>

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

struct win_s {
    WINDOW *win;
    chtype *title;
    int title_len;
    chtype *message;
    int message_len;
    int text_w;
    int text_h;
    int cur_x;
    int cur_y;
};

struct win_s op;
struct win_s cmds;
struct win_s pu;
struct win_s pu_shadow;

int drive_selected = 0;
int drive_scanned = 0;
int files_rebuilt = 0;

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
 * Generate a popup for ERROR, WARN, and SCAN
 */
void create_popup (enum status_code_e s, const char *fmt, va_list ap) {
    char *title;
    char message [100];
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
    } else if (s == WARN) {
        title = "Warning!";
    } else {
        title = "Scanning Drive...";
    }

    pu.title_len = strlen(title);
    pu.title = strchtype(title, pu.title_len);

    /* Convert the message into it's string form */
    pu.message_len = sizeof(message) / sizeof(*message);
    memset(message, 0, pu.message_len);
    pu.message_len = vsprintf(message, fmt, ap);
    pu.message = strchtype(message, pu.message_len);

    /* Center the popup horizontally */
    width = pu.message_len + 6;
    width = (COLS / 3 > width) ? COLS / 3 : width;
    pu.text_w = width - 2;
    x = (COLS - width) / 2;

    /* Center the popup vertically */
    height = LINES / 3;
    pu.text_h = height - 2;
    y = height;

    /* Setup shadow */
    pu_shadow.win = newwin(height, width, y, x + 1);
    wborder(pu_shadow.win,
        SHADOW, SHADOW, SHADOW, SHADOW,
        SHADOW, SHADOW, SHADOW, SHADOW);

    /* Set up popup */
    pu.win = newwin(height, width, y - 1, x);
    move_to(&pu, (pu.text_w - pu.message_len) / 2, pu.text_h / 3);
    waddchstr(pu.win, pu.message);
    /* Set up border */
    wborder(pu.win,
        /* Left, right, top, bottom sides */
        BOX_VER, BOX_VER, BOX_HOR, BOX_HOR,
        BOX_TL, BOX_TR, BOX_BL, BOX_BR);
    move_to(&pu, 1, 0);
    waddchstr(pu.win, pu.title);
    wbkgd(pu.win, COLOR_PAIR(pair) | A_REVERSE);
    wchgat(pu.win, pu.title_len, attr, pair, NULL);

    wnoutrefresh(pu_shadow.win);
    wnoutrefresh(pu.win);
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
        create_popup(sl, va_arg(ap, const char*), ap);
        break;
    case WARN:
        break;
    }

    va_end(ap);
}

/*
 * Cleanup tasks for ncurses run on program exit
 */
void tui_cleanup () {
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
    const char *drive_prompt_str = "Drive: ";
    chtype *drive_prompt = 0;
    const char *no_drive_msg_str = "[no drive selected]";
    chtype *no_drive_msg = 0;
    int inc = 0;
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
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f09);
    /* Disable if files not rebuilt */
    if (!drive_scanned) {
        wchgat(cmds.win, strlen(f09_str), A_UNDERLINE, COLOR_ERROR, NULL);
    }
    move_to(&cmds, cmds.cur_x + inc, cmds.cur_y);
    waddchstr(cmds.win, f11);

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
        return 1;
    /* Scan Drive */
    case KEY_F(3):
        /* Error if no drive selected */
        if (!drive_selected) {
            status(ERROR, "No drive selected!");
        } else {
        }
        return 1;
    /* Scan Results */
    case KEY_F(5):
        /* Error if no drive not scanned */
        if (!drive_selected) {
            status(ERROR, "No drive scanned!");
        } else {
        }
        return 1;
    /* Rebuild Files */
    case KEY_F(7):
        /* Error if no drive not scanned */
        if (!drive_selected) {
            status(ERROR, "No drive scanned!");
        } else {
        }
        return 1;
    /* List Files */
    case KEY_F(9):
        /* Error if no rebuilt files */
        if (!drive_selected) {
            status(ERROR, "No files have been rebuilt yet!");
        } else {
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
        /* Prep all the windows */
        wnoutrefresh(stdscr);
        prep_op();
        prep_cmds();
        doupdate();
    } while (parse_input(getch()));

    exit(0);
}
