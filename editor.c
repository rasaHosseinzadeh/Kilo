#include "editor.h"
#include "terminal.h" // For die()

struct editor_config E;

struct editorBuffer *get_active_buffer() {
  if (E.active_buffer < 0 || E.active_buffer >= E.num_buffers) {
    return NULL; // Or handle error appropriately
  }
  return E.buffers[E.active_buffer];
}

// Forward declaration for syntax highlighting update function
void editorUpdateRowSyntax(struct editorBuffer *buffer, erow *row);

// Forward declarations for row operations used in autocomplete
void row_del_char(erow *row, int at);
void insert_char(int c);


// --- Syntax Highlighting Data & Functions ---

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case", "default",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "const|", "volatile|", "extern|", "sizeof|", "NULL",
    "#include", "#define", "#if", "#ifdef", "#ifndef", "#else", "#elif", "#endif",
    NULL};

char *Python_HL_extensions[] = {".py", ".pyw", NULL};
char *Python_HL_keywords[] = {
    // Keywords
    "def", "class", "if", "elif", "else", "for", "while", "return", "import", "from",
    "try", "except", "finally", "with", "as", "pass", "break", "continue", "yield",
    "lambda", "global", "nonlocal", "assert", "del", "in", "is", "not", "and", "or",
    // Built-in constants/types (marked with | for HL_KEYWORD2)
    "True|", "False|", "None|", "int|", "str|", "list|", "dict|", "tuple|", "set|",
    "self|", "cls|", "print|", "len|", "range|", "super|", "isinstance|", "type|",
    NULL};

struct editorSyntax HLDB[] = {
    {"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"python", Python_HL_extensions, Python_HL_keywords, "#", "\"\"\"", "\"\"\"",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}", c) != NULL;
}

void editorSelectSyntaxHighlight(struct editorBuffer *buf) {
  fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: Processing filename: %s\n", buf->filename ? buf->filename : "NULL");
  buf->syntax = NULL;
  if (buf->filename == NULL) {
    fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: Filename is NULL, syntax not set.\n");
    return;
  }

  char *ext = strrchr(buf->filename, '.');
  if (!ext) {
    fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: No extension found for %s, syntax not set.\n", buf->filename);
    return;
  }
  fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: Found extension '%s' for %s\n", ext, buf->filename);

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: Comparing ext '%s' with HLDB filematch '%s' for filetype '%s'\n", ext, s->filematch[i], s->filetype);
      if (!strcmp(ext, s->filematch[i])) {
        buf->syntax = s;
        fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: Matched! Set syntax to '%s' for %s\n", s->filetype, buf->filename);
        // When syntax changes, all rows need re-highlighting (done by draw_rows)
        return;
      }
      i++;
    }
  }
  fprintf(stderr, "DEBUG: editorSelectSyntaxHighlight: No syntax profile matched for %s. buf->syntax remains NULL.\n", buf->filename);
}

// --- End Syntax Highlighting ---

void init_buffer(struct editorBuffer *buf) {
  buf->row = NULL;
  buf->rowoff = 0;
  buf->coloff = 0;
  buf->numrows = 0;
  buf->cx = 0;
  buf->cy = 0;
  buf->filename = NULL;
  buf->dirty = 0;
  buf->syntax = NULL; // Initialize syntax pointer
}

void init() {
  E.active_buffer = 0;
  E.num_buffers = 0;
  for (int i = 0; i < MAX_BUFFERS; ++i) {
    E.buffers[i] = NULL;
  }

  // Create initial empty buffer
  if (E.num_buffers < MAX_BUFFERS) {
    E.buffers[E.num_buffers] = malloc(sizeof(struct editorBuffer));
    if (E.buffers[E.num_buffers] == NULL) {
      die("malloc failed for initial buffer");
    }
    init_buffer(E.buffers[E.num_buffers]);
    E.active_buffer = E.num_buffers;
    E.num_buffers++;
  } else {
    die("Max buffers reached during init"); // Should not happen
  }

  enable_raw_mode();
  if (get_window_size(&E.screen_rows, &E.screen_cols) == -1) {
    die("get_windows_size");
  }
  E.screen_rows -= 3; // For tab bar, status bar, and message bar
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  // Initialize autocomplete fields
  E.suggestions = NULL;
  E.num_suggestions = 0;
  E.active_suggestion_idx = 0;
  E.autocomplete_active = 0;
  E.ac_trigger_cx = 0;
  E.ac_trigger_cy = 0;
  E.ac_prefix_len = 0;
}

// --- Autocomplete Functions ---

void editorClearAutocomplete() {
  if (E.suggestions) {
    for (int i = 0; i < E.num_suggestions; i++) {
      free(E.suggestions[i]);
    }
    free(E.suggestions);
    E.suggestions = NULL;
  }
  E.num_suggestions = 0;
  E.active_suggestion_idx = 0;
  E.autocomplete_active = 0;
  E.ac_trigger_cx = 0;
  E.ac_trigger_cy = 0;
  E.ac_prefix_len = 0;
}

// Extracts the word prefix at the cursor.
// Returns a new string (caller must free) or NULL.
// *prefix_len and *prefix_start_cx are out parameters.
char *get_word_prefix_at_cursor(erow *row, int cx, int *prefix_len_out, int *prefix_start_cx_out) {
  if (!row || cx == 0) return NULL;

  int start = cx;
  while (start > 0 && !is_separator(row->chars[start - 1])) {
    start--;
  }

  if (start == cx) return NULL; // No prefix or cursor is at start of word already

  *prefix_len_out = cx - start;
  *prefix_start_cx_out = start;

  char *prefix = malloc(*prefix_len_out + 1);
  if (!prefix) return NULL;
  memcpy(prefix, &row->chars[start], *prefix_len_out);
  prefix[*prefix_len_out] = '\0';
  return prefix;
}

// Basic suggestion adding, no sorting or advanced duplicate checking for now
#define MAX_SUGGESTIONS 50
void add_suggestion(const char *suggestion_text, int len) {
  if (E.num_suggestions >= MAX_SUGGESTIONS) return;

  // Avoid adding exact duplicate of existing suggestions
  for (int i = 0; i < E.num_suggestions; i++) {
    if (strncmp(E.suggestions[i], suggestion_text, len) == 0 && E.suggestions[i][len] == '\0') {
      return;
    }
  }
  
  char **new_suggestions = realloc(E.suggestions, sizeof(char *) * (E.num_suggestions + 1));
  if (!new_suggestions) return; // Failed to allocate
  E.suggestions = new_suggestions;

  E.suggestions[E.num_suggestions] = malloc(len + 1);
  if (!E.suggestions[E.num_suggestions]) {
      // Could try to shrink E.suggestions back, but for now, just fail to add
      return;
  }
  memcpy(E.suggestions[E.num_suggestions], suggestion_text, len);
  E.suggestions[E.num_suggestions][len] = '\0';
  E.num_suggestions++;
}


