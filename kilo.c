#include "append_buf.h"

#define CTRL_KEY(k) ((k) & 0x1f)

struct editor_config {
  int screen_rows;
  int screen_cols;
  struct termios orig_termois;
};

struct editor_config E;

void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

int die(const char *s) {
  clear_screen();
  perror(s);
  exit(1);
}

int get_window_size(int *rows, int *cols) {
  struct winsize wsz;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsz) == -1 && wsz.ws_col == 0) {
    return -1;
  } else {
    *cols = wsz.ws_col;
    *rows = wsz.ws_row;
    return 0;
  }
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termois) == -1) {
    die("tcsetattr");
  }
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termois) == -1) {
    die("tcgetattr");
  }
  atexit(disable_raw_mode);
  struct termios raw = E.orig_termois;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= !(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}
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

int main() {
  init();
  while (1) {
    refresh_screen();
    process_key_press();
  }
  return 0;
}
