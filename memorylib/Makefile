CC = g++
CFLAGS = -Wall -g
LDFLAGS =  -lpthread -shared -fPIC
LIB = libmemory.so
SRC = memory.c list.c

all: $(LIB)

$(LIB): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(LIB)

clean:
	rm -rf $(LIB)
