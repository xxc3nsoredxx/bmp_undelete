CLI_OBJS = bmp.o cli.o recover.o
TUI_OBJS = bmp.o recover.o tui.o
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c
TUI_LFLAGS = -lncurses

all: recover_cli recover_tui

bmp_undelete_cli: $(CLI_OBJS)
	$(CC) $(CLI_OBJS) -o bmp_undelete_cli

bmp_undelete_tui: $(TUI_OBJS)
	$(CC) $(TUI_OBJS) $(TUI_LFLAGS) -o bmp_undelete_tui

bmp.o: bmp.c bmp.h
	$(CC) $(CFLAGS) bmp.c

cli.o: cli.c recover.h
	$(CC) $(CFLAGS) cli.c

recover.o: recover.c bmp.h ext.h recover.h
	$(CC) $(CFLAGS) recover.c

tui.o: tui.c recover.h
	$(CC) $(CFLAGS) tui.c
