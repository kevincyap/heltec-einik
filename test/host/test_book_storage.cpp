#include <cassert>
#include "../../src/state.cpp"
#include "../../src/book_storage.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;

bool reader_delete_book_cache(const char *filename) {
  std::string path(filename);
  path.replace(path.size() - 4, 4, ".pc");
  return !LittleFS.exists(path.c_str()) || LittleFS.remove(path.c_str());
}

typedef BookStorageResult R;
static const char *temp = "/.book-upload.tmp";
static const uint8_t bytes[] = {'n', 'e', 'w'};

static void reset() {
  LittleFS = LittleFSMock();
  _cache_valid = false;
  _cached_book = {};
  LittleFS.storage["/book.txt"] = {'o', 'l', 'd'};
  LittleFS.storage["/book.pc"] = {1, 2};
}

static void old_book() {
  assert((LittleFS.storage["/book.txt"] == std::vector<uint8_t>{'o', 'l', 'd'}));
}

static void replace() {
  BookUpload upload;
  assert(upload.begin("book.txt") == R::Ok);
  assert(upload.append(0, bytes, 1, false) == R::Ok);
  assert(upload.append(1, bytes + 1, 2, true) == R::Ok);
  assert(upload.commit() == R::Ok);
  assert(!LittleFS.exists(temp) && !LittleFS.exists("/book.pc"));
  assert(LittleFS.storage["/book.txt"] == std::vector<uint8_t>(bytes, bytes + 3));
}

