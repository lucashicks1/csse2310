CC = gcc
CFLAGS = -Wall -g -pedantic -pthread -std=gnu99 -I/local/courses/csse2310/include
LIBS = -L/local/courses/csse2310/lib -lcsse2310a4 -lcsse2310a3 -lcrypt

all: crackclient crackserver

crackclient: crackclient.c
	$(CC) $(CFLAGS) $(LIBS) crackclient.c -o crackclient

crackserver: crackserver.c
	$(CC) $(CFLAGS) $(LIBS) crackserver.c -o crackserver

clean: 
	rm -f crackclient crackserver