void editorGenerateSuggestions() {
  editorClearAutocomplete();
  struct editorBuffer *buf = get_active_buffer();
  if (!buf || buf->cy >= buf->numrows) return;

  erow *row = &buf->row[buf->cy];
  int prefix_start_cx;
  char *prefix = get_word_prefix_at_cursor(row, buf->cx, &E.ac_prefix_len, &prefix_start_cx);

  if (!prefix || E.ac_prefix_len < 2) { // Minimum prefix length
    free(prefix);
    return;
  }

  E.ac_trigger_cx = buf->cx;
  E.ac_trigger_cy = buf->cy;

  // Scan current buffer for words starting with prefix
  for (int i = 0; i < buf->numrows; i++) {
    erow *scan_row = &buf->row[i];
    for (int j = 0; j < scan_row->size;) {
      if (is_separator(scan_row->chars[j])) {
        j++;
        continue;
      }
      int word_start = j;
      while (j < scan_row->size && !is_separator(scan_row->chars[j])) {
        j++;
      }
      int word_len = j - word_start;
      if (word_len > E.ac_prefix_len && word_len < 50 && // Arbitrary max suggestion len
          strncmp(prefix, &scan_row->chars[word_start], E.ac_prefix_len) == 0) {
          // Check if the found word is not the prefix itself
          if (word_len != E.ac_prefix_len || strncmp(prefix, &scan_row->chars[word_start], E.ac_prefix_len) != 0) {
             add_suggestion(&scan_row->chars[word_start], word_len);
          }
      }
    }
  }
  
  // Also add syntax keywords if they match
  if(buf->syntax && buf->syntax->keywords) {
      char **keywords = buf->syntax->keywords;
      for(int k=0; keywords[k]; ++k) {
          int kw_len = strlen(keywords[k]);
          // int is_type_kw = 0; // This was unused, removing it.
          if(keywords[k][kw_len-1] == '|') { // remove marker for type keywords
              kw_len--; 
              // is_type_kw = 1; // This was unused
          }
          // Check if keyword itself starts with prefix and is longer than prefix
          if (kw_len > E.ac_prefix_len && strncmp(prefix, keywords[k], E.ac_prefix_len) == 0) {
              char temp_kw[kw_len+1];
              strncpy(temp_kw, keywords[k], kw_len);
              temp_kw[kw_len] = '\0';
              add_suggestion(temp_kw, kw_len);
          }
      }
  }


  free(prefix);
  if (E.num_suggestions > 0) {
    E.autocomplete_active = 1;
    E.active_suggestion_idx = 0;
  } else {
    editorClearAutocomplete(); // No suggestions found, clear any partial state
  }
}

// --- End Autocomplete Functions ---

void editorApplySuggestion() {
  if (!E.autocomplete_active || E.num_suggestions == 0 || !E.suggestions ||
      E.active_suggestion_idx < 0 || E.active_suggestion_idx >= E.num_suggestions) {
    editorClearAutocomplete();
    return;
  }

  struct editorBuffer *buf = get_active_buffer();
  if (!buf) {
    editorClearAutocomplete();
    return;
  }

  char *suggestion = E.suggestions[E.active_suggestion_idx];
  int suggestion_len = strlen(suggestion);

  // Move cursor to start of the prefix
  buf->cx = E.ac_trigger_cx - E.ac_prefix_len;
  buf->cy = E.ac_trigger_cy; 
  
  // Ensure the target row is valid
  if (buf->cy >= buf->numrows) {
      editorClearAutocomplete();
      return;
  }
  erow *row = &buf->row[buf->cy];

  // Delete the prefix
  for (int i = 0; i < E.ac_prefix_len; i++) {
    // Check if cx is at the end of the line or beyond row size
    if (buf->cx >= row->size) break; 
    row_del_char(row, buf->cx); 
    // row_del_char does not increment cx, it removes char at cx.
    // So, if prefix is "abc" and cx is at 'c' (index 2), 
    // ac_trigger_cx = 3, ac_prefix_len = 3.
    // buf->cx becomes 3-3=0.
    // Delete char at 0, then char at 0, then char at 0.
    // This is wrong. We need to delete from (ac_trigger_cx - ac_prefix_len) up to ac_trigger_cx.
    // The original row_del_char(&buf->row[buf->cy], buf->cx-1) was for deleting char *before* cursor.
    // We need to delete char *at* cursor 'E.ac_prefix_len' times.
  }
  // Corrected prefix deletion:
  // The cursor is already placed at the start of the prefix.
  // We need to delete E.ac_prefix_len characters starting from this position.
  // row_del_char deletes the character at a given index 'at' within the row.
  // So, if buf->cx is the start of the prefix, we delete at buf->cx, E.ac_prefix_len times.
  // Each deletion shifts characters left, so the index remains buf->cx.
  for (int i = 0; i < E.ac_prefix_len; i++) {
      if (buf->cx < row->size) { // Make sure not to delete past end of row
          row_del_char(row, buf->cx); 
      } else {
          break; // Should not happen if prefix was valid
      }
  }


  // Insert the suggestion
  for (int i = 0; i < suggestion_len; i++) {
    insert_char(suggestion[i]); // insert_char increments buf->cx
  }

  editorClearAutocomplete();
  buf->dirty = 1;
}


