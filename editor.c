#include "editor.h"

struct editor_config E;

/* Syntax highlighting */

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", "size_t|", "const|", "extern|", "bool|", "volatile|", "register|"
};

char *PY_HL_extensions[] = {".py", NULL};
char *PY_HL_keywords[] = {
  "and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
  "else", "except", "exec", "finally", "for", "from", "global", "if", "import",
  "in", "is", "lambda", "not", "or", "pass", "print", "raise", "return", "try",
  "while", "with", "yield",
  
  "False|", "None|", "True|", "self|", "int|", "float|", "str|", "list|", "dict|",
  "set|", "bool|", "bytes|", "tuple|", "range|", "object|", "Exception|"
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "python",
    PY_HL_extensions,
    PY_HL_keywords,
    "#", "\"\"\"", "\"\"\"",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

void init() {
  E.row = NULL;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  enable_raw_mode();
  if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
    die("get_windows_size");
  }
  E.screen_rows -= 2;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.dirty = 0;
  E.syntax = NULL;
}

void update_row(erow *row) {
  free(row->render);
  row->render = malloc(row->size + 1);
  
  int j = 0;
  for (int i = 0; i < row->size; i++) {
    row->render[j++] = row->chars[i];
  }
  row->render[j] = '\0';
  row->rsize = j;
  
  update_syntax(row);
}

void update_syntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  
  if (E.syntax == NULL) return;
  
  char **keywords = E.syntax->keywords;
  
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  
  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;
  
  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->hl_open_comment) ? 1 : 0;
  
  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
    
    // Handle single line comments
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }
    
    // Handle multi-line comments
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    
    // Handle strings
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    
    // Handle numbers
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }
    
    // Handle keywords
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;
        
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }
    
    prev_sep = is_separator(c);
    i++;
  }
  
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    update_syntax(&E.row[row->idx + 1]);
}

void insert_row(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) {
    return;
  }
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], (E.numrows - at) * sizeof(erow));
  
  E.row[at].idx = at;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  update_row(&E.row[at]);
  
  E.numrows++;
  E.dirty = 1;
}

void select_syntax_highlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;
  
  char *ext = strrchr(E.filename, '.');
  
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;
        
        // Rehighlight all rows
        for (int filerow = 0; filerow < E.numrows; filerow++) {
          update_syntax(&E.row[filerow]);
        }
        
        return;
      }
      i++;
    }
  }
}

void open_file(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "a"); // Ensure the file exists
  if (!fp) {
    die("fopen");
  }
  fclose(fp);
  fp = fopen(filename, "r");
  if (!fp) {
    die("fopen");
  }
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    insert_row(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  
  select_syntax_highlight();
}

void insert_enter() {
  if (E.cx == 0) {
    insert_row(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy]; // previous refrence is invalid as insert_row callls
                        // realloc
    row->size = E.cx;
    row->chars[E.cx] = '\0';
  }
  E.cx = 0;
  E.cy++;
}

void row_insert_char(erow *row, int at, int c) {
  if (at < 0 || at > row->size) {
    at = row->size;
  }
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  update_row(row);
  E.dirty = 1;
}

void append_string_to_row(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  update_row(row);
  E.dirty = 1;
}

void del_row(int at) {
  if (at < 0 || at >= E.numrows) {
    return;
  }
  free(E.row[at].chars);
  free(E.row[at].render);
  free(E.row[at].hl);
  memmove(&E.row[at], &E.row[at + 1], (E.numrows - at - 1) * sizeof(erow));
  
  // Update the idx values for all rows after the deleted row
  for (int j = at; j < E.numrows - 1; j++) {
    E.row[j].idx = j;
  }
  
  E.numrows -= 1;
  E.dirty = 1;
}

void row_del_char(erow *row, int at) {
  if (at < 0 || at >= row->size) {
    return;
  }
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size -= 1;
  update_row(row);
  E.dirty = 1;
}

