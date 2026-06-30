#pragma once

#include "config.h"
#include "display.h"

bool reader_open(EinkDisplay &display, const char *filename, int start_page = 0);
void reader_render(EinkDisplay &display);
bool reader_next_page();
bool reader_prev_page();
int reader_current_page();
int reader_total_pages();
const char *reader_filename();
bool reader_is_open();
void reader_close();
void reader_delete_book_cache(const char *filename);
void reader_get_page_window(int *out_page, int *out_num_pages,
                             uint32_t *out_file_size, int window[4]);
bool reader_open_fast(const char *filename, uint32_t file_size, int num_pages,
                      int current_page, const int page_window[4]);
void reader_load_full_cache(int disp_w, int disp_h);