#define AC_BOX_MAX_HEIGHT 5
void draw_autocomplete_box(struct abuf *ab) {
  if (!E.autocomplete_active || E.num_suggestions == 0) return;

  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  // Calculate position of the box - below the trigger cursor line
  // Physical screen row for text input area starts at 2 (1 for tabs, 1 for 1-based indexing).
  // Cursor's visual y on screen, relative to text area start: (E.ac_trigger_cy - buf->rowoff)
  // Absolute screen line of cursor: (E.ac_trigger_cy - buf->rowoff) + 1 (for 1-based) + 1 (for tab bar)
  // Box starts on the line *after* this.
  int box_start_y = (E.ac_trigger_cy - buf->rowoff) + 1 + 1 + 1;
  
  // Ensure box doesn't go off screen, adjust upwards if needed
  int box_height = E.num_suggestions > AC_BOX_MAX_HEIGHT ? AC_BOX_MAX_HEIGHT : E.num_suggestions;
  if (box_start_y + box_height > E.screen_rows + 2) { // +2 because E.screen_rows is text area, not total physical rows
      box_start_y = (E.ac_trigger_cy - buf->rowoff) + 1 + 1 - box_height;
      if (box_start_y < 2) box_start_y = 2; // Don't draw over tab bar
  }


  // Calculate box_start_x based on where prefix started
  // (E.ac_trigger_cx - E.ac_prefix_len - buf->coloff) is 0-indexed on screen.
  // +1 for 1-based indexing.
  int box_start_x = (E.ac_trigger_cx - E.ac_prefix_len - buf->coloff) + 1;
  if (box_start_x < 1) box_start_x = 1;


  int max_suggestion_width = 0;
  for (int i = 0; i < E.num_suggestions; i++) {
    int len = strlen(E.suggestions[i]);
    if (len > max_suggestion_width) max_suggestion_width = len;
  }
  max_suggestion_width += 2; // For padding " "
  if (box_start_x + max_suggestion_width > E.screen_cols) {
      max_suggestion_width = E.screen_cols - box_start_x;
  }
  if (max_suggestion_width < 5) max_suggestion_width = 5; // Minimum width


  for (int i = 0; i < box_height; i++) {
    int suggestion_idx = i; // Could add scrolling in suggestions list later
    if (suggestion_idx >= E.num_suggestions) break;

    char move_cursor_cmd[32];
    snprintf(move_cursor_cmd, sizeof(move_cursor_cmd), "\x1b[%d;%dH", box_start_y + i, box_start_x);
    ab_append(ab, move_cursor_cmd, strlen(move_cursor_cmd));

    if (suggestion_idx == E.active_suggestion_idx) {
      ab_append(ab, "\x1b[7m", 4); // Inverse video for selected item
    }
    
    char text[max_suggestion_width +1];
    snprintf(text, sizeof(text), " %-*.*s", max_suggestion_width-2, max_suggestion_width-2, E.suggestions[suggestion_idx]);
    ab_append(ab, text, strlen(text));

    if (suggestion_idx == E.active_suggestion_idx) {
      ab_append(ab, "\x1b[m", 3); // Reset video
    }
    // No need to clear rest of line if box width is controlled
    // ab_append(ab, "\x1b[K", 3); 
  }
}


void insert_row(int at, char *s, size_t len) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (at < 0 || at > buf->numrows) {
    return;
  }
  buf->row = realloc(buf->row, sizeof(erow) * (buf->numrows + 1));
  memmove(&buf->row[at + 1], &buf->row[at], (buf->numrows - at) * sizeof(erow));
  buf->row[at].size = len;
  buf->row[at].chars = malloc(len + 1);
  memcpy(buf->row[at].chars, s, len);
  buf->row[at].chars[len] = '\0';

  buf->row[at].hl = malloc(len + 1); // Allocate for highlight bytes
  memset(buf->row[at].hl, HL_NORMAL, len + 1); // Initialize to normal
  // editorUpdateRowSyntax(&buf->row[at]); // Will be called by draw_rows or when needed

  buf->numrows++;
  buf->dirty = 1;
}

void free_row_hl(erow *row) {
    free(row->hl);
}

void open_file(char *filename) {
  struct editorBuffer *current_buf = get_active_buffer();

  // If current buffer is empty, not dirty, and has no filename, use it.
  // Otherwise, try to create a new buffer.
  if (current_buf && current_buf->numrows == 0 && !current_buf->dirty && !current_buf->filename) {
    // Use the current empty buffer
    free(current_buf->filename); // Should be NULL, but just in case
  } else {
    // Create a new buffer if we haven't reached the max
    if (E.num_buffers >= MAX_BUFFERS) {
      set_status_message("Cannot open more files: Maximum buffers reached.");
      return;
    }
    E.buffers[E.num_buffers] = malloc(sizeof(struct editorBuffer));
    if (E.buffers[E.num_buffers] == NULL) {
      die("malloc failed for new buffer");
    }
    init_buffer(E.buffers[E.num_buffers]);
    E.active_buffer = E.num_buffers;
    E.num_buffers++;
    current_buf = get_active_buffer(); // Update current_buf to the new one
    if (!current_buf) { // Should not happen
        die("Failed to get newly created active buffer");
    }
  }

  current_buf->filename = strdup(filename);
  FILE *fp = fopen(filename, "a"); // Ensure the file exists
  if (!fp) {
    // Keep the buffer, but show an error. User might want to save it as a new file.
    set_status_message("fopen error: %s", strerror(errno));
    return;
  }
  fclose(fp);

  fp = fopen(filename, "r");
  if (!fp) {
    set_status_message("fopen error: %s", strerror(errno));
    return;
  }

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    // insert_row already uses the active buffer
    insert_row(current_buf->numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  current_buf->dirty = 0; // File just loaded or created
  fprintf(stderr, "DEBUG: open_file: Calling editorSelectSyntaxHighlight for filename: %s\n", current_buf->filename);
  editorSelectSyntaxHighlight(current_buf);
}

void insert_enter() {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (buf->cx == 0) {
    insert_row(buf->cy, "", 0);
  } else {
    erow *row = &buf->row[buf->cy];
    insert_row(buf->cy + 1, &row->chars[buf->cx], row->size - buf->cx);
    row = &buf->row[buf->cy]; // previous refrence is invalid as insert_row callls
                        // realloc
    row->size = buf->cx;
    row->chars[buf->cx] = '\0';
  }
  buf->cx = 0;
  buf->cy++;
}

void row_insert_char(erow *row, int at, int c) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (at < 0 || at > row->size) {
    at = row->size;
  }
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  buf->dirty = 1;
}

void append_string_to_row(erow *row, char *s, size_t len) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  buf->dirty = 1;
}

void del_row(int at) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (at <= 0 || at >= buf->numrows) { // Note: original was at < 0, but deleting row 0 should be possible if cy > 0
    return;
  }
  free(buf->row[at].chars);
  free_row_hl(&buf->row[at]); // Free hl memory for the deleted row
  memmove(&buf->row[at], &buf->row[at + 1], (buf->numrows - at - 1) * sizeof(erow));
  buf->numrows -= 1;
  buf->dirty = 1;
}

void row_del_char(erow *row, int at) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (at < 0 || at >= row->size) {
    return;
  }
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size -= 1;
  buf->dirty = 1;
}

void insert_char(int c) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (buf->cy == buf->numrows) {
    insert_row(buf->numrows, "", 0);
  }
  row_insert_char(&buf->row[buf->cy], buf->cx++, c);
}