void insert_char(int c) {
  if (E.cy == E.numrows) {
    insert_row(E.numrows, "", 0);
  }
  row_insert_char(&E.row[E.cy], E.cx++, c);
}

void del_char() {
  if (E.cy == E.numrows) {
    return;
  }
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    row_del_char(row, E.cx - 1);
    E.cx -= 1;
  } else if (E.cx == 0 && E.cy > 0) {
    E.cx = E.row[E.cy - 1].size;
    append_string_to_row(&E.row[E.cy - 1], row->chars, row->size);
    del_row(E.cy);
    E.cy -= 1;
  }
}

char *rows2string(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; ++j) {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; ++j) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

char *show_prompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128, buflen = 0;
  char *buf = malloc(bufsize);
  buf[0] = '\0';

  while (1) {
    set_status_message(prompt, buf);
    refresh_screen();
    int c = read_key();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen > 0) {
        buf[--buflen] = '\0';
      }
    } else if (c == '\x1b') {
      set_status_message("");
      if (callback) {
        callback(buf, c);
      }
      free(buf);
      return NULL;
    } else if (c == '\r' || c == '\n') {
      if (buflen != 0) {
        set_status_message("");
        if (callback) {
          callback(buf, c);
        }
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize <<= 1;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback) {
      callback(buf, c);
    }
  }
}

void save_file() {
  if (E.filename == NULL) {
    E.filename = show_prompt("Save as: %s", NULL);
    if (E.filename == NULL) {
      set_status_message("Save aborted");
      return;
    }
    select_syntax_highlight();
  }
  int len;
  int err = 0;
  char *buf = rows2string(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    err = 1;
    goto cleanup;
  }
  if (ftruncate(fd, len) == -1) {
    err = 1;
    goto cleanup;
  }
  if (write(fd, buf, len) != len) {
    err = 1;
    goto cleanup;
  }
cleanup:
  if (err) {
    set_status_message("Can't save! I/O error: %s", strerror(errno));
  } else {
    E.dirty = 0;
    set_status_message("Wrote %d bytes to disk.", len);
  }
  close(fd);
  free(buf);
}

void find_callback(char *query, int c) {
  static int last_match = -1;
  static int direction = 1;
  if (c == '\r' || c == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (c == ARROW_DOWN || c == ARROW_RIGHT) {
    direction = 1;
  } else if (c == ARROW_UP || c == ARROW_LEFT) {
    direction = -1;
  } else {
    direction = 1;
    last_match = -1;
  }
  int i, current = last_match;
  for (i = 0; i < E.numrows; ++i) {
    current += direction;
    if (current == -1) {
      current = E.numrows - 1;
    } else if (current == E.numrows) {
      current = 0;
    }
    erow *row = &E.row[current];
    char *match = strstr(row->chars, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = match - row->chars;
      E.rowoff = E.numrows;
      break;
    }
  }
}

void find() {
  int bkupx = E.cx, bkupy = E.cy;
  int bkupcoloff = E.coloff, bkuprowoff = E.rowoff;
  char *query = show_prompt("Search: %s (ESC to cancel)", find_callback);
  if (query) {
    free(query);
  } else {
    E.cx = bkupx;
    E.cy = bkupy;
    E.coloff = bkupcoloff;
    E.rowoff = bkuprowoff;
  }
}

int read_key() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1) {
      die("read");
    }
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '3':
            return DEL_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        }
      }
    }
  }
  return c;
}
int cx_to_rx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (8 - 1) - (rx % 8);
    rx++;
  }
  return rx;
}

