CC = gcc
CFLAGS = -Wall -g

MLIBDIR = memorylib
MLIB = memory

LDFLAGS = -lpthread
LDFLAGS += -l$(MLIB) -L./$(MLIBDIR)
EXECUTABLE = main

all: $(EXECUTABLE)

$(EXECUTABLE): $(EXECUTABLE).c
	cd $(MLIBDIR); make;
	$(CC) $(CFLAGS) $@.c $(LDFLAGS) -o $@

clean:
	cd $(MLIBDIR); make clean;
	rm -rf $(EXECUTABLE)