void del_char() {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;
  if (buf->cy == buf->numrows) {
    return;
  }
  erow *row = &buf->row[buf->cy];
  if (buf->cx > 0) {
    row_del_char(row, buf->cx - 1);
    buf->cx -= 1;
  } else if (buf->cx == 0 && buf->cy > 0) {
    buf->cx = buf->row[buf->cy - 1].size;
    append_string_to_row(&buf->row[buf->cy - 1], row->chars, row->size);
    del_row(buf->cy);
    buf->cy -= 1;
  }
}

char *rows2string(int *buflen) {
  struct editorBuffer *buf_active = get_active_buffer();
  if (!buf_active) {
      *buflen = 0;
      return NULL;
  }
  int totlen = 0;
  int j;
  for (j = 0; j < buf_active->numrows; ++j) {
    totlen += buf_active->row[j].size + 1;
  }
  *buflen = totlen;
  char *buf_str = malloc(totlen +1); // +1 for potential empty file null terminator
  if (!buf_str) {
      *buflen = 0;
      return NULL;
  }
  char *p = buf_str;
  for (j = 0; j < buf_active->numrows; ++j) {
    memcpy(p, buf_active->row[j].chars, buf_active->row[j].size);
    p += buf_active->row[j].size;
    *p = '\n';
    p++;
  }
  *p = '\0'; // Ensure null termination even if empty
  return buf_str;
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
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  char *fname_from_prompt = NULL; // Declare here to manage its lifecycle

  if (buf->filename == NULL) {
    fname_from_prompt = show_prompt("Save as: %s", NULL);
    if (fname_from_prompt == NULL) { // User cancelled
        set_status_message("Save aborted.");
        return;
    }
    // User provided a new filename
    free(buf->filename); // Free old filename if any (should be NULL here)
    buf->filename = fname_from_prompt; // Ownership of malloc'd string transferred
    fname_from_prompt = NULL; // Avoid double free later

    fprintf(stderr, "DEBUG: save_file (Save As): Calling editorSelectSyntaxHighlight for new filename: %s\n", buf->filename);
    editorSelectSyntaxHighlight(buf);
  }
  // If fname_from_prompt was allocated (because buf->filename was initially NULL) but not used to set buf->filename 
  // (e.g. if logic changes or error before assignment), it should be freed.
  // However, with current logic, if fname_from_prompt is not NULL here, it means it was assigned to buf->filename.
  // If it is NULL, it was either not allocated or already assigned and nulled.
  // So, explicit free here is not needed IF logic above is strict.
  // Let's be safe: if fname_from_prompt is still non-NULL here, it means it wasn't consumed.
  if (fname_from_prompt) {
      free(fname_from_prompt); // Should not happen with current logic path
  }


  int len;
  int err = 0;
  char *buf_str = rows2string(&len);
  if (buf_str == NULL && len == 0 && buf->numrows > 0) { // Malloc failure in rows2string
      set_status_message("Cannot save! Malloc error.");
      return;
  }


  int fd = open(buf->filename, O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    err = 1;
    goto cleanup;
  }
  // Check if len is 0 for empty file, ftruncate should be fine
  if (ftruncate(fd, len) == -1) {
    err = 1;
    goto cleanup;
  }
  if (len > 0 && write(fd, buf_str, len) != len) { // Only write if there's content
    err = 1;
    goto cleanup;
  } else if (len == 0 && write(fd, "", 0) != 0) { // Handle writing 0 bytes for empty file explicitly if needed
    // Some systems might behave differently for 0-byte writes.
    // However, ftruncate should have already set the file size.
  }


cleanup:
  if (fd != -1) close(fd); // Ensure fd is closed if it was opened
  free(buf_str); // Free the string from rows2string

  if (err) {
    set_status_message("Can't save! I/O error: %s", strerror(errno));
  } else {
    buf->dirty = 0;
    set_status_message("Wrote %d bytes to %s", len, buf->filename);
  }
}

void find_callback(char *query, int c) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  static int last_match = -1; // TODO: This should be per-buffer
  static int direction = 1;   // TODO: This should be per-buffer

  if (c == '\r' || c == '\x1b') { // Enter or Escape
    last_match = -1;
    direction = 1;
    return;
  } else if (c == ARROW_DOWN || c == ARROW_RIGHT) {
    direction = 1;
  } else if (c == ARROW_UP || c == ARROW_LEFT) {
    direction = -1;
  } else { // New search query
    direction = 1;
    last_match = -1;
  }

  if (last_match == -1) direction = 1; // Start from beginning or current pos based on direction logic

  int current = last_match;
  for (int i = 0; i < buf->numrows; ++i) {
    current += direction;
    if (current == -1) {
      current = buf->numrows - 1;
    } else if (current == buf->numrows) {
      current = 0;
    }

    erow *row = &buf->row[current];
    char *match = strstr(row->chars, query);
    if (match) {
      last_match = current;
      buf->cy = current;
      buf->cx = match - row->chars;
      buf->rowoff = buf->numrows; // This forces scroll to show the match, might need refinement
      set_status_message("Found at %d:%d", buf->cy, buf->cx);
      return; // Found
    }
  }
  // If no match found after checking all rows
  set_status_message("'%s' not found", query);
}

