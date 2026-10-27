#pragma once

#include <stddef.h>
#include <string.h>

inline bool has_txt_extension(const char *name) {
  size_t len = strlen(name);
  if (len < 4) return false;
  const char *ext = name + len - 4;
  return ext[0] == '.' &&
         (ext[1] == 't' || ext[1] == 'T') &&
         (ext[2] == 'x' || ext[2] == 'X') &&
         (ext[3] == 't' || ext[3] == 'T');
}
