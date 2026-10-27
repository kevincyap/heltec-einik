#include <cassert>
#include "../../src/state.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;

int main() {
  state_save("/first.txt", 3);
  assert(LittleFS.write_opens == 1);
  state_save("/first.txt", 3);
  assert(LittleFS.write_opens == 1);
  ReadingState saved = state_load();
  assert(saved.valid && saved.page == 3);
  assert(strcmp(saved.filename, "/first.txt") == 0);

  LittleFS.fail_write_open = true;
  state_save("/first.txt", 4);
  LittleFS.fail_write_open = false;
  state_save("/first.txt", 4);
  assert(state_load().page == 4);
  LittleFS.write_limit = 1;
  assert(!state_save("/first.txt", 5));
  assert(!_cache_valid && state_load().page == 4);
  LittleFS.write_limit = size_t(-1);
  state_save("/first.txt", 4);
  assert(state_load().valid && state_load().page == 4);

  state_save_book_page("/first.txt", 3);
  int writes = LittleFS.write_opens;
  state_save_book_page("/first.txt", 3);
  assert(LittleFS.write_opens == writes);
  assert(state_load_book_page("/first.txt") == 3);
  state_save_book_page("/first.txt", 3);
  assert(LittleFS.write_opens == writes);
  state_save_book_page("/second.txt", 3);
  assert(state_load_book_page("/second.txt") == 3);
  assert(state_load_book_page("/first.txt") == 3);
  state_save_book_page("/first.txt", 4);
  assert(state_load_book_page("/first.txt") == 4);

  LittleFS.write_limit = 1;
  state_save_book_page("/first.txt", 5);
  assert(!_cached_book.valid);
  LittleFS.write_limit = size_t(-1);
  state_save_book_page("/first.txt", 4);
  assert(state_load_book_page("/first.txt") == 4);
  state_delete_book_page("/first.txt");
  assert(!_cached_book.valid && !LittleFS.exists("/first.pos"));
  state_save_book_page("/first.txt", 4);
  assert(state_load_book_page("/first.txt") == 4);

  LittleFS.fail_write_open = true;
  state_save_book_page("/first.txt", 5);
  LittleFS.fail_write_open = false;
  state_save_book_page("/first.txt", 5);
  assert(state_load_book_page("/first.txt") == 5);
  assert(state_load_book_page("/missing.txt") == 0);
  assert(!_cached_book.valid);
  LittleFS.storage["/first.pos"].resize(1);
  assert(state_load_book_page("/first.txt") == 0 && !_cached_book.valid);
  state_save_book_page("/first.txt", 5);
  assert(state_load_book_page("/first.txt") == 5);
  LittleFS.fail_rename_path = "/first.pos";
  assert(!state_save_book_page("/first.txt", 6));
  assert(state_load_book_page("/first.txt") == 5);
  LittleFS.fail_rename_path.clear();
  assert(state_save_book_page("/first.txt", 6));
  LittleFS.fail_remove_path = "/first.pos";
  assert(!state_delete_book_page("/first.txt"));
  assert(_cached_book.valid && LittleFS.exists("/first.pos"));
  LittleFS.fail_remove_path.clear();
  assert(state_delete_book_page("/first.txt"));
  assert(state_delete_book_page("/first.txt"));

  assert(state_save("/first.txt", 7));
  LittleFS.fail_rename_path = "/state.dat";
  assert(!state_save("/first.txt", 8));
  assert(state_load().page == 7);
  LittleFS.fail_rename_path.clear();
  assert(state_save("/first.txt", 8));
  LittleFS.fail_remove_path = "/state.dat";
  assert(!state_clear() && state_load().page == 8);
  LittleFS.fail_remove_path.clear();
  assert(state_clear() && state_clear());
  ReadingState checked;
  assert(state_load_checked(checked) && !checked.valid);
  assert(state_save("/first.txt", 8));
  LittleFS.fail_open_path = "/state.dat";
  assert(!state_load_checked(checked) && !checked.valid);
  LittleFS.fail_open_path.clear();
  puts("State/bookmark deduplication, disk format and failed-write retries passed");
}
