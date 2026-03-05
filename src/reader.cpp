#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "reader.h"
#include <Fonts/FreeSans9pt7b.h>

static const GFXfont *_font = &FreeSans9pt7b;

static size_t _file_size = 0;
static char _filename[64] = {};

static int *_page_offsets = nullptr;
static int _page_capacity = 0;
static int _num_pages = 0;
static int _current_page = 0;

static int read_battery_percent() {
#if BATTERY_INDICATOR_ENABLED
  #ifdef ARDUINO_ARCH_ESP32
    static int cached_percent = -1;
    static bool has_valid_battery = false;
    static float last_battery_mv = 0.0f;
    static unsigned long last_sample_ms = 0;
    static unsigned long retry_interval_ms = 5000;
    unsigned long now = millis();

    if (now < 3000) {
      return has_valid_battery ? cached_percent : -1;
    }

    if ((now - last_sample_ms) < retry_interval_ms) {
      return has_valid_battery ? cached_percent : -1;
    }

    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    uint32_t adc_mv = (uint32_t)analogReadMilliVolts(BATTERY_ADC_PIN);
    if (adc_mv == 0) {
      has_valid_battery = false;
      retry_interval_ms = 10000;
      last_sample_ms = now;
      return -1;
    }

    float battery_mv = adc_mv * BATTERY_DIVIDER_RATIO;
    bool plausible = (battery_mv >= (BATTERY_EMPTY_MV - 200) && battery_mv <= (BATTERY_FULL_MV + 300));
    bool stable = (last_battery_mv > 0.0f) && (fabsf(battery_mv - last_battery_mv) <= 120.0f);
    last_battery_mv = battery_mv;

    if (!plausible || !stable) {
      has_valid_battery = false;
      retry_interval_ms = 10000;
      last_sample_ms = now;
      return -1;
    }

    float pct = ((battery_mv - BATTERY_EMPTY_MV) * 100.0f) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    cached_percent = (int)(pct + 0.5f);
    has_valid_battery = true;
    retry_interval_ms = 30000;
    last_sample_ms = now;
    return cached_percent;
  #else
    return -1;
  #endif
 #else
  return -1;
 #endif
}

struct BufferedFileReader {
  File *file;
  uint8_t buffer[512];
  size_t buffer_len;
  size_t buffer_pos;
  size_t absolute_pos;

  bool begin(File &f, size_t start_pos) {
    file = &f;
    buffer_len = 0;
    buffer_pos = 0;
    absolute_pos = start_pos;
    return file->seek(start_pos, SeekSet);
  }

  bool next(char &out_char, size_t &out_pos) {
    if (buffer_pos >= buffer_len) {
      buffer_len = file->read(buffer, sizeof(buffer));
      buffer_pos = 0;
      if (buffer_len == 0) return false;
    }

    out_pos = absolute_pos;
    out_char = (char)buffer[buffer_pos++];
    absolute_pos++;
    return true;
  }
};

struct ReflowFileReader {
  BufferedFileReader raw;
  uint8_t pending_newlines;
  bool pending_space;
  bool has_deferred;
  char deferred_char;
  size_t deferred_pos;

  bool begin(File &f, size_t start_pos) {
    pending_newlines = 0;
    pending_space = false;
    has_deferred = false;
    deferred_char = '\0';
    deferred_pos = 0;
    return raw.begin(f, start_pos);
  }

