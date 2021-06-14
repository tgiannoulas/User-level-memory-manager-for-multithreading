CC = g++
CFLAGS = -Wall -shared -fPIC
LIB = memory.so
SRC = memory.c list.c

all: $(LIB)

$(LIB): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o lib$(LIB)

clean:
	rm -rf $(LIB)
