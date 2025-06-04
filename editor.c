#include "editor.h"

struct editor_config *E_;
static struct editor_config *buffers[10];
static int num_buffers = 0;
static int cur_buffer = 0;

/* Autocomplete state */
static char **ac_matches = NULL;
static int ac_count = 0;
static int ac_index = 0;
static int ac_prefixlen = 0;
static int ac_startx = 0;
static int ac_inserted = 0;
static int ac_active = 0;

/* Search state */
static regex_t search_regex;
static char *search_query = NULL;
static int search_active = 0;

static void ac_reset() {
  if (ac_matches) {
    for (int i = 0; i < ac_count; i++)
      free(ac_matches[i]);
    free(ac_matches);
  }
  ac_matches = NULL;
  ac_count = ac_index = ac_prefixlen = ac_startx = ac_inserted = 0;
  ac_active = 0;
}

static void init_buffer(struct editor_config *b) {
  b->row = NULL;
  b->rowoff = 0;
  b->coloff = 0;
  b->numrows = 0;
  b->cx = 0;
  b->cy = 0;
  b->rx = 0;
  if (get_window_size(&b->screen_rows, &b->screen_cols) == -1) {
    die("get_window_size");
  }
  b->screen_rows -= 3;
  b->filename = NULL;
  b->statusmsg[0] = '\0';
  b->statusmsg_time = 0;
  b->dirty = 0;
  b->readonly = 0;
  b->syntax = NULL;
  b->mode = MODE_NORMAL;
}

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
  E_ = malloc(sizeof(struct editor_config));
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  start_color();
  init_buffer(E_);
  enable_raw_mode();
  buffers[0] = E_;
  num_buffers = 1;
  cur_buffer = 0;
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
  highlight_search(row);
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

void highlight_search(erow *row) {
  if (!search_active || !search_query) return;
  const char *p = row->render;
  regmatch_t m;
  while (regexec(&search_regex, p, 1, &m, 0) == 0) {
    for (int i = m.rm_so; i < m.rm_eo && i < row->rsize; i++)
      row->hl[i] = HL_MATCH;
    p += m.rm_eo;
  }
}

