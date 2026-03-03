#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "reader.h"
#include <Fonts/FreeSans9pt7b.h>

static const GFXfont *_font = &FreeSans9pt7b;

static char *_text = nullptr;
static size_t _text_len = 0;
static char _filename[64] = {};

static int *_page_offsets = nullptr;
static int _num_pages = 0;
static int _current_page = 0;
static int _refresh_count = 0;

// Measure pixel width of a string segment using font glyph data
static int measure_text(const char *text, int len) {
  int width = 0;
  for (int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)text[i];
    if (c >= _font->first && c <= _font->last) {
      width += _font->glyph[c - _font->first].xAdvance;
    }
  }
  return width;
}

static int get_space_width() {
  if (' ' >= _font->first && ' ' <= _font->last)
    return _font->glyph[' ' - _font->first].xAdvance;
  return 4;
}

static void paginate(int disp_w, int disp_h) {
  int max_x = disp_w - MARGIN_X;
  int max_lines = (disp_h - MARGIN_TOP - MARGIN_BOTTOM) / LINE_HEIGHT;
  if (max_lines < 1) max_lines = 1;
  int spc_w = get_space_width();

  // Worst case: every byte is a newline → text_len / max_lines pages
  int max_pages = (_text_len / max_lines) + 2;
  free(_page_offsets);
  _page_offsets = (int *)ps_malloc(max_pages * sizeof(int));
  if (!_page_offsets)
    _page_offsets = (int *)malloc(max_pages * sizeof(int));
  _page_offsets[0] = 0;
  _num_pages = 1;

  int x = MARGIN_X;
  int line = 0;
  size_t i = 0;

  while (i < _text_len) {
    if (_text[i] == '\r') { i++; continue; }
    if (_text[i] == '\n') {
      x = MARGIN_X;
      line++;
      if (line >= max_lines) {
        if (_num_pages < max_pages)
          _page_offsets[_num_pages++] = i + 1;
        line = 0;
      }
      i++;
      continue;
    }

    // Skip leading spaces on a line
    if (x == MARGIN_X) {
      while (i < _text_len && _text[i] == ' ') i++;
      if (i >= _text_len) break;
    }

    // Find word boundary
    size_t word_start = i;
    while (i < _text_len && _text[i] != ' ' && _text[i] != '\n' && _text[i] != '\r')
      i++;
    if (i == word_start) continue;

    int word_width = measure_text(_text + word_start, i - word_start);

    // Word wrap
    if (x + word_width > max_x && x > MARGIN_X) {
      x = MARGIN_X;
      line++;
      if (line >= max_lines) {
        if (_num_pages < max_pages)
          _page_offsets[_num_pages++] = word_start;
        line = 0;
      }
    }

    x += word_width;

    // Consume trailing space
    if (i < _text_len && _text[i] == ' ') {
      x += spc_w;
      i++;
    }
  }
}

bool reader_open(DISPLAY_TYPE &display, const char *filename, int start_page) {
  reader_close();

  File f = LittleFS.open(filename, "r");
  if (!f) {
    Serial.printf("Failed to open: %s\n", filename);
    return false;
  }

  _text_len = f.size();
  _text = (char *)ps_malloc(_text_len + 1);
  if (!_text)
    _text = (char *)malloc(_text_len + 1);
  if (!_text) {
    Serial.println("Failed to allocate text buffer");
    f.close();
    return false;
  }

  f.read((uint8_t *)_text, _text_len);
  _text[_text_len] = '\0';
  f.close();

  strncpy(_filename, filename, sizeof(_filename) - 1);
  paginate(display.width(), display.height());

  _current_page = constrain(start_page, 0, max(0, _num_pages - 1));
  _refresh_count = 0;

  Serial.printf("Opened %s: %u bytes, %d pages\n", filename, (unsigned)_text_len, _num_pages);
  return true;
}

void reader_render(DISPLAY_TYPE &display) {
  if (!_text || _num_pages == 0) return;

  display.clearMemory();
  display.setFont(_font);
  display.setTextColor(BLACK);
  display.setTextWrap(false);

  int spc_w = get_space_width();
  int max_x = display.width() - MARGIN_X;

  size_t start = _page_offsets[_current_page];
  size_t end = (_current_page + 1 < _num_pages)
                   ? (size_t)_page_offsets[_current_page + 1]
                   : _text_len;

  int x = MARGIN_X;
  int y = MARGIN_TOP;
  size_t i = start;

  while (i < end) {
    if (_text[i] == '\r') { i++; continue; }
    if (_text[i] == '\n') {
      x = MARGIN_X;
      y += LINE_HEIGHT;
      i++;
      continue;
    }
    if (x == MARGIN_X) {
      while (i < end && _text[i] == ' ') i++;
      if (i >= end) break;
    }

    size_t word_start = i;
    while (i < end && _text[i] != ' ' && _text[i] != '\n' && _text[i] != '\r')
      i++;
    if (i == word_start) continue;

    int word_width = measure_text(_text + word_start, i - word_start);

    if (x + word_width > max_x && x > MARGIN_X) {
      x = MARGIN_X;
      y += LINE_HEIGHT;
    }

    display.setCursor(x, y);
    char saved = _text[i];
    _text[i] = '\0';
    display.print(_text + word_start);
    _text[i] = saved;

    x += word_width;
    if (i < end && _text[i] == ' ') {
      x += spc_w;
      i++;
    }
  }

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

  // Alternate between full and partial refresh
  if (_refresh_count % FULL_REFRESH_INTERVAL == 0) {
    display.fastmodeOff();
  } else {
    display.fastmodeOn();
  }
  display.update();
  _refresh_count++;
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
bool reader_is_open() { return _text != nullptr; }

void reader_close() {
  if (_text) { free(_text); _text = nullptr; }
  if (_page_offsets) { free(_page_offsets); _page_offsets = nullptr; }
  _text_len = 0;
  _num_pages = 0;
  _current_page = 0;
  _filename[0] = '\0';
}
