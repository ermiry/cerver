CC = gcc
CFLAGS = -I $(IDIR) -pthread -D DEBUG

IDIR = ./include/
SRCDIR = ./src/

SOURCES = $(SRCDIR)*.c \
		  $(SRCDIR)utils/*.c 

all: server #run #clean

server: $(SOURCES)
	$(CC) $(SOURCES) $(CFLAGS) -o ./bin/server

run:
	./bin/server

clean:
	rm ./bin/server 