#ifndef EDITOR
#define EDITOR

#include "append_buf.h"
#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct editor_config {
  int cx, cy;
  int screen_rows;
  int screen_cols;
  struct termios orig_termois;
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

#endif
