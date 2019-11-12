OBJS_CLI = bmp.o cli.o recover.o
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c

all: recover_cli

recover_cli: $(OBJS_CLI)
	$(CC) $(OBJS_CLI) -o recover_cli

bmp.o: bmp.c bmp.h
	$(CC) $(CFLAGS) bmp.c

cli.o: cli.c recover.h
	$(CC) $(CFLAGS) cli.c

recover.o: recover.c bmp.h ext.h recover.h
	$(CC) $(CFLAGS) recover.c
