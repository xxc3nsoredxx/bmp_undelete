OBJS_CLI = cli.o bmp.o
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c

all: recover_cli

recover_cli: $(OBJS_CLI)
	$(CC) $(OBJS_CLI) -o recover_cli

cli.o: cli.c bmp.h ext.h
	$(CC) $(CFLAGS) main.c

bmp.o: bmp.c bmp.h
	$(CC) $(CFLAGS) bmp.c
