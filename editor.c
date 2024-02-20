#include "editor.h"

struct editor_config E;

void init() {
  enable_raw_mode();
  if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
    die("get_windows_size");
  }
}

char read_key() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {
      die("read");
    }
  }
  return c;
}

void draw_rows(struct abuf *ab) {
  for (int y = 0; y < E.screen_rows; ++y) {
    ab_append(ab, "~", 1);
    if (y != E.screen_rows - 1) {
      ab_append(ab, "\r\n", 2);
    }
  }
}

void refresh_screen() {
  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[2J", 4);
  ab_append(&ab, "\x1b[H", 3);
  draw_rows(&ab);
  ab_append(&ab, "\x1b[H", 3);
  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

void process_key_press() {
  char c = read_key();
  switch (c) {
  case CTRL_KEY('q'):
    clear_screen();
    exit(0);
    break;
  }
}
