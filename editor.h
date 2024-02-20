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
  int screen_rows;
  int screen_cols;
  struct termios orig_termois;
};

extern struct editor_config E;

void init();
char read_key();
void draw_rows(struct abuf *ab);
void refresh_screen();
void process_key_press();

#endif