void clear_search() {
  if (!search_active) return;
  regfree(&search_regex);
  free(search_query);
  search_query = NULL;
  search_active = 0;
  for (int i = 0; i < E.numrows; i++)
    update_row(&E.row[i]);
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

int open_file(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    if (errno == ENOENT) {
      /* new file, just start empty */
      select_syntax_highlight();
      set_status_message("New file: %s", filename);
      return 0;
    } else {
      set_status_message("open %s: %s", filename, strerror(errno));
      return -1;
    }
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
  return 0;
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

void open_line_below() {
  if (E.cy >= E.numrows) {
    insert_row(E.numrows, "", 0);
    E.cy = E.numrows - 1;
  } else {
    insert_row(E.cy + 1, "", 0);
    E.cy++;
  }
  E.cx = 0;
  E.mode = MODE_INSERT;
}

void move_end_line() {
  if (E.cy >= E.numrows) return;
  E.cx = E.row[E.cy].size;
}

static int is_word_char(int c) { return !is_separator(c); }

void move_word_forward() {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  if (!row) return;
  while (1) {
    while (E.cx < row->size && is_word_char(row->chars[E.cx])) E.cx++;
    while (E.cx < row->size && !is_word_char(row->chars[E.cx])) E.cx++;
    if (E.cx < row->size) break;
    if (E.cy + 1 >= E.numrows) break;
    E.cy++; row = &E.row[E.cy]; E.cx = 0;
  }
}

void move_word_backward() {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  if (!row) return;
  while (1) {
    while (E.cx > 0 && !is_word_char(row->chars[E.cx-1])) E.cx--;
    while (E.cx > 0 && is_word_char(row->chars[E.cx-1])) E.cx--;
    if (E.cx > 0 || E.cy == 0) break;
    if (E.cy > 0) {
      E.cy--; row = &E.row[E.cy]; E.cx = row->size;
    }
  }
}

void move_word_end() {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  if (!row) return;
  while (1) {
    if (E.cx >= row->size) {
      if (E.cy + 1 >= E.numrows) return;
      E.cy++; row = &E.row[E.cy]; E.cx = 0; continue;
    }
    if (!is_word_char(row->chars[E.cx])) { E.cx++; continue; }
    while (E.cx < row->size && is_word_char(row->chars[E.cx])) E.cx++;
    if (E.cx > 0) E.cx--; /* go to last char */
    break;
  }
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
  if (E.readonly) {
    set_status_message("Cannot save readonly buffer");
    return;
  }
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

void draw_tabs(struct abuf *ab) {
  ab_append(ab, "\x1b[7m", 4);
  for (int i = 0; i < num_buffers; i++) {
    const char *name = buffers[i]->filename ? buffers[i]->filename : "[No Name]";
    if (i == cur_buffer) ab_append(ab, "\x1b[4m", 4); // underline
    ab_append(ab, name, strlen(name));
    if (i == cur_buffer) ab_append(ab, "\x1b[24m", 5);
    if (i < num_buffers - 1) ab_append(ab, " | ", 3);
  }
  ab_append(ab, "\x1b[m", 4);
  ab_append(ab, "\r\n", 2);
}

void draw_status_bar(struct abuf *ab) {
  ab_append(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s%s - %d lines %s",
                     E.filename ? E.filename : "[No Name]",
                     E.readonly ? "[RO]" : "", E.numrows,
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

static void draw_autocomplete(struct abuf *ab) {
  if (!ac_active || ac_count == 0) return;
  int show = ac_count < 5 ? ac_count : 5;
  int width = 0;
  for (int i = 0; i < show; i++) {
    int l = strlen(ac_matches[i]);
    if (l > width) width = l;
  }
  int x = (E.rx - E.coloff) + 1;
  int y = (E.cy - E.rowoff) + 3;
  if (y + show + 2 > E.screen_rows + 2)
    y = E.screen_rows + 2 - show - 2;
  if (x + width + 2 > E.screen_cols)
    x = E.screen_cols - width - 2;
  char buf[32];
  ab_append(ab, "\x1b[s", 3); /* save cursor */
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH+", y, x);
  ab_append(ab, buf, strlen(buf));
  for (int i=0;i<width+2;i++) ab_append(ab, "-", 1);
  ab_append(ab, "+", 1);
  for (int i = 0; i < show; i++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH| %-*s |", y+1+i, x, width, ac_matches[i]);
    ab_append(ab, buf, strlen(buf));
  }
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH+", y+show+1, x);
  ab_append(ab, buf, strlen(buf));
  for (int i=0;i<width+2;i++) ab_append(ab, "-", 1);
  ab_append(ab, "+", 1);
  ab_append(ab, "\x1b[u", 3); /* restore cursor */
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
            ab_append(ab, "\x1b[0m", 4);
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
      ab_append(ab, "\x1b[0m", 4);
    }
    ab_append(ab, "\x1b[K", 3);
    ab_append(ab, "\r\n", 2);
  }
}

void editor_scroll() {
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
  editor_scroll();
  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[?25l", 6); // Hide cursor
  ab_append(&ab, "\x1b[H", 3);
  draw_tabs(&ab);
  draw_rows(&ab);
  draw_status_bar(&ab);
  draw_message_bar(&ab);
  draw_autocomplete(&ab);
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 2,
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
    case HL_MATCH: return 43;      // Yellow background
    default: return 37;            // White
  }
}

void process_key_press() {
  static int quit_times = 1;
  int c = read_key();
  if (E.mode == MODE_INSERT) {
    switch (c) {
    case ESCAPE:
      if (ac_active) {
        while (ac_inserted--) del_char();
        ac_reset();
      } else {
        E.mode = MODE_NORMAL;
      }
      break;
    case '\t':
      autocomplete();
      break;
    default:
      if (ac_active) {
        ac_reset();
      }
      if (c == BACKSPACE || c == DEL_KEY || c == CTRL_KEY('h')) {
        if (c == DEL_KEY)
          move_cursor(ARROW_RIGHT);
        del_char();
      } else if (c == '\r' || c == '\n') {
        insert_enter();
      } else {
        insert_char(c);
      }
    }
  } else {
    switch (c) {
    case 'i':
      E.mode = MODE_INSERT;
      break;
    case 'o':
      open_line_below();
      break;
    case 'h':
      move_cursor(ARROW_LEFT);
      break;
    case 'j':
      move_cursor(ARROW_DOWN);
      break;
    case 'k':
      move_cursor(ARROW_UP);
      break;
    case 'l':
      move_cursor(ARROW_RIGHT);
      break;
    case 'w':
      move_word_forward();
      break;
    case 'b':
      move_word_backward();
      break;
    case 'e':
      move_word_end();
      break;
    case '$':
      move_end_line();
      break;
    case 'n':
      search_next(1);
      break;
    case 'N':
      search_next(-1);
      break;
    case ':':
      command_mode();
      break;
    case '/':
      search_mode();
      break;
    case CTRL_KEY('f'):
      search_mode();
      break;
    case CTRL_KEY('n'):
      switch_buffer((cur_buffer + 1) % num_buffers);
      break;
    case CTRL_KEY('p'):
      switch_buffer((cur_buffer - 1 + num_buffers) % num_buffers);
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
    }
  }
  quit_times = 1;
}

void switch_buffer(int idx) {
  if (idx < 0 || idx >= num_buffers) return;
  cur_buffer = idx;
  E_ = buffers[cur_buffer];
  select_syntax_highlight();
}

static void free_buffer(struct editor_config *b) {
  for (int i = 0; i < b->numrows; i++) {
    free(b->row[i].chars);
    free(b->row[i].render);
    free(b->row[i].hl);
  }
  free(b->row);
  free(b->filename);
  free(b);
}

void close_current_buffer() {
  if (num_buffers <= 1) return; // keep at least one
  free_buffer(buffers[cur_buffer]);
  memmove(&buffers[cur_buffer], &buffers[cur_buffer+1],
          sizeof(struct editor_config*) * (num_buffers - cur_buffer - 1));
  num_buffers--;
  if (cur_buffer >= num_buffers) cur_buffer = num_buffers - 1;
  E_ = buffers[cur_buffer];
  select_syntax_highlight();
}

void open_new_file(char *filename, int readonly) {
  if (num_buffers >= 10) return;
  buffers[num_buffers] = malloc(sizeof(struct editor_config));
  init_buffer(buffers[num_buffers]);
  E_ = buffers[num_buffers];
  if (filename) {
    if (open_file(filename) < 0) {
      /* leave empty buffer on error */
        free(E.filename);
        E.filename = strdup(filename);
    }
  }
  E.readonly = readonly;
  num_buffers++;
  cur_buffer = num_buffers - 1;
}

void autocomplete() {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  if (!row) { ac_reset(); return; }
  int i = E.cx - 1;
  while (i >= 0 && (isalnum(row->chars[i]) || row->chars[i]=='_')) i--;
  int start = i + 1;
  int len = E.cx - start;
  if (!ac_active) {
    if (len <= 0) { insert_char('\t'); return; }
    char prefix[32];
    if (len >= (int)sizeof(prefix)) return;
    memcpy(prefix, &row->chars[start], len);
    prefix[len] = '\0';
    char **keywords = E.syntax ? E.syntax->keywords : NULL;
    if (keywords) {
      for (int k=0; keywords[k]; k++) {
        if (strncmp(keywords[k], prefix, len)==0) {
          const char *kw = keywords[k];
          int kwlen = strlen(kw);
          if (kw[kwlen-1]=='|') kwlen--;
          ac_matches = realloc(ac_matches, sizeof(char*)*(ac_count+1));
          ac_matches[ac_count++] = strndup(kw, kwlen);
        }
      }
    }
    for (int r=0; r<E.numrows; r++) {
      char *p = E.row[r].chars;
      while (*p) {
        while (*p && is_separator(*p)) p++;
        char *startw = p;
        while (*p && !is_separator(*p)) p++;
        int wlen = p - startw;
        if (wlen >= len && strncmp(startw, prefix, len)==0) {
          char word[64];
          if (wlen >= (int)sizeof(word)) wlen = sizeof(word)-1;
          memcpy(word, startw, wlen); word[wlen] = '\0';
          int exists = 0;
          for (int j=0;j<ac_count;j++) if (strcmp(ac_matches[j], word)==0) {exists=1;break;}
          if (!exists) {
            ac_matches = realloc(ac_matches, sizeof(char*)*(ac_count+1));
            ac_matches[ac_count++] = strdup(word);
          }
        }
      }
    }
    if (ac_count == 0) { insert_char('\t'); ac_reset(); return; }
    ac_active = 1; ac_index = 0; ac_prefixlen = len; ac_startx = start;
  } else {
    while (ac_inserted--) del_char();
    ac_index = (ac_index + 1) % ac_count;
  }
  const char *kw = ac_matches[ac_index];
  int kwlen = strlen(kw);
  ac_inserted = kwlen - ac_prefixlen;
  for (int j=ac_prefixlen; j<kwlen; j++) {
    insert_char(kw[j]);
  }
}

static void command_execute(char *cmd) {
  if (strcmp(cmd, "q") == 0) {
    clear_screen();
    exit(0);
  } else if (strcmp(cmd, "w") == 0) {
    save_file();
  } else if (strcmp(cmd, "bn") == 0) {
    switch_buffer((cur_buffer + 1) % num_buffers);
  } else if (strcmp(cmd, "bp") == 0) {
    switch_buffer((cur_buffer - 1 + num_buffers) % num_buffers);
  } else if (strcmp(cmd, "bd") == 0) {
    close_current_buffer();
  } else if (!strncmp(cmd, "e ", 2)) {
    open_new_file(cmd + 2, 0);
  } else if (strcmp(cmd, "help") == 0) {
    open_new_file("help.txt", 1);
  } else if (strcmp(cmd, "noh") == 0) {
    clear_search();
  } else if (strncmp(cmd, "s/", 2) == 0) {
    char *pat = cmd + 2;
    char *p = strchr(pat, '/');
    if (p) {
      *p = '\0';
      char *repl = p + 1;
      char *end = strchr(repl, '/');
      if (end) {
        *end = '\0';
        substitute(pat, repl);
      }
    }
  }
}

void command_mode() {
  char *cmd = show_prompt(":%s", NULL);
  if (cmd) {
    command_execute(cmd);
    free(cmd);
  }
}

void search_mode() {
  char *pat = show_prompt("/%s", NULL);
  if (!pat) return;
  clear_search();
  if (regcomp(&search_regex, pat, REG_EXTENDED)) {
    free(pat);
    return;
  }
  search_query = pat;
  search_active = 1;
  for (int i = 0; i < E.numrows; i++) highlight_search(&E.row[i]);
  search_next(1);
}

void search_next(int dir) {
  if (!search_active) return;
  int r = E.cy;
  int c = E.cx + (dir == 1 ? 1 : -1);
  if (dir == 1) {
    for (; r < E.numrows; r++) {
      char *start = E.row[r].chars;
      if (r == E.cy && c > 0) start += c;
      regmatch_t m;
      if (regexec(&search_regex, start, 1, &m, 0) == 0) {
        E.cy = r;
        E.cx = (start - E.row[r].chars) + m.rm_so;
        E.rowoff = E.numrows;
        return;
      }
      c = 0;
    }
  } else {
    for (; r >= 0; r--) {
      char *text = E.row[r].chars;
      char *p = text;
      char *last = NULL;
      regmatch_t m;
      while (regexec(&search_regex, p, 1, &m, 0) == 0) {
        if (r == E.cy && p - text + m.rm_so >= c) break;
        last = p + m.rm_so;
        p += m.rm_eo;
      }
      if (last) {
        E.cy = r;
        E.cx = last - text;
        E.rowoff = E.numrows;
        return;
      }
      c = INT_MAX;
    }
  }
}

void substitute(char *pat, char *repl) {
  regex_t reg;
  clear_search();
  if (regcomp(&reg, pat, REG_EXTENDED)) return;
  for (int r = 0; r < E.numrows; r++) {
    erow *row = &E.row[r];
    char *p = row->chars;
    char *out = NULL;
    size_t outlen = 0;
    int replaced = 0;
    regmatch_t m;
    while (regexec(&reg, p, 1, &m, 0) == 0) {
      replaced = 1;
      out = realloc(out, outlen + m.rm_so + strlen(repl) + 1);
      memcpy(out + outlen, p, m.rm_so);
      outlen += m.rm_so;
      memcpy(out + outlen, repl, strlen(repl));
      outlen += strlen(repl);
      p += m.rm_eo;
    }
    if (replaced) {
      size_t remain = strlen(p);
      out = realloc(out, outlen + remain + 1);
      memcpy(out + outlen, p, remain);
      outlen += remain;
      out[outlen] = '\0';
      free(row->chars);
      row->chars = out;
      row->size = outlen;
      update_row(row);
    } else {
      free(out);
    }
  }
  regfree(&reg);
  if (regcomp(&search_regex, repl, REG_EXTENDED) == 0) {
    search_query = strdup(repl);
    search_active = 1;
    for (int i = 0; i < E.numrows; i++) highlight_search(&E.row[i]);
  } else {
    search_active = 0;
  }
}
