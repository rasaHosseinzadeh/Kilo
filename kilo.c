#include "editor.h"

int main(int argc, char *argv[]) {
  init();
  if (argc >= 2) {
    open(argv[1]);
  }
  set_status_message("This ain't vim! Hit Ctrl+q to exit.");
  while (1) {
    refresh_screen();
    process_key_press();
  }
  return 0;
}
