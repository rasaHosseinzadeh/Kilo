#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termois;

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termois); }

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_termois);
  atexit(disable_raw_mode);
  struct termios raw = orig_termois;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
  char c;
  enable_raw_mode();
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    if (iscntrl(c)) {
      printf("%d\n", c);
    } else {
      printf("%d ( %c )\n", c, c);
    }
  }
  return 0;
}
