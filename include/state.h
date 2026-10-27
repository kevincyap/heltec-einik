#pragma once

struct ReadingState {
  char filename[64];
  int page;
  bool valid;
};

ReadingState state_load();
bool state_load_checked(ReadingState &result);
bool state_save(const char *filename, int page);
bool state_clear();

int  state_load_book_page(const char *filename);
bool state_save_book_page(const char *filename, int page);
bool state_delete_book_page(const char *filename);
