#pragma once

#include "config.h"
#include <heltec-eink-modules.h>

bool reader_open(DISPLAY_TYPE &display, const char *filename, int start_page = 0);
void reader_render(DISPLAY_TYPE &display);
bool reader_next_page();
bool reader_prev_page();
int reader_current_page();
int reader_total_pages();
const char *reader_filename();
bool reader_is_open();
void reader_close();
