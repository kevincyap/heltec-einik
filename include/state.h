#pragma once

struct ReadingState {
  char filename[64];
  int page;
  bool valid;
};

ReadingState state_load();
void state_save(const char *filename, int page);
void state_clear();