void move_cursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
  case ARROW_LEFT:
    if (E.cx > 0) {
      E.cx--;
    } else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size) {
      E.cx++;
    } else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    E.cy--;
    E.cy = E.cy < 0 ? 0 : E.cy;
    break;
  case ARROW_DOWN:
    E.cy++;
    E.cy = E.cy > E.numrows ? E.numrows : E.cy;
    break;
  }
  
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void draw_status_bar(struct abuf *ab) {
  ab_append(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                     E.syntax ? E.syntax->filetype : "no ft",
                     E.cy + 1, E.numrows);
  if (len > E.screen_cols)
    len = E.screen_cols;
  ab_append(ab, status, len);
  while (len < E.screen_cols) {
    if (E.screen_cols - len == rlen) {
      ab_append(ab, rstatus, rlen);
      break;
    } else {
      ab_append(ab, " ", 1);
      len++;
    }
  }
  ab_append(ab, "\x1b[m", 4);
  ab_append(ab, "\n\r", 2);
}

void draw_message_bar(struct abuf *ab) {
  ab_append(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screen_cols) {
    msglen = E.screen_cols;
  }
  if (msglen && time(NULL) - E.statusmsg_time < 5) {
    ab_append(ab, E.statusmsg, msglen);
  }
}

void draw_rows(struct abuf *ab) {
  for (int y = E.rowoff; y < E.rowoff + E.screen_rows; ++y) {
    if (y >= E.numrows) {
      ab_append(ab, "~", 1);
    } else {
      int len = E.row[y].rsize - E.coloff;
      if (len < 0) {
        len = 0;
      }
      if (len > E.screen_cols) {
        len = E.screen_cols;
      }
      
      char *c = &E.row[y].render[E.coloff];
      unsigned char *hl = &E.row[y].hl[E.coloff];
      int current_color = -1;
      
      for (int j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            ab_append(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          ab_append(ab, &c[j], 1);
        } else {
          int color = syntax_to_color(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            ab_append(ab, buf, clen);
          }
          ab_append(ab, &c[j], 1);
        }
      }
      ab_append(ab, "\x1b[39m", 5);
    }
    ab_append(ab, "\x1b[K", 3);
    ab_append(ab, "\r\n", 2);
  }
}

void scroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = cx_to_rx(&E.row[E.cy], E.cx);
  }
  
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  } else if (E.cy >= E.rowoff + E.screen_rows) {
    E.rowoff = E.cy - E.screen_rows + 1;
  }

  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  } else if (E.rx >= E.coloff + E.screen_cols) {
    E.coloff = E.rx - E.screen_cols + 1;
  }
}

void refresh_screen() {
  scroll();
  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[?25l", 6); // Hide cursor
  ab_append(&ab, "\x1b[H", 3);
  draw_rows(&ab);
  draw_status_bar(&ab);
  draw_message_bar(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  ab_append(&ab, buf, strlen(buf));
  ab_append(&ab, "\x1b[?25h", 6); // Show cursor
  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

void set_status_message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/* Syntax highlighting functions */

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

int syntax_to_color(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;  // Cyan
    case HL_KEYWORD1: return 33;   // Yellow
    case HL_KEYWORD2: return 32;   // Green
    case HL_STRING: return 35;     // Magenta
    case HL_NUMBER: return 31;     // Red
    case HL_MATCH: return 34;      // Blue
    default: return 37;            // White
  }
}

void process_key_press() {
  static int quit_times = 1;
  int c = read_key();
  switch (c) {
  case ESCAPE:
  case CTRL_KEY('l'):
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {
      set_status_message("Unsaved changes! Press Ctrl+q again to quit.");
      quit_times--;
      return;
    }
    clear_screen();
    exit(0);
    break;
  case CTRL_KEY('s'):
    save_file();
    break;
  case '\r':
  case '\n':
    insert_enter();
    break;
  case BACKSPACE:
  case DEL_KEY:
  case CTRL_KEY('h'):
    if (c == DEL_KEY) {
      move_cursor(ARROW_RIGHT);
    }
    del_char();
    break;
  case CTRL_KEY('f'):
    find();
    break;
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
  case ARROW_LEFT:
    move_cursor(c);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screen_rows;
    while (times--)
      move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  default:
    insert_char(c);
  }
  quit_times = 1;
}
