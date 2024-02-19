#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct termios orig_termois;

void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

int die(const char *s) {
  clear_screen();
  perror(s);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termois) == -1) {
    die("tcsetattr");
  }
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &orig_termois) == -1) {
    die("tcgetattr");
  }
  atexit(disable_raw_mode);
  struct termios raw = orig_termois;
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

void refresh_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
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
  enable_raw_mode();
  while (1) {
    refresh_screen();
    process_key_press();
  }
  return 0;
}
