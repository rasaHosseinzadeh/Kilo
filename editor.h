#ifndef EDITOR
#define EDITOR

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "append_buf.h"
#include "terminal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editor_config {
  int cx, cy;
  int rowoff, coloff;
  int screen_rows, screen_cols;
  int numrows;
  int dirty;
  erow *row;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termois;
};

extern struct editor_config E;

enum editorKey {
  ESCAPE = 27,
  BACKSPACE = 127,
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
void open_file(char *filename);
void append_row(char *s, size_t len);
void set_status_message(const char *fmt, ...);

#endif
