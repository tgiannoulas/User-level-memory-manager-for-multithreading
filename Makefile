CC = gcc
CFLAGS = -Wall -g -lpthread
MLIBDIR = memorylib
MLIB = memory
EXECUTABLE = main


all: $(EXECUTABLE)

$(EXECUTABLE): $(EXECUTABLE).c
	cd $(MLIBDIR); make;
	$(CC) $(CFLAGS) $@.c -l$(MLIB) -L./$(MLIBDIR) -o $@

clean:
	cd $(MLIBDIR); make clean;
	rm -rf $(EXECUTABLE)