  bool next_normalized_char(char &out_char, size_t &out_pos) {
    char c;
    size_t pos;
    while (raw.next(c, pos)) {
      uint8_t b0 = (uint8_t)c;

      if (b0 < 0x80) {
        out_char = c;
        out_pos = pos;
        return true;
      }

      if (b0 == 0xC2) {
        char c1;
        size_t p1;
        if (raw.next(c1, p1)) {
          uint8_t b1 = (uint8_t)c1;
          if (b1 == 0xA0) {
            out_char = ' ';
            out_pos = pos;
            return true;
          }
        }
        out_char = '?';
        out_pos = pos;
        return true;
      }

      if (b0 == 0xEF) {
        char c1, c2;
        size_t p1, p2;
        if (raw.next(c1, p1) && raw.next(c2, p2)) {
          uint8_t b1 = (uint8_t)c1;
          uint8_t b2 = (uint8_t)c2;
          if (pos == 0 && b1 == 0xBB && b2 == 0xBF) {
            continue;
          }
        }
        out_char = '?';
        out_pos = pos;
        return true;
      }

      if (b0 == 0xE2) {
        char c1, c2;
        size_t p1, p2;
        if (raw.next(c1, p1) && raw.next(c2, p2)) {
          uint8_t b1 = (uint8_t)c1;
          uint8_t b2 = (uint8_t)c2;

          if (b1 == 0x80 && (b2 == 0x93 || b2 == 0x94)) {
            out_char = '-';
            out_pos = pos;
            return true;
          }
          if (b1 == 0x80 && (b2 == 0x98 || b2 == 0x99 || b2 == 0x9B)) {
            out_char = '\'';
            out_pos = pos;
            return true;
          }
          if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D)) {
            out_char = '"';
            out_pos = pos;
            return true;
          }
          if (b1 == 0x80 && b2 == 0xA6) {
            out_char = '.';
            out_pos = pos;
            return true;
          }
          if (b1 == 0x88 && b2 == 0x92) {
            out_char = '-';
            out_pos = pos;
            return true;
          }
        }
        out_char = '?';
        out_pos = pos;
        return true;
      }

      out_char = '?';
      out_pos = pos;
      return true;
    }

    return false;
  }

  bool next(char &out_char, size_t &out_pos) {
    if (has_deferred) {
      out_char = deferred_char;
      out_pos = deferred_pos;
      has_deferred = false;
      return true;
    }

    while (true) {
      char c;
      size_t pos;
      if (!next_normalized_char(c, pos)) {
        return false;
      }

      if (c == '\r')
        continue;

      if (c == '\n') {
        if (pending_newlines < 255)
          pending_newlines++;
        continue;
      }

      if (c == ' ' || c == '\t') {
        if (pending_newlines >= 2) {
          pending_newlines = 0;
          pending_space = false;
          out_char = '\n';
          out_pos = pos;
          return true;
        }
        pending_space = true;
        pending_newlines = 0;
        continue;
      }

      if (pending_newlines >= 2) {
        has_deferred = true;
        deferred_char = c;
        deferred_pos = pos;
        pending_newlines = 0;
        pending_space = false;
        out_char = '\n';
        out_pos = pos;
        return true;
      }

      if (pending_newlines == 1 || pending_space) {
        has_deferred = true;
        deferred_char = c;
        deferred_pos = pos;
        pending_newlines = 0;
        pending_space = false;
        out_char = ' ';
        out_pos = pos;
        return true;
      }

      pending_newlines = 0;
      pending_space = false;
      out_char = c;
      out_pos = pos;
      return true;
    }
  }
};

static int char_width(char c);

enum TokenType {
  TOKEN_WORD,
  TOKEN_SPACE,
  TOKEN_NEWLINE
};

struct TextToken {
  TokenType type;
  size_t pos;
  char text[128];
  int len;
  int width;
};

struct TokenReader {
  ReflowFileReader *reader;
  bool has_pending;
  char pending_char;
  size_t pending_pos;

  bool begin(ReflowFileReader &r) {
    reader = &r;
    has_pending = false;
    pending_char = '\0';
    pending_pos = 0;
    return true;
  }

