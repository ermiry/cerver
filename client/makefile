CC = gcc
CFLAGS = -I $(IDIR) `sdl2-config --cflags --libs` -lm

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