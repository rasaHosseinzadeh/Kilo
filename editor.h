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
#include <regex.h>
#include <ncurses.h>
#include <limits.h>

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow {
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

struct editor_config {
  int cx, cy;
  int rx;
  int rowoff, coloff;
  int screen_rows, screen_cols;
  int numrows;
  int dirty;
  int readonly;
  erow *row;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  struct termios orig_termois;
  int mode;
};

enum editor_mode {
  MODE_NORMAL,
  MODE_INSERT,
  MODE_COMMAND
};

extern struct editor_config *E_;
#define E (*E_)

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
void draw_tabs(struct abuf *ab);
void refresh_screen();
void process_key_press();
void move_cursor(int key);
int open_file(char *filename);
void insert_row(int at, char *s, size_t len);
void set_status_message(const char *fmt, ...);
void update_syntax(erow *row);
int syntax_to_color(int hl);
void select_syntax_highlight();
int is_separator(int c);
void update_row(erow *row);
void open_line_below();
void move_word_forward();
void move_word_backward();
void move_word_end();
void move_end_line();
void switch_buffer(int idx);
void close_current_buffer();
void open_new_file(char *filename, int readonly);
void autocomplete();
void command_mode();
void search_mode();
void substitute(char *pattern, char *repl);
void search_next(int dir);
void highlight_search(erow *row);
void clear_search();

#endif
