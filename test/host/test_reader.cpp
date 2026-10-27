#include <cassert>
#include "mocks/diagnostics_stub.h"
#include "../../src/reader.cpp"
#include "book_files.h"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;
bool vext_held;
bool deep_sleep_hold;

float battery_voltage_mv() { return 4000; }

static void check_cache_recovery(EinkDisplay &display, const std::vector<uint8_t> &bad) {
  LittleFS.storage["/book.pc"] = bad;
  int pages = _num_pages;
  int *offsets = _page_offsets;
  size_t allocations = ps_malloc_calls;
  reader_load_full_cache(display.width(), display.height());
  assert(_num_pages == pages && _page_offsets == offsets);
  assert(ps_malloc_calls == allocations);
  assert(_page_offsets[0] == 0 && _page_offsets[1] > 0);
}

int main() {
  for (GFXglyph &glyph : mock_glyphs) glyph.xAdvance = 6;
  assert(has_txt_extension("a.txt") && has_txt_extension("/BOOK.TxT"));
  assert(!has_txt_extension("txt") && !has_txt_extension("a.txt.pc"));
  assert(!has_txt_extension("") && !has_txt_extension("a.text"));

  std::string book;
  for (int i = 0; i < 4000; i++) book += "hello world ";
  LittleFS.storage["/book.txt"] = std::vector<uint8_t>(book.begin(), book.end());
  EinkDisplay display;
  assert(reader_open(display, "/book.txt", 1));
  assert(_num_pages > 2 && reader_current_page() == 1);
  std::vector<uint8_t> cache = LittleFS.storage["/book.pc"];
  int *offsets = _page_offsets;
  size_t allocations = ps_malloc_calls;
  reader_load_full_cache(display.width(), display.height());
  assert(_page_offsets == offsets && reader_current_page() == 1);
  assert(ps_malloc_calls == allocations);

  reader_render(display);
  std::vector<std::string> page_text = display.printed;
  assert(page_text.size() > 2);
  assert(page_text.back() == "Bat 4.000 V");
  assert(display.rectangles.size() == 1);
  reader_render(display, true);
  assert(display.printed == page_text && display.rectangles.size() == 5);
  const auto &lock = display.rectangles[3];
  assert(lock.x == (display.width() - 9) / 2 && lock.w == 9);
  assert(lock.y + lock.h < display.height());
  reader_render(display);
  assert(display.rectangles.size() == 1);

  std::vector<uint8_t> truncated = cache;
  truncated.resize(sizeof(PageCacheHeader) + 1);
  check_cache_recovery(display, truncated);
  PageCacheHeader hdr;
  memcpy(&hdr, cache.data(), sizeof(hdr));
  hdr.num_pages = UINT32_MAX;
  std::vector<uint8_t> oversized = cache;
  memcpy(oversized.data(), &hdr, sizeof(hdr));
  check_cache_recovery(display, oversized);
  for (int invalid : {-1, 0, int(book.size())}) {
    std::vector<uint8_t> bad_offsets = cache;
    memcpy(bad_offsets.data() + sizeof(PageCacheHeader) + sizeof(int),
           &invalid, sizeof(invalid));
    check_cache_recovery(display, bad_offsets);
  }

  int current, pages, window[4];
  uint32_t file_size;
  reader_get_page_window(&current, &pages, &file_size, window);
  assert(!reader_open_fast("/book.txt", file_size, pages, -1, window));
  assert(!reader_open_fast("/book.txt", file_size, pages, pages, window));
  assert(reader_open_fast("/book.txt", file_size, pages, current, window));
  reader_render(display);
  assert(display.printed == page_text);
  offsets = _page_offsets;
  allocations = ps_malloc_calls;
  reader_load_full_cache(display.width(), display.height());
  assert(_page_offsets == offsets);
  assert(ps_malloc_calls == allocations);
  reader_render(display);
  assert(display.printed == page_text);

  LittleFS.fail_remove_path = "/book.pc";
  assert(!reader_delete_book_cache("/book.txt"));
  assert(LittleFS.exists("/book.pc"));
  LittleFS.fail_remove_path.clear();
  LittleFS.write_limit = 1;
  assert(reader_delete_book_cache("/book.txt"));
  assert(reader_delete_book_cache("/book.txt"));
  save_page_cache(display.width(), display.height());
  assert(!LittleFS.exists("/book.pc"));
  LittleFS.write_limit = size_t(-1);
  save_page_cache(display.width(), display.height());
  reader_close();
  assert(reader_open(display, "/book.txt", 1));
  reader_render(display);
  assert(display.printed == page_text);
  reader_close();
  puts("Reader cache reuse/recovery, fast wake, lock geometry and filename tests passed");
}