void find() {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  int bkupx = buf->cx, bkupy = buf->cy;
  int bkupcoloff = buf->coloff, bkuprowoff = buf->rowoff;

  char *query = show_prompt("Search: %s (ESC/Enter to cancel/confirm, Arrows to navigate)", find_callback);

  if (query) {
    // If find_callback found something, cursor is already updated.
    // If user just typed and pressed Enter, find_callback was called with '\r'.
    // If find_callback found nothing, or user Escaped, restore.
    // The static vars in find_callback make this tricky.
    // For now, if query is not NULL, we assume find_callback handled it or user confirmed.
    free(query);
  } else { // User pressed ESC in show_prompt
    buf->cx = bkupx;
    buf->cy = bkupy;
    buf->coloff = bkupcoloff;
    buf->rowoff = bkuprowoff;
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
void move_cursor(int key) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  erow *row = (buf->cy >= buf->numrows) ? NULL : &buf->row[buf->cy]; // Check against numrows

  switch (key) {
  case ARROW_LEFT:
    if (buf->cx != 0) {
      buf->cx--;
    } else if (buf->cy > 0) { // Move to end of previous line
      buf->cy--;
      buf->cx = buf->row[buf->cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && buf->cx < row->size) {
      buf->cx++;
    } else if (row && buf->cx == row->size && buf->cy < buf->numrows -1) { // Move to start of next line
        buf->cy++;
        buf->cx = 0;
    }
    break;
  case ARROW_UP:
    if (buf->cy != 0) {
      buf->cy--;
    }
    break;
  case ARROW_DOWN:
    if (buf->cy < buf->numrows -1) { // Make sure not to go past last line content
      buf->cy++;
    } else if (buf->cy == buf->numrows -1 && buf->numrows > 0) { // if on last line, allow to go to numrows for new line
         buf->cy = buf->numrows; // Allow cursor to be one past last line
    }
    break;
  }

  // Snap cursor to end of line if past it
  row = (buf->cy >= buf->numrows) ? NULL : &buf->row[buf->cy];
  int rowlen = row ? row->size : 0;
  if (buf->cx > rowlen) {
    buf->cx = rowlen;
  }
}

void draw_status_bar(struct abuf *ab) {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  ab_append(ab, "\x1b[7m", 4); // Select graphic rendition: inverse video
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     buf->filename ? buf->filename : "[No Name]", buf->numrows,
                     buf->dirty ? "(modified)" : "");
  // Display current buffer index and total buffers if more than one
  char buf_info[32] = "";
  // Ensure E.num_buffers is checked before accessing E.buffers[E.active_buffer] implicitly via get_active_buffer()
  // struct editorBuffer *buf = get_active_buffer(); is already at the start of function
  if (E.num_buffers > 0 && buf) { 
    snprintf(buf_info, sizeof(buf_info), "Buf %d/%d", E.active_buffer + 1, E.num_buffers);
  }
  
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %s | %d/%d",
                     buf->syntax ? buf->syntax->filetype : "no ft", // Display filetype
                     buf_info, 
                     buf->cy + 1, buf->numrows > 0 ? buf->numrows:1); 

  if (len > E.screen_cols) len = E.screen_cols;
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
  ab_append(ab, "\x1b[m", 3); // Reset graphic rendition
  ab_append(ab, "\r\n", 2); // Use \r\n instead of \n\r for message bar next
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
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  for (int y_screen = 0; y_screen < E.screen_rows; ++y_screen) {
    int filerow = y_screen + buf->rowoff;
    if (filerow >= buf->numrows) {
      if (buf->numrows == 0 && y_screen == E.screen_rows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", "0.0.1");
        if (welcomelen > E.screen_cols) welcomelen = E.screen_cols;
        int padding = (E.screen_cols - welcomelen) / 2;
        if (padding) {
          ab_append(ab, "~", 1);
          padding--;
        }
        while (padding--) ab_append(ab, " ", 1);
        ab_append(ab, welcome, welcomelen);
      } else {
        ab_append(ab, "~", 1);
      }
    } else {
      editorUpdateRowSyntax(buf, &buf->row[filerow]); // Update syntax for the current row

      int len = buf->row[filerow].size - buf->coloff;
      if (len < 0) len = 0;
      if (len > E.screen_cols) len = E.screen_cols;
      
      char *c = &buf->row[filerow].chars[buf->coloff];
      unsigned char *hl = &buf->row[filerow].hl[buf->coloff];
      int current_color = -1;

      for (int j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            ab_append(ab, "\x1b[39m", 5); // Reset to default text color
            current_color = -1;
          }
          ab_append(ab, &c[j], 1);
        } else {
          int hl_type = hl[j]; 
          char color_escape[16];
          int apply_color_change = 0;

          if (hl_type == HL_KEYWORD1) {
            if (current_color != 91) { // Bright Red
              snprintf(color_escape, sizeof(color_escape), "\x1b[91m");
              current_color = 91;
              apply_color_change = 1;
            }
          } else if (hl_type == HL_KEYWORD2) {
            if (current_color != 92) { // Bright Green
              snprintf(color_escape, sizeof(color_escape), "\x1b[92m");
              current_color = 92;
              apply_color_change = 1;
            }
          } else if (hl_type != HL_NORMAL) { // Other syntax elements like comments, strings, numbers
             if (current_color != hl_type) { // Use their defined value directly
                snprintf(color_escape, sizeof(color_escape), "\x1b[%dm", hl_type);
                current_color = hl_type;
                apply_color_change = 1;
             }
          }
          // Note: HL_NORMAL case is handled by the outer if (hl[j] == HL_NORMAL)

          if (apply_color_change) {
            ab_append(ab, color_escape, strlen(color_escape));
          }
          ab_append(ab, &c[j], 1);
        }
      }
      if (current_color != -1) { // Reset color at end of line if a color was active
         ab_append(ab, "\x1b[39m", 5);
      }
    }
    ab_append(ab, "\x1b[K", 3); // Clear rest of the line
    ab_append(ab, "\r\n", 2);
  }
}