  bool next(TextToken &tok, size_t end_pos) {
    char c;
    size_t pos;

    if (has_pending) {
      c = pending_char;
      pos = pending_pos;
      has_pending = false;
    } else {
      if (!reader->next(c, pos)) return false;
    }

    if (pos >= end_pos) return false;

    if (c == '\n') {
      tok.type = TOKEN_NEWLINE;
      tok.pos = pos;
      tok.len = 0;
      tok.width = 0;
      tok.text[0] = '\0';
      return true;
    }

    if (c == ' ') {
      tok.type = TOKEN_SPACE;
      tok.pos = pos;
      tok.len = 0;
      tok.width = 0;
      tok.text[0] = '\0';
      return true;
    }

    tok.type = TOKEN_WORD;
    tok.pos = pos;
    tok.len = 0;
    tok.width = 0;

    while (true) {
      if (tok.len < (int)sizeof(tok.text) - 1) {
        tok.text[tok.len++] = c;
        tok.width += char_width(c);
      }

      if (!reader->next(c, pos))
        break;
      if (pos >= end_pos)
        break;
      if (c == ' ' || c == '\n') {
        has_pending = true;
        pending_char = c;
        pending_pos = pos;
        break;
      }
    }

    tok.text[tok.len] = '\0';
    return true;
  }
};

static int char_width(char c) {
  uint8_t uc = (uint8_t)c;
  if (uc >= _font->first && uc <= _font->last) {
    return _font->glyph[uc - _font->first].xAdvance;
  }
  return 0;
}

static int *alloc_page_offsets(int count) {
  int *buffer = (int *)ps_malloc((size_t)count * sizeof(int));
  if (!buffer)
    buffer = (int *)malloc((size_t)count * sizeof(int));
  return buffer;
}

static bool ensure_page_capacity(int required_count) {
  if (required_count <= _page_capacity)
    return true;

  int new_capacity = (_page_capacity > 0) ? _page_capacity : 32;
  while (new_capacity < required_count) {
    if (new_capacity > 500000) {
      new_capacity = required_count;
      break;
    }
    new_capacity *= 2;
  }

  int *new_offsets = alloc_page_offsets(new_capacity);
  if (!new_offsets)
    return false;

  if (_page_offsets && _num_pages > 0)
    memcpy(new_offsets, _page_offsets, (size_t)_num_pages * sizeof(int));

  free(_page_offsets);
  _page_offsets = new_offsets;
  _page_capacity = new_capacity;
  return true;
}

static int get_space_width() {
  if (' ' >= _font->first && ' ' <= _font->last)
    return _font->glyph[' ' - _font->first].xAdvance;
  return 4;
}

static bool push_page_offset(size_t pos) {
  if (ensure_page_capacity(_num_pages + 1)) {
    _page_offsets[_num_pages++] = (int)pos;
    return true;
  }
  Serial.println("Page index full; truncating page map");
  return false;
}

static bool advance_layout_line(int &x, int &line, int max_lines, size_t next_page_pos) {
  x = MARGIN_X;
  line++;
  if (line >= max_lines) {
    if (!push_page_offset(next_page_pos))
      return false;
    line = 0;
  }
  return true;
}

static bool commit_layout_word(int word_width, size_t word_start,
                               int &x, int &line,
                               int max_x, int max_lines) {
  if (x + word_width > max_x && x > MARGIN_X) {
    if (!advance_layout_line(x, line, max_lines, word_start))
      return false;
  }
  x += word_width;
  return true;
}

