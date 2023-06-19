CC=gcc
CFLAGS=-Wall
LDFLAGS=-lrt

kvdb: kvdb.c
	$(CC) $(CFLAGS) -o kvdb kvdb.c $(LDFLAGS)
	$(CC) $(CFLAGS) -o unit_test unit_test.c

clean:
	rm -f kvdb
	rm -f unit_test
	rm -f kvdb.dat
