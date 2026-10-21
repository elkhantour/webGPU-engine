#ifndef _PATH_UTILS_H_
#define _PATH_UTILS_H_

// If adjusting those, make sure to sync with projects CMake resources mapping
#include "defines.h"
#include <stdio.h>
#include <string.h>

#define PATH_SHADER(filename) "./shaders/" filename
#define PATH_TEXTURE(filename) "./texture/" filename
#define PATH_FONT(filename) "./font/" filename
#define PATH_GLTF(filename) "./gltf/" filename
#define PATH_MBIN(filename) "./mbin/" filename

EXTERN_C_BEGIN

static inline int file_dir(const char *path, char *out, const size_t out_size) {
  const char *slash = strrchr(path, '/');
  size_t len;

  if (slash == NULL) {
    /* No directory part: use current directory */
    if (out_size < 2)
      return -1;
    strcpy(out, ".");
    return 0;
  }

  if (slash == path)
    len = 1; /* "/file" -> "/" */
  else
    len = (size_t)(slash - path); /* "a/b/file" -> "a/b" */

  if (len + 1 > out_size)
    return -1;

  memcpy(out, path, len);
  out[len] = '\0';
  return 0;
}

// Replace the extension of `path` in-place (or append if there is none).
static inline void path_set_ext(char *path, size_t cap, const char *ext) {
  char *slash = strrchr(path, '/');
  char *bslash = strrchr(path, '\\');
  if (bslash > slash)
    slash = bslash;

  char *dot = strrchr(path, '.');
  size_t len =
      (dot && (!slash || dot > slash)) ? (size_t)(dot - path) : strlen(path);
  snprintf(path + len, cap - len, ".%s", ext);
}

static inline bool path_has_ext(const char *path, const char *ext) {
  const char *dot = path ? strrchr(path, '.') : NULL;
  if (!dot)
    return false;
#ifdef _WIN32
  return _stricmp(dot + 1, ext) == 0;
#else
  return strcasecmp(dot + 1, ext) == 0;
#endif
}

EXTERN_C_END

#endif