int main() {
  char path[64];
  for (const char *name : {"", "/", ".txt", "/state.dat",
                           "/book.pc", "/book.pos", "/diagnostics.log", "../a.txt",
                           "/a/b.txt", "a\\b.txt", "//a.txt", "a\n.txt", "a\x7f.txt"}) {
    assert(!book_storage_path(name, path));
    assert(book_storage_delete(name) == R::InvalidName);
    BookUpload upload;
    assert(upload.begin(name) == R::InvalidName);
  }
  assert(book_storage_path("A book.TxT", path) && strcmp(path, "/A book.TxT") == 0);
  for (const char *name : {"Alice's Adventures.txt", "a\"quoted\".txt",
                           "caf\xc3\xa9.txt", ".hidden.txt", "part..two.txt"}) {
    assert(book_storage_path(name, path));
    assert(strcmp(path + 1, name) == 0);
  }
  std::string longest(58, 'a');
  longest += ".txt";
  assert(book_storage_path(longest.c_str(), path) && strlen(path) == 63);
  longest.insert(0, "a");
  assert(!book_storage_path(longest.c_str(), path));
  int page = -1;
  assert(book_storage_page("0", page) && page == 0);
  assert(book_storage_page("2147483647", page) && page == INT_MAX);
  for (const char *text : {"", "-1", "+1", " 2", "2 ", "3abc", "1.5", "2147483648"})
    assert(!book_storage_page(text, page));

  reset();
  assert(state_save("/book.txt", 9));
  assert(state_save_book_page("/book.txt", 9));
  replace(); // same-size replacement must invalidate cache, not bookmarks
  assert(state_load().page == 9 && state_load_book_page("/book.txt") == 9);
  assert(book_storage_delete("book.txt") == R::Ok);
  assert(!LittleFS.exists("/book.pos") && !state_load().valid);
  assert(book_storage_delete("book.txt") == R::NotFound);
  {
    BookUpload create;
    assert(create.begin("/new.txt") == R::Ok);
    assert(create.append(0, bytes, 3, true) == R::Ok);
    assert(create.commit() == R::Ok);
  }
  reset();
  assert(state_save("/other.txt", 5));
  assert(book_storage_delete("book.txt") == R::Ok);
  assert(state_load().valid && strcmp(state_load().filename, "/other.txt") == 0);

  reset();
  {
    BookUpload missing;
    assert(missing.commit() == R::IncompleteUpload);
  }
  {
    BookUpload empty;
    assert(empty.begin("empty.txt") == R::Ok);
    assert(empty.append(0, nullptr, 0, true) == R::Ok);
    assert(empty.commit() == R::Ok); // Empty text files are legitimate storage objects.
    assert(LittleFS.exists("/empty.txt") && LittleFS.storage["/empty.txt"].empty());
  }
  {
    BookUpload incomplete;
    assert(incomplete.begin("book.txt") == R::Ok);
    assert(incomplete.append(0, bytes, 3, false) == R::Ok);
    assert(incomplete.commit() == R::IncompleteUpload);
    old_book();
    assert(!LittleFS.exists(temp));
  }
  {
    BookUpload first, competing;
    assert(first.begin("book.txt") == R::Ok);
    assert(competing.begin("other.txt") == R::Busy);
    assert(book_storage_delete("book.txt") == R::Busy);
    assert(book_storage_set_page("book.txt", 0) == R::Busy);
    assert(first.append(1, bytes, 3, true) == R::InvalidUpload);
    assert(!LittleFS.exists(temp));
  }
  {
    BookUpload disconnect;
    assert(disconnect.begin("book.txt") == R::Ok);
    assert(disconnect.append(0, bytes, 3, true) == R::Ok);
  }
  old_book();
  assert(!LittleFS.exists(temp));
  {
    BookUpload multiple;
    assert(multiple.begin("book.txt") == R::Ok);
    assert(multiple.append(0, bytes, 3, true) == R::Ok);
    assert(multiple.begin("other.txt") == R::InvalidUpload);
    assert(multiple.commit() == R::InvalidUpload);
  }
  old_book();
  {
    BookUpload extra;
    assert(extra.begin("book.txt") == R::Ok);
    assert(extra.append(0, bytes, 3, true) == R::Ok);
    assert(extra.append(3, bytes, 1, true) == R::InvalidUpload);
  }
  for (int failure = 0; failure < 5; ++failure) {
    reset();
    BookUpload upload;
    if (failure == 0) LittleFS.fail_open_path = temp;
    R begin = upload.begin("book.txt");
    if (failure == 0) {
      assert(begin == R::OpenFailed);
    } else {
      assert(begin == R::Ok);
      if (failure == 1) LittleFS.write_limit = 1;
      R append = upload.append(0, bytes, 3, true);
      if (failure == 1) {
        assert(append == R::WriteFailed);
      } else {
        assert(append == R::Ok);
        if (failure == 2) LittleFS.fail_rename_path = "/book.txt";
        if (failure == 3) LittleFS.fail_remove_path = "/book.pc";
        if (failure == 4) LittleFS.fail_rename = true;
        assert(upload.commit() == (failure == 3 ? R::CacheFailed : R::RenameFailed));
      }
    }
    old_book();
    assert(!LittleFS.exists(temp));
    LittleFS.fail_open_path.clear();
    LittleFS.fail_remove_path.clear();
    LittleFS.fail_rename_path.clear();
    LittleFS.fail_rename = false;
    LittleFS.write_limit = size_t(-1);
    replace();
  }
  reset();
  LittleFS.storage[temp] = {1};
  LittleFS.fail_remove_path = temp;
  {
    BookUpload abandoned;
    assert(abandoned.begin("book.txt") == R::CleanupFailed);
  }
  LittleFS.fail_remove_path.clear();
  replace(); // Removes stale temp before retry.
  reset();
  {
    BookUpload cleanup;
    assert(cleanup.begin("book.txt") == R::Ok);
    LittleFS.fail_remove_path = temp;
    LittleFS.write_limit = 0;
    assert(cleanup.append(0, bytes, 3, true) == R::CleanupFailed);
  }
  LittleFS.fail_remove_path.clear();
  LittleFS.write_limit = size_t(-1);
  replace();

  for (const char *failed : {"/book.pc", "/book.pos", "/state.dat", "/book.txt"}) {
    reset();
    assert(state_save("/book.txt", 4));
    assert(state_save_book_page("/book.txt", 4));
    LittleFS.fail_remove_path = failed;
    R expected = strcmp(failed, "/book.pc") == 0 ? R::CacheFailed :
                 strcmp(failed, "/book.pos") == 0 ? R::BookmarkFailed :
                 strcmp(failed, "/state.dat") == 0 ? R::StateFailed : R::DeleteFailed;
    assert(book_storage_delete("book.txt") == expected);
    old_book();
    LittleFS.fail_remove_path.clear();
    assert(book_storage_delete("book.txt") == R::Ok);
  }
  reset();
  assert(state_save("/book.txt", 2));
  LittleFS.fail_open_path = "/state.dat";
  assert(book_storage_delete("book.txt") == R::StateFailed);
  old_book();
  LittleFS.fail_open_path.clear();
  LittleFS.storage["/state.dat"].resize(1);
  assert(book_storage_delete("book.txt") == R::StateFailed);
  assert(state_clear());
  assert(book_storage_delete("book.txt") == R::Ok);

  for (int failure = 0; failure < 8; ++failure) {
    reset();
    assert(state_save("/other.txt", 1));
    assert(state_save_book_page("/book.txt", 1));
    if (failure == 0) LittleFS.fail_open_path = "/book.pos.tmp";
    if (failure == 1) LittleFS.write_limit = 1;
    if (failure == 2) LittleFS.fail_rename_path = "/book.pos";
    if (failure == 3) LittleFS.fail_open_path = "/state.dat.tmp";
    if (failure == 4) LittleFS.fail_rename_path = "/state.dat";
    if (failure == 5) LittleFS.write_limit = sizeof(int);
    if (failure == 6) {
      LittleFS.storage["/book.pos.tmp"] = {1};
      LittleFS.fail_remove_path = "/book.pos.tmp";
    }
    if (failure == 7) {
      LittleFS.storage["/state.dat.tmp"] = {1};
      LittleFS.fail_remove_path = "/state.dat.tmp";
    }
    R expected = failure <= 2 || failure == 6 ? R::BookmarkFailed : R::StateFailed;
    assert(book_storage_set_page("book.txt", 2) == expected);
    assert(strcmp(state_load().filename, "/other.txt") == 0);
    LittleFS.fail_open_path.clear();
    LittleFS.fail_rename_path.clear();
    LittleFS.fail_remove_path.clear();
    LittleFS.write_limit = size_t(-1);
    assert(book_storage_set_page("book.txt", 2) == R::Ok);
    assert(state_load().page == 2 && state_load_book_page("/book.txt") == 2);
  }
  assert(book_storage_set_page("book.txt", -1) == R::InvalidPage);
  assert(book_storage_set_page("missing.txt", 0) == R::NotFound);
  for (int i = int(R::Ok); i <= int(R::DeleteFailed); ++i) {
    R result = static_cast<R>(i);
    assert(book_storage_message(result)[0]);
    assert((book_storage_http_status(result) == 200) == (result == R::Ok));
  }
  puts("Book storage transactions, mutation guard, metadata errors and retries passed");
}
