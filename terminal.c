#include "terminal.h"
#include "editor.h" // For editorBuffer, editor_config, MAX_BUFFERS

void clear_screen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

// editor.h is needed for struct editorBuffer and E
// However, to avoid circular dependencies if terminal.h is included by editor.h,
// we might need to forward declare or be careful.
// For now, let's assume editor.h can be included here or relevant structs are opaque/forward declared.
// Actually, editor.c includes editor.h, and terminal.c is linked with editor.c,
// so E should be accessible as an extern. Let's get get_active_buffer.
// No, get_active_buffer is in editor.c.
// Simplest for now: die() should clean up global resources it knows about,
// and active buffer specific resources if possible.

// Forward declare editorBuffer to avoid include issues if terminal.h is included elsewhere.
struct editorBuffer; 
extern struct editor_config E; // E is defined in editor.c

// Function to get active buffer, similar to one in editor.c but might be risky due to potential linkage issues
// or order of compilation. A better way would be to pass necessary info to die().
// The original die cleaned E.row and E.numrows which are now per-buffer.
// This version attempts to clean all buffers.

int die(const char *s) {
  clear_screen(); // Clear screen first
  perror(s);      // Print error message for s

  // Clean up all buffers
  for (int i = 0; i < E.num_buffers; i++) {
    if (E.buffers[i] != NULL) {
      struct editorBuffer *buf_to_clean = E.buffers[i];
      for (int j = 0; j < buf_to_clean->numrows; j++) {
        if (buf_to_clean->row && buf_to_clean->row[j].chars) {
             free(buf_to_clean->row[j].chars);
        }
      }
      free(buf_to_clean->row);
      free(buf_to_clean->filename);
      free(buf_to_clean); // Free the buffer struct itself
      E.buffers[i] = NULL; // Avoid double free if somehow called again or E is inspected
    }
  }
  // Note: E.orig_termios is restored by atexit(disable_raw_mode), which is good.
  // No need to free E.buffers array itself as it's a global variable, not heap allocated.
  // (It's an array of pointers, the pointers are to heap-allocated structs)
  
  exit(1); // Exit the program
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
