#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

enum SeekMode { SeekSet };

class File {
 public:
  File() : _files(nullptr), _index(-1), _next(0) {}
  explicit File(const std::vector<std::string> *files, int index = -1)
    : _files(files), _index(index), _next(0) {}
  File(std::vector<uint8_t> *data, const char *path, const size_t *write_limit)
    : _files(nullptr), _index(-1), _next(0), _data(data),
      _name(path), _write_limit(write_limit) {}
  explicit operator bool() const { return _files != nullptr || _data != nullptr; }
  bool isDirectory() const { return _files && _index == -1; }
  const char *name() const { return _data ? _name.c_str() : (*_files)[_index].c_str(); }
  File openNextFile() {
    if (!_files || _next >= _files->size()) return File();
    return File(_files, int(_next++));
  }
  size_t size() const { return _data ? _data->size() : 0; }
  bool seek(size_t pos, SeekMode) {
    if (!_data || pos > _data->size()) return false;
    _position = pos;
    return true;
  }
  size_t read(uint8_t *out, size_t count) {
    if (!_data) return 0;
    count = std::min(count, _data->size() - _position);
    if (count) memcpy(out, _data->data() + _position, count);
    _position += count;
    return count;
  }
  size_t write(const uint8_t *in, size_t count) {
    if (!_data) return 0;
    count = std::min(count, *_write_limit);
    if (_position + count > _data->size()) _data->resize(_position + count);
    if (count) memcpy(_data->data() + _position, in, count);
    _position += count;
    return count;
  }
  void close() { _files = nullptr; _data = nullptr; }
  void append() { _position = size(); }

 private:
  const std::vector<std::string> *_files;
  int _index;
  size_t _next;
  std::vector<uint8_t> *_data = nullptr;
  std::string _name;
  size_t _position = 0;
  const size_t *_write_limit = nullptr;
};

struct LittleFSMock {
  std::vector<std::string> files;
  std::map<std::string, std::vector<uint8_t>> storage;
  int write_opens = 0;
  bool fail_write_open = false;
  std::string fail_open_path;
  std::string fail_remove_path;
  std::string fail_rename_path;
  bool fail_rename = false;
  size_t write_limit = size_t(-1);
  bool begin(bool) { return true; }
  bool exists(const char *path) {
    if (storage.count(path)) return true;
    for (const std::string &name : files)
      if (name == path || "/" + name == path) return true;
    return false;
  }
  bool remove(const char *path) {
    if (fail_remove_path == path) return false;
    return storage.erase(path) != 0;
  }
  bool rename(const char *from, const char *to) {
    if (!storage.count(from) || fail_rename || fail_rename_path == to) return false;
    storage[to] = storage[from];
    storage.erase(from);
    return true;
  }
  File open(const char *path, const char *mode = "r") {
    if (fail_open_path == path) return File();
    if (strcmp(path, "/") == 0) return File(&files);
    if (strcmp(mode, "w") == 0 || strcmp(mode, "a") == 0) {
      write_opens++;
      if (fail_write_open) return File();
      if (strcmp(mode, "w") == 0) storage[path].clear();
      else if (!storage.count(path)) storage[path] = {};
    } else if (!storage.count(path)) {
      return File();
    }
    File file(&storage[path], path, &write_limit);
    if (strcmp(mode, "a") == 0) file.append();
    return file;
  }
};
extern LittleFSMock LittleFS;
