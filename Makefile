CC = gcc
CFLAGS = -Wall -g

OBJS = SimbanaF-clienteFTP.o connectTCP.o connectsock.o errexit.o

all: clientftp

clientftp: $(OBJS)
	$(CC) $(CFLAGS) -o clientftp $(OBJS)

SimbanaF-clienteFTP.o: SimbanaF-clienteFTP.c
	$(CC) $(CFLAGS) -c SimbanaF-clienteFTP.c

connectTCP.o: connectTCP.c
	$(CC) $(CFLAGS) -c connectTCP.c

connectsock.o: connectsock.c
	$(CC) $(CFLAGS) -c connectsock.c

errexit.o: errexit.c
	$(CC) $(CFLAGS) -c errexit.c

clean:
	rm -f *.o clientftp

