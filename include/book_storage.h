#pragma once

#include <LittleFS.h>

enum class BookStorageResult {
  Ok, Busy, InvalidName, InvalidPage, NotFound, IncompleteUpload,
  InvalidUpload, OpenFailed, WriteFailed, RenameFailed, CleanupFailed,
  CacheFailed, BookmarkFailed, StateFailed, MemoryFailed, DeleteFailed
};

bool book_storage_path(const char *name, char path[64]);
bool book_storage_page(const char *text, int &page);
BookStorageResult book_storage_delete(const char *name);
BookStorageResult book_storage_set_page(const char *name, int page);
int book_storage_http_status(BookStorageResult result);
const char *book_storage_message(BookStorageResult result);

// Owns the mutation guard until commit/abort; never buffers book contents.
class BookUpload {
 public:
  BookUpload();
  ~BookUpload();
  BookStorageResult begin(const char *name);
  BookStorageResult append(size_t offset, const uint8_t *data, size_t count, bool final);
  BookStorageResult commit();
  BookStorageResult reject();
  BookStorageResult result() const { return _result; }
  const char *path() const { return _path; }
  size_t size() const { return _size; }

 private:
  BookUpload(const BookUpload &) = delete;
  BookUpload &operator=(const BookUpload &) = delete;
  BookStorageResult fail(BookStorageResult result);
  void release();
  File _file;
  char _path[64];
  size_t _size;
  BookStorageResult _result;
  bool _owns;
  bool _started;
  bool _final;
};
