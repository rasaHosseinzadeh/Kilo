CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
OBJ = kilo.o append_buf.o terminal.o editor.o
LIBS = -lncurses
DEPS = append_buf.h terminal.h editor.h
EXEC = kilo

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJ) $(EXEC)

