#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "recover.h"

#define CSI         "\x1B["
#define RED         "" CSI "91m"
#define YELLOW      "" CSI "93m"
#define GREEN       "" CSI "92m"
#define RESET       "" CSI "0m"

void usage () {
    printf("Usage: ./recover_cli [device]\n");
    printf("NOTE: Requires root permissions.\n");
}

/* 
 * Recieve the broadcasted status
 */
void status (enum status_code_e sl, ...) {
    va_list ap;
    uint32_t var = 0;

    va_start(ap, sl);

    switch (sl) {
    /* Handle methods */
    case CLEANUP:
        printf(YELLOW "[!] " RESET
            "Cleaning up junk\n");
        break;

    case GROUP_INFO:
        printf(YELLOW "[!] " RESET
            "Saving information about group:");
        break;
    case GROUP_PROG:
        var = va_arg(ap, uint32_t);
        /* Display the current group number accessed */
        printf(" %u%s", var, ((var == *fs_info.ngroups - 1) ? "\n" : ""));
        break;

    case POP:
        vprintf(YELLOW "[!] " RESET
            "Populating inode %u...\n", ap);
        break;
    case POP_DIR:
        vprintf(YELLOW "[!] " RESET
            "Direct blocks: %u -> %u\n", ap);
        break;
    case POP_IND:
        vprintf(YELLOW "[!] " RESET
            "%ux indirect block: %u\n", ap);
        break;

    case LINK:
        vprintf(YELLOW "[!] " RESET
            "Linking inode %u to root directory...\n", ap);
        break;
    case RECOVERED:
        printf(GREEN "[+] ");
        vprintf("Recovered file: %s\n", ap);
        printf(RESET);
        break;

    case SCAN:
        printf(YELLOW "[!] " RESET
            "Scanning drive for important blocks...\n");
        break;
    case SCAN_IND:
        printf(GREEN "[+] ");
        vprintf("Found potential %ux indirect block: %u\n", ap);
        printf(RESET);
        break;
    case SCAN_BMP:
        printf(GREEN "[+] ");
        vprintf("Found potential BMP header block: %u\n", ap);
        printf(RESET);
        break;
    case SCAN_PROG:
        var = va_arg(ap, uint32_t);
        /* Display progress every 10% */
        if (var % 10 == 0) {
            printf(YELLOW "[!] " RESET
                "%u%% complete...\n", var);
        }
        break;

    case COLLECT:
        printf(YELLOW "[!] " RESET
            "Building BMP files...\n");
        break;
    case SANITY:
        vprintf("\n" YELLOW "[!] " RESET
            "Running sanity check on block %u...\n", ap);
        break;
    case INODE:
        printf(GREEN "[+] ");
        vprintf("Reserved inode %u\n", ap);
        printf(RESET);
        break;

    case DONE:
        printf(YELLOW "[!] " RESET
            "Done!\n"
            "------------------------------\n");
        break;

    /* Handle error codes */
    case ERROR:
        printf(RED "[-] ");
        vprintf(va_arg(ap, const char*), ap);
        printf(RESET);
        break;
    case WARN:
        printf(YELLOW "[!] ");
        vprintf(va_arg(ap, const char*), ap);
        printf(RESET);
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

    /* Test if running as root */
    if (getuid()) {
        status(ERROR, "Requires root permissions to run!\n");
        exit(-1);
    }

    /* Initialize */
    init(*(argv + 1));

    /* Scan the drive */
    if (!scan()) {
        status(ERROR, "No potential BMP start blocks found, exiting...\n");
        exit(-1);
    }

    /* Create entries for the files found */
    collect();

    exit(0);
}
