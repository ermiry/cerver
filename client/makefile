CC = gcc
CFLAGS = -I $(IDIR)

IDIR = ./include/
SRCDIR = ./src/

SOURCES = $(SRCDIR)*.c 

all: client #run #clean

client: $(SOURCES)
	$(CC) $(SOURCES) $(CFLAGS) -o ./bin/client

run:
	./bin/client

clean:
	rm ./bin/client 