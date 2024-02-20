CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
OBJ = kilo.o append_buf.o
DEPS = append_buf.h
EXEC = kilo

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f $(OBJ) $(EXEC)

