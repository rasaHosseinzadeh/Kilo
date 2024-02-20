#include "editor.h"

struct editor_config E;

void init() {
  E.cx = 0;
  E.cy = 0;
  enable_raw_mode();
  if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
    die("get_windows_size");
  }
}

int read_key() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {
      die("read");
    }
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '3':
            return DEL_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        }
      }
    }
  }
  return c;
}
void move_cursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    E.cx--;
    E.cx = E.cx < 0 ? 0 : E.cx;
    break;
  case ARROW_RIGHT:
    E.cx++;
    E.cx = E.cx >= E.screen_cols ? E.screen_cols - 1 : E.cx;
    break;
  case ARROW_UP:
    E.cy--;
    E.cy = E.cx < 0 ? 0 : E.cx;
    break;
  case ARROW_DOWN:
    E.cy++;
    E.cy = E.cy >= E.screen_rows ? E.screen_rows - 1 : E.cy;
    break;
  }
}
void draw_rows(struct abuf *ab) {
  for (int y = 0; y < E.screen_rows; ++y) {
    ab_append(ab, "~", 1);
    ab_append(ab, "\x1b[K", 3);
    if (y != E.screen_rows - 1) {
      ab_append(ab, "\r\n", 2);
    }
  }
}

void refresh_screen() {
  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[H", 3);
  draw_rows(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  ab_append(&ab, buf, strlen(buf));
  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

void process_key_press() {
  int c = read_key();
  switch (c) {
  case CTRL_KEY('q'):
    clear_screen();
    exit(0);
    break;
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
  case ARROW_LEFT:
    move_cursor(c);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screen_rows;
    while (times--)
      move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  }
}
