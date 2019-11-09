OBJS = main.o bmp.o
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c

all: recover

recover: $(OBJS)
	$(CC) $(OBJS) -o recover

main.o: main.c bmp.h ext.h
	$(CC) $(CFLAGS) main.c

bmp.o: bmp.c bmp.h
	$(CC) $(CFLAGS) bmp.c
