<<<<<<< HEAD
OBJS_CLI = bmp.o cli.o recover.o
=======
OBJS_CLI = cli.o bmp.o
>>>>>>> 84b2620017c0d93bf2ac7f078f11bc67e90cc8ef
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c

all: recover_cli

recover_cli: $(OBJS_CLI)
	$(CC) $(OBJS_CLI) -o recover_cli
<<<<<<< HEAD
=======

cli.o: cli.c bmp.h ext.h
	$(CC) $(CFLAGS) main.c
>>>>>>> 84b2620017c0d93bf2ac7f078f11bc67e90cc8ef

bmp.o: bmp.c bmp.h
	$(CC) $(CFLAGS) bmp.c

cli.o: cli.c recover.h
	$(CC) $(CFLAGS) cli.c

recover.o: recover.c bmp.h ext.h recover.h
	$(CC) $(CFLAGS) recover.c
