CC = g++
CFLAGS = -Wall -shared -fPIC
OBJ = memory.so

all: $(OBJ)

%.so: %.c
	$(CC) $(CFLAGS) $*.c -o lib$*.so

clean:
	rm -rf *.so