#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "recover.h"

#define CSI         "\x1B["
#define RED         "" CSI "91m"
#define YELLOW      "" CSI "93m"
#define GREEN       "" CSI "92m"
#define RESET       "" CSI "0m"

void usage () {
    printf("Usage: ./recover [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

void status (enum status_level_e sl, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);

    /* Handle each status level */
    switch (sl) {
    case BAD:
        printf(RED "[-] ");
        vprintf(fmt, ap);
        printf(RESET);
        break;
    case GOOD:
        printf(GREEN "[+] ");
        vprintf(fmt, ap);
        printf(RESET);
        break;
    case INFO:
    default:
        printf(YELLOW "[!] " RESET);
        vprintf(fmt, ap);
        break;
    }

    va_end(ap);
}

int main (int argc, char **argv) {
    /* Test args */
    if (argc != 2) {
        usage();
        exit(-1);
    }

    /* Initialize */
    init(*(argv + 1));

    /* Scan the drive */
    if (!scan()) {
        status(BAD, "No potential BMP start blocks found, exiting...\n");
        exit(-1);
    }

    /* Create entries for the files found */
    collect();

    exit(0);
}
