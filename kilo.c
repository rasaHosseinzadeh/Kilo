#include "editor.h"

int main() {
  init();
  while (1) {
    refresh_screen();
    process_key_press();
  }
  return 0;
}
