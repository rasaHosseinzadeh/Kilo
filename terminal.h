#ifndef TERMINAL
#define TERMINAL

#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

void clear_screen();
int die(const char *s);
int get_window_size(int *rows, int *cols);
void disable_raw_mode();
void enable_raw_mode();

#endif