static void paginate(int disp_w, int disp_h) {
  File f = LittleFS.open(_filename, "r");
  if (!f) {
    _num_pages = 0;
    return;
  }

  int max_x = disp_w - MARGIN_X;
  int max_lines = (disp_h - MARGIN_TOP - MARGIN_BOTTOM) / LINE_HEIGHT;
  if (max_lines < 1) max_lines = 1;
  int spc_w = get_space_width();

  int initial_pages = (int)(_file_size / 700) + 8;
  if (initial_pages < 32) initial_pages = 32;

  free(_page_offsets);
  _page_offsets = nullptr;
  _page_capacity = 0;

  if (!ensure_page_capacity(initial_pages)) {
    _num_pages = 0;
    return;
  }
  _page_offsets[0] = 0;
  _num_pages = 1;

  ReflowFileReader reader;
  if (!reader.begin(f, 0)) {
    f.close();
    _num_pages = 0;
    return;
  }

  TokenReader tokens;
  tokens.begin(reader);

  int x = MARGIN_X;
  int line = 0;
  TextToken tok;
  while (tokens.next(tok, (size_t)-1)) {
    if (tok.type == TOKEN_NEWLINE) {
      if (!advance_layout_line(x, line, max_lines, tok.pos + 1))
        break;
      continue;
    }

    if (tok.type == TOKEN_SPACE) {
      if (x != MARGIN_X)
        x += spc_w;
      continue;
    }

    if (!commit_layout_word(tok.width, tok.pos, x, line, max_x, max_lines))
      break;
  }

  f.close();
}

// ── Page-offset cache ─────────────────────────────────────────────────────

#pragma pack(push, 1)
struct PageCacheHeader {
  uint8_t  magic[4];
  uint32_t file_size;
  uint16_t disp_w;
  uint16_t disp_h;
  uint32_t num_pages;
};
#pragma pack(pop)

static const uint8_t CACHE_MAGIC[4] = {'E', 'P', 'I', 'C'};

static void cache_path_for(const char *book_path, char *out, size_t out_len) {
  strncpy(out, book_path, out_len - 1);
  out[out_len - 1] = '\0';
  char *dot = strrchr(out, '.');
  if (dot && dot > out) {
    strncpy(dot, ".pc", out_len - (size_t)(dot - out));
    out[out_len - 1] = '\0';
  } else {
    size_t cur_len = strlen(out);
    if (cur_len + 3 < out_len)
      memcpy(out + cur_len, ".pc\0", 4);
  }
}

