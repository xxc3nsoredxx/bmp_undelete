OBJS = main.o
CC = gcc
CFLAGS = -Wall -Wextra --pedantic-errors -std=c89 -c

all: recover

recover: $(OBJS)
	$(CC) $(OBJS) -o recover

main.o: main.c ext.h
	$(CC) $(CFLAGS) main.c
