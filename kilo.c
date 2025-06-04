#include "editor.h"

int main(int argc, char *argv[]) {
  init();
  if (argc >= 2) {
    /* populate the initial buffer with the first file */
    open_file(argv[1]);
    for (int i = 2; i < argc; i++) {
      open_new_file(argv[i], 0);
    }
    switch_buffer(0);
  } else {
    /* start with an empty unnamed buffer */
    open_new_file(NULL, 0);
  }
  set_status_message("This ain't vim! Hit Ctrl+q to exit.");
  while (1) {
    refresh_screen();
    process_key_press();
  }
  return 0;
}