static bool load_page_cache(int disp_w, int disp_h) {
  char cache_path[72];
  cache_path_for(_filename, cache_path, sizeof(cache_path));

  File f = LittleFS.open(cache_path, "r");
  if (!f) return false;

  PageCacheHeader hdr;
  if (f.read((uint8_t *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    return false;
  }

  if (memcmp(hdr.magic, CACHE_MAGIC, 4) != 0 ||
      hdr.file_size != (uint32_t)_file_size ||
      hdr.disp_w != (uint16_t)disp_w ||
      hdr.disp_h != (uint16_t)disp_h ||
      hdr.num_pages == 0) {
    f.close();
    return false;
  }

  free(_page_offsets);
  _page_offsets = nullptr;
  _page_capacity = 0;

  if (!ensure_page_capacity((int)hdr.num_pages)) {
    f.close();
    return false;
  }

  size_t bytes = (size_t)hdr.num_pages * sizeof(int);
  if (f.read((uint8_t *)_page_offsets, bytes) != bytes) {
    f.close();
    free(_page_offsets);
    _page_offsets = nullptr;
    _page_capacity = 0;
    return false;
  }

  _num_pages = (int)hdr.num_pages;
  f.close();
  Serial.printf("Page cache hit: %s (%d pages)\n", cache_path, _num_pages);
  return true;
}

static void save_page_cache(int disp_w, int disp_h) {
  if (_num_pages == 0 || !_page_offsets) return;

  char cache_path[72];
  cache_path_for(_filename, cache_path, sizeof(cache_path));

  File f = LittleFS.open(cache_path, "w");
  if (!f) {
    Serial.printf("Cache write failed: %s\n", cache_path);
    return;
  }

  PageCacheHeader hdr;
  memcpy(hdr.magic, CACHE_MAGIC, 4);
  hdr.file_size  = (uint32_t)_file_size;
  hdr.disp_w     = (uint16_t)disp_w;
  hdr.disp_h     = (uint16_t)disp_h;
  hdr.num_pages  = (uint32_t)_num_pages;

  f.write((uint8_t *)&hdr, sizeof(hdr));
  f.write((uint8_t *)_page_offsets, (size_t)_num_pages * sizeof(int));
  f.close();
  Serial.printf("Page cache saved: %s (%d pages)\n", cache_path, _num_pages);
}

// ── Fast-wake helpers ─────────────────────────────────────────────────────

void reader_get_page_window(int *out_page, int *out_num_pages,
                             uint32_t *out_file_size, int window[4]) {
  *out_page      = _current_page;
  *out_num_pages = _num_pages;
  *out_file_size = (uint32_t)_file_size;
  window[0] = (_current_page - 1 >= 0)            ? _page_offsets[_current_page - 1] : 0;
  window[1] = _page_offsets[_current_page];
  window[2] = (_current_page + 1 < _num_pages)    ? _page_offsets[_current_page + 1] : 0;
  window[3] = (_current_page + 2 < _num_pages)    ? _page_offsets[_current_page + 2] : 0;
}

bool reader_open_fast(const char *filename, uint32_t file_size, int num_pages,
                      int current_page, const int page_window[4]) {
  reader_close();

  if (!filename || filename[0] == '\0' || num_pages <= 0 || file_size == 0)
    return false;

  if (!ensure_page_capacity(num_pages))
    return false;

  memset(_page_offsets, 0, (size_t)num_pages * sizeof(int));
  _num_pages = num_pages;
  _file_size = (size_t)file_size;
  strncpy(_filename, filename, sizeof(_filename) - 1);
  _filename[sizeof(_filename) - 1] = '\0';

  // Place the 4 nearby offsets at their correct indices in the full array
  if (current_page - 1 >= 0)         _page_offsets[current_page - 1] = page_window[0];
                                      _page_offsets[current_page]     = page_window[1];
  if (current_page + 1 < num_pages)  _page_offsets[current_page + 1] = page_window[2];
  if (current_page + 2 < num_pages)  _page_offsets[current_page + 2] = page_window[3];

  _current_page = constrain(current_page, 0, num_pages - 1);
  Serial.printf("Fast open: %s page %d/%d\n", filename, _current_page + 1, num_pages);
  return true;
}

bool reader_open(DISPLAY_TYPE &display, const char *filename, int start_page) {
  reader_close();

  File f = LittleFS.open(filename, "r");
  if (!f) {
    Serial.printf("Failed to open: %s\n", filename);
    return false;
  }

  _file_size = (size_t)f.size();
  if (_file_size == 0) {
    Serial.println("File is empty");
    f.close();
    return false;
  }
  f.close();

  strncpy(_filename, filename, sizeof(_filename) - 1);
  _filename[sizeof(_filename) - 1] = '\0';
  if (!load_page_cache(display.width(), display.height())) {
    paginate(display.width(), display.height());
    if (_num_pages > 0)
      save_page_cache(display.width(), display.height());
  }
  if (_num_pages == 0) {
    Serial.println("Failed to allocate pagination buffer");
    reader_close();
    return false;
  }

  _current_page = constrain(start_page, 0, max(0, _num_pages - 1));

  Serial.printf("Opened %s: %u bytes, %d pages\n", filename, (unsigned)_file_size, _num_pages);
  return true;
}

void reader_render(DISPLAY_TYPE &display) {
  if (_filename[0] == '\0' || _num_pages == 0) return;

  display.clearMemory();
  display.fillRect(0, 0, display.width(), display.height(), WHITE);
  display.setFont(_font);
  display.setTextColor(BLACK);
  display.setTextWrap(false);

  File f = LittleFS.open(_filename, "r");
  if (!f) return;

  int spc_w = get_space_width();
  int max_x = display.width() - MARGIN_X;

  size_t start = _page_offsets[_current_page];
  size_t end = (_current_page + 1 < _num_pages)
                   ? (size_t)_page_offsets[_current_page + 1]
                   : _file_size;

  if (start >= _file_size)
    start = 0;
  if (end <= start || end > _file_size)
    end = _file_size;

  ReflowFileReader reader;
  if (!reader.begin(f, start)) {
    f.close();
    return;
  }

  TokenReader tokens;
  tokens.begin(reader);

  int x = MARGIN_X;
  int y = MARGIN_TOP;
  int max_y = display.height() - MARGIN_BOTTOM;

  TextToken tok;
  int token_count = 0;
  while (tokens.next(tok, end)) {
    token_count++;
    if (token_count > 4000) {
      break;
    }

    if (tok.type == TOKEN_NEWLINE) {
      x = MARGIN_X;
      y += LINE_HEIGHT;
      if (y > max_y)
        break;
      continue;
    }

    if (tok.type == TOKEN_SPACE) {
      if (x != MARGIN_X)
        x += spc_w;
      continue;
    }

    if (x + tok.width > max_x && x > MARGIN_X) {
      x = MARGIN_X;
      y += LINE_HEIGHT;
      if (y > max_y)
        break;
    }

    if (y > max_y)
      break;

    display.setCursor(x, y);
    display.print(tok.text);
    x += tok.width;
  }

  f.close();

  // Page indicator (bottom-right, small default font)
  display.setFont(NULL);
  display.setTextSize(1);
  char indicator[16];
  snprintf(indicator, sizeof(indicator), "%d/%d", _current_page + 1, _num_pages);
  int16_t ix, iy;
  uint16_t iw, ih;
  display.getTextBounds(indicator, 0, 0, &ix, &iy, &iw, &ih);
  display.setCursor(display.width() - iw - 2, display.height() - ih - 2);
  display.print(indicator);

#if BATTERY_INDICATOR_ENABLED
  int battery_pct = read_battery_percent();
  char battery_label[20];
  if (battery_pct >= 0)
    snprintf(battery_label, sizeof(battery_label), "Bat %d%%", battery_pct);
  else
    snprintf(battery_label, sizeof(battery_label), "Bat --%%");

  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(battery_label, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(2, display.height() - bh - 2);
  display.print(battery_label);
#endif

  display.fastmodeOn();

  display.update();

  if (PARTIAL_CLEANUP_EXTRA_UPDATES > 0) {
    for (int i = 0; i < PARTIAL_CLEANUP_EXTRA_UPDATES; i++) {
      display.update();
    }
  }
}

bool reader_next_page() {
  if (_current_page < _num_pages - 1) {
    _current_page++;
    return true;
  }
  return false;
}

bool reader_prev_page() {
  if (_current_page > 0) {
    _current_page--;
    return true;
  }
  return false;
}

int reader_current_page() { return _current_page; }
int reader_total_pages() { return _num_pages; }
const char *reader_filename() { return _filename; }
bool reader_is_open() { return _filename[0] != '\0' && _num_pages > 0; }

void reader_close() {
  if (_page_offsets) { free(_page_offsets); _page_offsets = nullptr; }
  _page_capacity = 0;
  _file_size = 0;
  _num_pages = 0;
  _current_page = 0;
  _filename[0] = '\0';
}

void reader_delete_book_cache(const char *filename) {
  char cache_path[72];
  cache_path_for(filename, cache_path, sizeof(cache_path));
  if (LittleFS.exists(cache_path)) {
    LittleFS.remove(cache_path);
    Serial.printf("Cache deleted: %s\n", cache_path);
  }
}

void reader_load_full_cache(int disp_w, int disp_h) {
  if (_filename[0] == '\0' || _file_size == 0) return;
  int saved_page = _current_page;
  if (!load_page_cache(disp_w, disp_h)) {
    Serial.println("Full cache miss after fast wake; repaginating");
    paginate(disp_w, disp_h);
    if (_num_pages > 0)
      save_page_cache(disp_w, disp_h);
  }
  _current_page = constrain(saved_page, 0, max(0, _num_pages - 1));
  Serial.printf("Full offsets loaded: %d pages, current=%d\n", _num_pages, _current_page);
}