void editorUpdateRowSyntax(struct editorBuffer *buffer, erow *row) {
    if (!buffer->syntax) { // No syntax definition for this filetype
        // If hl is already allocated, fill with HL_NORMAL, otherwise allocate and fill
        if (row->hl && row->size > 0) { // Check if row->hl is allocated and row has content
             memset(row->hl, HL_NORMAL, row->size);
        } else if (!row->hl && row->size > 0) { // Should not happen if insert_row allocates hl
            row->hl = malloc(row->size +1);
            if(row->hl) memset(row->hl, HL_NORMAL, row->size); else return; // Malloc failed
        } else if (row->size == 0 && row->hl) { // Empty row, ensure hl is minimal if exists
            row->hl[0] = HL_NORMAL;
        } else if (row->size == 0 && !row->hl) { // Empty row, no hl (e.g. new row not yet fully processed by insert_row)
             // This case should ideally be handled by insert_row ensuring hl exists.
             // For safety, if hl is NULL for a 0-size row, we can't do much.
        }
        return;
    }

    // Ensure hl is allocated
    if (row->hl == NULL) { // Should be allocated by insert_row
        row->hl = malloc(row->size + 1);
        if (!row->hl) return; // Malloc failed
    }
    memset(row->hl, HL_NORMAL, row->size); // Default to normal

    struct editorSyntax *syntax = buffer->syntax;
    char **keywords = syntax->keywords;

    char *scs = syntax->singleline_comment_start;
    char *mcs = syntax->multiline_comment_start;
    char *mce = syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int i = 0;
    // TODO: Add multiline comment state persistence (in_comment)
    // For now, multiline comments are only detected if they start on the current line.

    while (i < row->size) {
        char c = row->chars[i];

        // Single-line comment
        if (scs_len && strncmp(&row->chars[i], scs, scs_len) == 0) {
            for (int j = i; j < row->size; j++) row->hl[j] = HL_COMMENT;
            return; // Rest of the line is a comment
        }

        // Multi-line comment
        if (mcs_len && mce_len && strncmp(&row->chars[i], mcs, mcs_len) == 0) {
            int comment_end_found = 0;
            for (int j = i; j < row->size; j++) {
                row->hl[j] = HL_COMMENT;
                if (strncmp(&row->chars[j], mce, mce_len) == 0 && (j + mce_len <= row->size) ) {
                    for(int k=0; k < mce_len; ++k) row->hl[j+k] = HL_COMMENT; // Color the end token as well
                    i = j + mce_len;
                    comment_end_found = 1;
                    break; 
                }
            }
            if(comment_end_found) continue; else return; // If no end, rest of line is comment
        }

        // Strings
        if (syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (c == '"' || c == '\'') {
                char string_char = c;
                row->hl[i++] = HL_STRING; // Opening quote
                while (i < row->size && row->chars[i] != string_char) {
                    if (row->chars[i] == '\\' && i + 1 < row->size) { // Handle escape
                        row->hl[i++] = HL_STRING;
                        row->hl[i++] = HL_STRING;
                    } else {
                        row->hl[i++] = HL_STRING;
                    }
                }
                if (i < row->size) row->hl[i++] = HL_STRING; // Closing quote
                continue;
            }
        }

        // Numbers
        if (syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if (isdigit(c) || (c == '.' && i + 1 < row->size && isdigit(row->chars[i+1]))) {
                 row->hl[i] = HL_NUMBER;
                 i++;
                 while(i < row->size && (isdigit(row->chars[i]) || row->chars[i] == '.')) {
                     row->hl[i] = HL_NUMBER;
                     i++;
                 }
                 continue;
            }
        }
        
        // Keywords
        if (keywords) {
            int j = 0;
            while(keywords[j]) {
                int klen = strlen(keywords[j]);
                int is_kw2 = keywords[j][klen-1] == '|'; // Keyword type 2 (e.g. int|)
                if(is_kw2) klen--;

                if (i + klen <= row->size && strncmp(&row->chars[i], keywords[j], klen) == 0 &&
                    is_separator(row->chars[i+klen]) && (i == 0 || is_separator(row->chars[i-1])) ) {
                    for(int k=0; k < klen; k++) {
                        row->hl[i+k] = is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1;
                    }
                    i += klen;
                    goto next_char_in_row; // continue outer while loop
                }
                j++;
            }
        }

        i++; // Move to next character if no specific rule matched
        next_char_in_row:;
    }
}


void scroll() {
  struct editorBuffer *buf = get_active_buffer();
  if (!buf) return;

  // Vertical scroll
  if (buf->cy < buf->rowoff) {
    buf->rowoff = buf->cy;
  }
  if (buf->cy >= buf->rowoff + E.screen_rows) {
    buf->rowoff = buf->cy - E.screen_rows + 1;
  }
  // Horizontal scroll
  if (buf->cx < buf->coloff) {
    buf->coloff = buf->cx;
  }
  if (buf->cx >= buf->coloff + E.screen_cols) {
    buf->coloff = buf->cx - E.screen_cols + 1;
  }
}

