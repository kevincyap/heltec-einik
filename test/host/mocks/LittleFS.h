#pragma once
#include <string>
#include <vector>

class File {
 public:
  File() : _files(nullptr), _index(-1), _next(0) {}
  explicit File(const std::vector<std::string> *files, int index = -1)
    : _files(files), _index(index), _next(0) {}
  explicit operator bool() const { return _files != nullptr; }
  bool isDirectory() const { return _files && _index == -1; }
  const char *name() const { return (*_files)[_index].c_str(); }
  File openNextFile() {
    if (!_files || _next >= _files->size()) return File();
    return File(_files, int(_next++));
  }
  void close() { _files = nullptr; }

 private:
  const std::vector<std::string> *_files;
  int _index;
  size_t _next;
};

struct LittleFSMock {
  std::vector<std::string> files;
  bool begin(bool) { return true; }
  bool exists(const char *) { return true; }
  File open(const char *path) {
    return strcmp(path, "/") == 0 ? File(&files) : File();
  }
};
extern LittleFSMock LittleFS;
