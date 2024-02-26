#ifndef EDITOR
#define EDITOR

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "append_buf.h"
#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editor_config {
  int cx, cy;
  int rowoff, coloff;
  int screen_rows;
  int screen_cols;
  struct termios orig_termois;
  int numrows;
  erow *row;
};

extern struct editor_config E;

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY,
};

void init();
int read_key();
void draw_rows(struct abuf *ab);
void refresh_screen();
void process_key_press();
void move_cursor(int key);
void open(char *filename);
void append_row(char *s, size_t len);

#endif