void refresh_screen() {
  struct editorBuffer *buf = get_active_buffer();
  // If no buffer (e.g. init failed, or some other logic error), can't refresh.
  // This should ideally not happen if init guarantees a buffer.
  if (!buf && E.num_buffers == 0) {
      // Maybe call die() or show a critical error message?
      // For now, let's try to ensure init always makes a buffer.
      // If it's mid-operation and buffer becomes null, that's a bug.
      return; 
  }
   if (!buf && E.active_buffer >=0 && E.active_buffer < E.num_buffers) {
      // This means E.buffers[E.active_buffer] is NULL when it shouldn't be.
      // This is a critical error.
      die("Active buffer is NULL unexpectedly in refresh_screen");
      return;
  }
  // if !buf and E.num_buffers > 0, but active_buffer is somehow invalid.
  // This also indicates a bug in buffer management.
  if (!buf) {
      // Attempt to recover or default to a state?
      // For now, let's assume get_active_buffer() works or we die earlier.
      // If there are no buffers at all (e.g. after a close all), then behavior might differ.
      // But the prompt says "editor should still behave like a single-file editor".
      // So there should always be at least one buffer.
      // If `init` ensures one buffer, and `open_file` uses it or adds one,
      // and we don't have buffer closing yet, `buf` should always be valid.
      // Let's add a specific check for the case where `get_active_buffer()` returns NULL
      // when it really shouldn't.
      if(E.num_buffers > 0 && (E.active_buffer < 0 || E.active_buffer >= E.num_buffers)) {
          die("active_buffer index out of bounds in refresh_screen");
          return;
      }
      // If all buffers were somehow closed (not part of this task)
      // then the screen would be blank or show a message.
      // For this task, assume there's always an active buffer.
  }


  scroll(); // Uses get_active_buffer()

  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[?25l", 6); // Hide cursor
  // Move cursor to top-left. Tab bar will be written first, then rows, etc.
  // So \x1b[H is correct for the start of the whole screen refresh.
  ab_append(&ab, "\x1b[H", 3);    

  draw_tab_bar(&ab);      // Draw the new tab bar first
  draw_rows(&ab);         // Uses get_active_buffer(), draws into its allocated screen rows
  draw_status_bar(&ab);   // Uses get_active_buffer()
  draw_message_bar(&ab);  // Uses global E for statusmsg

  // Position cursor: 
  // Tab bar takes 1st row. Text area (draw_rows) starts from 2nd row.
  // So, cursor Y is (cy - rowoff) + 1 (for 1-based) + 1 (for tab_bar offset)
  // Or, more simply, if draw_rows handles its own viewport correctly,
  // the cursor calc within the text area is (cy - rowoff) + 1.
  // We then need to tell terminal to put it on the correct physical screen line.
  // Let's assume draw_rows draws from where the cursor is left after draw_tab_bar.
  // No, draw_rows also uses \x1b[H implicitly by design of overall refresh.
  // The ANSI sequence for cursor positioning is absolute.
  // So if tab bar is line 1, rows start at line 2.
  // (current_buf_for_cursor->cy - current_buf_for_cursor->rowoff) is 0-indexed for the visible part of buffer.
  // +1 to make it 1-indexed for ANSI.
  // +1 again because the text area starts on the 2nd screen line due to the tab bar.
  // Example: cursor at (0,0) in buffer, rowoff=0. Should be on screen line 2. (0-0)+1+1 = 2.
  // Example: cursor at (5,0) in buffer, rowoff=0. Should be on screen line 7. (5-0)+1+1 = 7.
  
  // Draw autocomplete box before status/message bar, but after main text rows.
  draw_autocomplete_box(&ab);

  // Ensure buf is valid before dereferencing for cy, cx, rowoff, coloff
  // get_active_buffer() is called multiple times, consider caching if performance issue
  // but for now, clarity is fine.
  struct editorBuffer *current_buf_for_cursor = get_active_buffer();
  if (current_buf_for_cursor) {
    char cursor_buf_cmd[32];
    // The +1 for ANSI 1-based indexing, and +1 because text area starts on line 2.
    int cursor_y_on_screen = (current_buf_for_cursor->cy - current_buf_for_cursor->rowoff) + 1 + 1; 
    int cursor_x_on_screen = (current_buf_for_cursor->cx - current_buf_for_cursor->coloff) + 1;
    snprintf(cursor_buf_cmd, sizeof(cursor_buf_cmd), "\x1b[%d;%dH", cursor_y_on_screen, cursor_x_on_screen);
    ab_append(&ab, cursor_buf_cmd, strlen(cursor_buf_cmd));
  } else {
    // No active buffer? Position cursor at 1,1 by default or handle error.
    // With tab bar, maybe 2,1 is better if no buffer means just tab bar is visible.
     ab_append(&ab, "\x1b[2;1H", 6); // Default to top of text area
  }


  ab_append(&ab, "\x1b[?25h", 6); // Show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

// This replaces the previous complex/buggy draw_tab_bar
void draw_tab_bar(struct abuf *ab) {
  ab_append(ab, "\x1b[K", 3); // Clear the entire line to default background
  
  int total_width_used = 0;
  const int max_fname_display_len = 20; // Max length for the filename part shown in a tab

  for (int i = 0; i < E.num_buffers; i++) {
    // Add separator before the second tab onwards
    if (i > 0) {
      if (total_width_used + 3 <= E.screen_cols) { // 3 for " | "
        ab_append(ab, " | ", 3);
        total_width_used += 3;
      } else {
        break; // No more space for separator and next tab name
      }
    }

    char tab_content[max_fname_display_len + 10]; // Buffer for " > filename* "
    char filename_part[max_fname_display_len + 1];

    if (E.buffers[i]->filename) {
      char *basename = strrchr(E.buffers[i]->filename, '/');
      if (basename) {
        basename++; // Skip the '/'
      } else {
        basename = E.buffers[i]->filename;
      }
      snprintf(filename_part, sizeof(filename_part), "%.*s", max_fname_display_len, basename);
    } else {
      strncpy(filename_part, "[No Name]", sizeof(filename_part) -1);
      filename_part[sizeof(filename_part)-1] = '\0';
    }

    // Format: ">filename*" or " filename*" (active tab has '>')
    // Inverse video will be applied to the whole string for the active tab.
    snprintf(tab_content, sizeof(tab_content), "%s%s%s",
             (i == E.active_buffer) ? ">" : " ",
             filename_part,
             E.buffers[i]->dirty ? "*" : "");

    int current_tab_len = strlen(tab_content);
    if (total_width_used + current_tab_len > E.screen_cols) {
      int remaining_space = E.screen_cols - total_width_used;
      if (remaining_space > 0) {
        current_tab_len = remaining_space;
      } else {
        break; // No space left at all
      }
    }

    if (i == E.active_buffer) {
      ab_append(ab, "\x1b[7m", 4); // Start inverse video for active tab
    }
    ab_append(ab, tab_content, current_tab_len);
    if (i == E.active_buffer) {
      ab_append(ab, "\x1b[m", 3);   // End inverse video for active tab
    }
    total_width_used += current_tab_len;
  }
  
  // The rest of the line is already cleared to default background by \x1b[K at the beginning.
  // No need to fill with spaces unless a specific background for the empty part of the tab bar is needed.
  // If we wanted the entire bar (even empty parts) to be inverse, we'd set inverse at start,
  // then for active tab, reset, print special marker + name, then set inverse again.
  // The current approach: default bg for bar, active tab is inverse text on default bg.

  ab_append(ab, "\r\n", 2); // Move to the next line, so draw_rows starts correctly
}


void free_buffer_contents(struct editorBuffer *buf) {
  if (!buf) return;
  for (int i = 0; i < buf->numrows; i++) {
    if (buf->row && buf->row[i].chars) {
      free(buf->row[i].chars);
    }
    if (buf->row && buf->row[i].hl){ // Free hl memory
        free(buf->row[i].hl);
    }
  }
  free(buf->row);
  buf->row = NULL; 
  free(buf->filename);
  buf->filename = NULL; // Avoid double free
  // The editorBuffer struct itself is freed by the caller if needed
}

// Creates a new buffer, adds it to E.buffers, and makes it active.
// Returns 1 on success, 0 on failure (e.g., max buffers reached).
int create_new_buffer() {
  if (E.num_buffers >= MAX_BUFFERS) {
    set_status_message("Max buffers reached. Cannot create new tab.");
    return 0;
  }

  struct editorBuffer *new_buf = malloc(sizeof(struct editorBuffer));
  if (!new_buf) {
    die("malloc for new_buf failed in create_new_buffer"); // Should exit
  }
  init_buffer(new_buf); // Initialize all fields to defaults (empty)

  E.buffers[E.num_buffers] = new_buf;
  E.active_buffer = E.num_buffers;
  E.num_buffers++;
  return 1;
}

// Closes the buffer at the given index.
void close_buffer(int buffer_idx_to_close) {
  if (buffer_idx_to_close < 0 || buffer_idx_to_close >= E.num_buffers) {
    return; // Invalid index
  }

  struct editorBuffer *buf_to_close = E.buffers[buffer_idx_to_close];

  // Check for unsaved changes
  if (buf_to_close->dirty) {
    // For now, just warn. A real implementation would prompt to save or discard.
    // This prompt would need to interact with show_prompt and potentially pause this operation.
    // For this subtask, we'll allow closing dirty buffers without saving.
    // A more complex prompt: char *choice = show_prompt("Buffer has unsaved changes. Close anyway? (y/n): %s", NULL);
    // if (choice && strcmp(choice, "y") == 0) { /* proceed */ } else { free(choice); return; }
    // free(choice);
    set_status_message("Warning: Buffer '%s' was closed with unsaved changes.", buf_to_close->filename ? buf_to_close->filename : "[No Name]");
  }

  free_buffer_contents(buf_to_close);
  free(buf_to_close);
  E.buffers[buffer_idx_to_close] = NULL; // Mark as NULL

  // Shift subsequent buffers down
  for (int i = buffer_idx_to_close; i < E.num_buffers - 1; i++) {
    E.buffers[i] = E.buffers[i + 1];
  }
  E.buffers[E.num_buffers - 1] = NULL; // Null out the last shifted position

  E.num_buffers--;

  if (E.num_buffers == 0) {
    // Always keep at least one buffer.
    if (!create_new_buffer()) {
        // This should ideally not happen if MAX_BUFFERS >= 1
        // and we just made space.
        die("Failed to create initial buffer after closing last one.");
    }
    // E.active_buffer is already set by create_new_buffer
  } else {
    // Adjust active_buffer if necessary
    if (E.active_buffer > buffer_idx_to_close) {
      E.active_buffer--; // Shift active index left if it was after the closed one
    } else if (E.active_buffer == buffer_idx_to_close) {
      // If the active buffer was closed, try to activate the one now at its index,
      // or the previous one if it was the last one.
      if (E.active_buffer >= E.num_buffers) { // If active was the last one
        E.active_buffer = E.num_buffers - 1;
      }
      // If E.active_buffer became -1 (e.g. closed buffer 0 when num_buffers was 1),
      // and then num_buffers became 0, create_new_buffer would set active_buffer to 0.
      // If num_buffers > 0, and active_buffer is now valid, that's fine.
      // Ensure active_buffer is always valid if num_buffers > 0
      if (E.active_buffer < 0 && E.num_buffers > 0) E.active_buffer = 0;
    }
     // Ensure active_buffer is valid, clamp if somehow out of new bounds
    if (E.active_buffer >= E.num_buffers && E.num_buffers > 0) {
        E.active_buffer = E.num_buffers -1;
    }
    if (E.active_buffer < 0 && E.num_buffers > 0) { // Should not happen with above logic
        E.active_buffer = 0;
    }
  }
}


void set_status_message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void process_key_press() {
  static int quit_times = 1;
  int c = read_key();

  struct editorBuffer *current_buf = get_active_buffer(); // Get current buffer for easy access

  if (E.autocomplete_active) {
    switch (c) {
      case '\r': // Enter
      case '\n':
        editorApplySuggestion();
        // After applying, E.autocomplete_active becomes false.
        // Don't process this key further in the main switch.
        return; 
      case ESCAPE:
        editorClearAutocomplete();
        return;
      case '\t': // Tab - Cycle suggestions
      case ARROW_DOWN:
        E.active_suggestion_idx = (E.active_suggestion_idx + 1) % E.num_suggestions;
        return;
      case ARROW_UP: // Cycle suggestions (Shift+TAB is harder to detect without more complex key reading)
        E.active_suggestion_idx = (E.active_suggestion_idx - 1 + E.num_suggestions) % E.num_suggestions;
        return;
      default:
        // Any other key press dismisses autocomplete and processes the key normally
        editorClearAutocomplete();
        // Fall through to process 'c' in the main switch
        break; 
    }
  }


  switch (c) {
  case ESCAPE: // Also used to clear autocomplete, handled above if active
  case CTRL_KEY('l'):
    // If not handled by autocomplete, just break.
    break;
  case '\t': // Tab - Trigger autocomplete or insert tab
    if (current_buf && current_buf->cy < current_buf->numrows) { // Only if cursor is on a valid line
        editorGenerateSuggestions();
        if (E.autocomplete_active) {
            if (E.num_suggestions == 1) { // If only one suggestion, apply it directly
                editorApplySuggestion();
            }
            // If multiple suggestions or zero, the box will show or nothing happens.
            // Don't insert a literal tab if we activated or attempted to activate.
        } else { // No prefix suitable for autocomplete, or feature disabled, insert literal tab
            insert_char('\t'); 
        }
    } else {
        insert_char('\t'); // Cursor not on a valid line, or no buffer, just insert tab
    }
    break;
  case CTRL_KEY('n'): // Next tab
    editorClearAutocomplete(); // Clear AC when switching tabs
    if (E.num_buffers > 0) {
      E.active_buffer = (E.active_buffer + 1) % E.num_buffers;
    }
    break;
  case CTRL_KEY('p'): // Previous tab
    editorClearAutocomplete(); // Clear AC when switching tabs
    if (E.num_buffers > 0) {
      E.active_buffer = (E.active_buffer - 1 + E.num_buffers) % E.num_buffers;
    }
    break;
  case CTRL_KEY('t'): // New tab
    editorClearAutocomplete();
    create_new_buffer(); 
    break;
  case CTRL_KEY('w'): // Close tab
    editorClearAutocomplete();
    if (E.num_buffers > 0) { 
        close_buffer(E.active_buffer);
    }
    break;
  case CTRL_KEY('q'):
    {
      editorClearAutocomplete(); // Clear AC before trying to quit
      // Check ALL buffers for unsaved changes before quitting
      int unsaved_changes_exist = 0;
      for (int i = 0; i < E.num_buffers; i++) {
        if (E.buffers[i] && E.buffers[i]->dirty) {
          unsaved_changes_exist = 1;
          break;
        }
      }

      if (unsaved_changes_exist && quit_times > 0) {
        set_status_message("Unsaved changes exist in some tabs! Press Ctrl+Q again to quit.");
        quit_times--;
        return; 
      }
      clear_screen();
      exit(0);
    } 
    break; 
  case CTRL_KEY('s'):
    save_file(); // Uses get_active_buffer()
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
  case ARROW_DOWN: // These are handled by autocomplete if active
  case ARROW_UP:
  case ARROW_RIGHT:
  case ARROW_LEFT:
    // If autocomplete was active and an arrow key was pressed not for cycling,
    // it would have been cleared. If it's not active, normal cursor movement.
    // Also, clear autocomplete on any cursor movement not related to AC navigation.
    if (!E.autocomplete_active) editorClearAutocomplete(); // Clear if user moves away
    move_cursor(c);
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    struct editorBuffer *buf = get_active_buffer();
    if (!buf) break;
    if (c == PAGE_UP) {
        buf->cy = buf->rowoff;
    } else if (c == PAGE_DOWN) {
        buf->cy = buf->rowoff + E.screen_rows - 1;
        if (buf->cy > buf->numrows) buf->cy = buf->numrows;
    }
    int times = E.screen_rows; // screen_rows is global
    while (times--) {
      move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); // move_cursor uses active buffer
    }
  } break;
  default:
    // Any other char usually clears autocomplete (handled if AC was active)
    // Then insert the char. If it was a char that forms a word,
    // a new autocomplete might be triggered on next TAB.
    // If user types 'a', then 'b', then 'c', AC is cleared after each if it was active.
    // This is basic; a smarter AC would update suggestions.
    if (E.autocomplete_active) editorClearAutocomplete(); // Should be handled above, but as safety
    insert_char(c); 
  }
  quit_times = 1;
}
