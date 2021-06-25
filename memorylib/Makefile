CC = g++
CFLAGS = -Wall -g -shared -fPIC
LIB = memory.so
SRC = memory.c list.c

all: $(LIB)

$(LIB): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o lib$(LIB)

clean:
	rm -rf $(LIB)
