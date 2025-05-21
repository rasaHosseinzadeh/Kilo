#ifndef EDITOR
#define EDITOR

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "append_buf.h"
#include "terminal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct erow {
  int size;
  char *chars;
  // For syntax highlighting, stores the highlight type for each character
  unsigned char *hl; 
} erow;

// Forward declaration for editorSyntax used in editorBuffer
struct editorSyntax;

struct editorBuffer {
  int cx, cy;
  int rowoff, coloff;
  int numrows;
  int dirty;
  erow *row;
  char *filename;
  struct editorSyntax *syntax; // Pointer to syntax highlighting rules
};

#define MAX_BUFFERS 10 // Maximum number of buffers

// Syntax highlighting flags
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct editorSyntax {
  char *filetype;
  char **filematch; // Array of file extensions/patterns
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

// Highlighting Colors (ANSI escape codes)
// These are just examples, specific colors can be chosen
#define HL_NORMAL 0 // Default terminal color
#define HL_COMMENT 36  // Cyan for comments
#define HL_KEYWORD1 33 // Yellow for primary keywords
#define HL_KEYWORD2 32 // Green for type keywords
#define HL_STRING 35   // Magenta for strings
#define HL_NUMBER 31   // Red for numbers
#define HL_MATCH 34    // Blue for search matches (example, not part of this task's HL struct)

struct editor_config {
  int screen_rows, screen_cols;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termois;
  struct editorBuffer *buffers[MAX_BUFFERS]; // Array of buffer pointers
  int active_buffer; // Index of the active buffer
  int num_buffers;   // Current number of open buffers

  // Autocomplete state
  char **suggestions;
  int num_suggestions;
  int active_suggestion_idx;
  int autocomplete_active; // Is the suggestion box currently displayed?
  int ac_trigger_cx;       // Cursor X where autocomplete was triggered
  int ac_trigger_cy;       // Cursor Y where autocomplete was triggered
  int ac_prefix_len;       // Length of the prefix that generated suggestions
};

extern struct editor_config E;

enum editorKey {
  ESCAPE = 27,
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY,
};

void init();
int read_key();
void draw_rows(struct abuf *ab);
void refresh_screen();
void process_key_press();
void move_cursor(int key);
void open_file(char *filename);
void insert_row(int at, char *s, size_t len);
void set_status_message(const char *fmt, ...);
void draw_tab_bar(struct abuf *ab); // Prototype for draw_tab_bar

#endif
