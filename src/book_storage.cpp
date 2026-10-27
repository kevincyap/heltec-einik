#include "book_storage.h"
#include "book_files.h"
#include "reader.h"
#include "state.h"
#include "serial_output.h"
#include <atomic>
#include <limits.h>
#include <string.h>

static std::atomic_flag mutation = ATOMIC_FLAG_INIT;
static const char upload_temp[] = "/.book-upload.tmp";

class MutationGuard {
 public:
  MutationGuard() : owned(!mutation.test_and_set()) {}
  ~MutationGuard() { if (owned) mutation.clear(); }
  bool owned;
};

bool book_storage_path(const char *name, char path[64]) {
  if (!name) return false;
  if (*name == '/') ++name;
  size_t len = strlen(name);
  if (len < 5 || len > 62 || !has_txt_extension(name))
    return false;
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = name[i];
    if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
  }
  path[0] = '/';
  memcpy(path + 1, name, len + 1);
  return true;
}

bool book_storage_page(const char *text, int &page) {
  if (!text || !*text) return false;
  int value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    int digit = *text - '0';
    if (value > (INT_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  page = value;
  return true;
}

BookStorageResult book_storage_delete(const char *name) {
  char path[64];
  if (!book_storage_path(name, path)) return BookStorageResult::InvalidName;
  MutationGuard guard;
  if (!guard.owned) return BookStorageResult::Busy;
  if (!LittleFS.exists(path)) return BookStorageResult::NotFound;
  ReadingState saved;
  if (!state_load_checked(saved)) return BookStorageResult::StateFailed;
  if (!reader_delete_book_cache(path)) return BookStorageResult::CacheFailed;
  if (!state_delete_book_page(path)) return BookStorageResult::BookmarkFailed;
  if (saved.valid && strcmp(saved.filename, path) == 0 && !state_clear())
    return BookStorageResult::StateFailed;
  if (!LittleFS.remove(path)) return BookStorageResult::DeleteFailed;
  return BookStorageResult::Ok;
}

BookStorageResult book_storage_set_page(const char *name, int page) {
  char path[64];
  if (!book_storage_path(name, path)) return BookStorageResult::InvalidName;
  if (page < 0) return BookStorageResult::InvalidPage;
  MutationGuard guard;
  if (!guard.owned) return BookStorageResult::Busy;
  if (!LittleFS.exists(path)) return BookStorageResult::NotFound;
  if (!state_save_book_page(path, page)) return BookStorageResult::BookmarkFailed;
  if (!state_save(path, page)) return BookStorageResult::StateFailed;
  return BookStorageResult::Ok;
}

BookUpload::BookUpload()
  : _path{}, _size(0), _result(BookStorageResult::IncompleteUpload),
    _owns(false), _started(false), _final(false) {}

BookUpload::~BookUpload() {
  if (_owns) {
    BookStorageResult result = fail(BookStorageResult::IncompleteUpload);
    debug_serial.printf("[BOOK] Upload aborted: %s\n", book_storage_message(result));
  }
}

void BookUpload::release() {
  if (_owns) {
    _owns = false;
    mutation.clear();
  }
}

BookStorageResult BookUpload::fail(BookStorageResult result) {
  _file.close();
  if (_owns && LittleFS.exists(upload_temp) && !LittleFS.remove(upload_temp))
    result = BookStorageResult::CleanupFailed;
  release();
  _result = result;
  return result;
}

BookStorageResult BookUpload::begin(const char *name) {
  if (_started) return reject();
  _started = true;
  if (!book_storage_path(name, _path)) return fail(BookStorageResult::InvalidName);
  if (mutation.test_and_set()) return fail(BookStorageResult::Busy);
  _owns = true;
  if (LittleFS.exists(upload_temp) && !LittleFS.remove(upload_temp))
    return fail(BookStorageResult::CleanupFailed);
  _file = LittleFS.open(upload_temp, "w");
  if (!_file) return fail(BookStorageResult::OpenFailed);
  _result = BookStorageResult::Ok;
  return _result;
}

BookStorageResult BookUpload::append(size_t offset, const uint8_t *data,
                                     size_t count, bool final) {
  if (_result != BookStorageResult::Ok) return _result;
  if (!_owns || _final || offset != _size || count > SIZE_MAX - _size ||
      (count && !data)) return reject();
  if (count && _file.write(data, count) != count) return fail(BookStorageResult::WriteFailed);
  _size += count;
  _final = final;
  if (final) _file.close();
  return _result;
}

BookStorageResult BookUpload::reject() {
  return fail(BookStorageResult::InvalidUpload);
}

BookStorageResult BookUpload::commit() {
  if (_result != BookStorageResult::Ok) return _result;
  if (!_owns || !_final) return fail(BookStorageResult::IncompleteUpload);
  if (!reader_delete_book_cache(_path)) return fail(BookStorageResult::CacheFailed);
  // POSIX rename replaces the target without an intervening delete.
  if (!LittleFS.rename(upload_temp, _path)) return fail(BookStorageResult::RenameFailed);
  release();
  return _result;
}

int book_storage_http_status(BookStorageResult result) {
  switch (result) {
    case BookStorageResult::Ok: return 200;
    case BookStorageResult::Busy: return 409;
    case BookStorageResult::NotFound: return 404;
    case BookStorageResult::MemoryFailed: return 503;
    case BookStorageResult::InvalidName:
    case BookStorageResult::InvalidPage:
    case BookStorageResult::IncompleteUpload:
    case BookStorageResult::InvalidUpload: return 400;
    default: return 500;
  }
}

const char *book_storage_message(BookStorageResult result) {
  switch (result) {
    case BookStorageResult::Ok: return "OK";
    case BookStorageResult::Busy: return "Book mutation busy";
    case BookStorageResult::InvalidName: return "Invalid root .txt filename";
    case BookStorageResult::InvalidPage: return "Invalid zero-based page";
    case BookStorageResult::NotFound: return "Book not found";
    case BookStorageResult::IncompleteUpload: return "Missing or incomplete upload";
    case BookStorageResult::InvalidUpload: return "Invalid upload sequence or multiple files";
    case BookStorageResult::OpenFailed: return "Upload open failed";
    case BookStorageResult::WriteFailed: return "Upload write incomplete";
    case BookStorageResult::RenameFailed: return "Upload commit failed";
    case BookStorageResult::CleanupFailed: return "Upload cleanup failed";
    case BookStorageResult::CacheFailed: return "Page cache removal failed";
    case BookStorageResult::BookmarkFailed: return "Bookmark persistence failed";
    case BookStorageResult::StateFailed: return "Reading state persistence failed";
    case BookStorageResult::MemoryFailed: return "Upload context allocation failed";
    case BookStorageResult::DeleteFailed: return "Book deletion failed";
  }
  return "Storage failed";
}
