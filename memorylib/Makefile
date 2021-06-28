CC = g++
CFLAGS = -Wall -g -shared -fPIC
LIB = libmemory.so
SRC = memory.c list.c

all: $(LIB)

$(LIB): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(LIB)

clean:
	rm -rf $(LIB)
