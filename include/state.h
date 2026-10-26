#pragma once

struct ReadingState {
  char filename[64];
  int page;
  bool valid;
};

ReadingState state_load();
void state_save(const char *filename, int page);
void state_clear();

int  state_load_book_page(const char *filename);
void state_save_book_page(const char *filename, int page);
void state_delete_book_page(const char *filename);
