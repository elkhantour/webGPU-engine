#ifndef _PATH_UTILS_H_
#define _PATH_UTILS_H_

// If adjusting those, make sure to sync with projects CMake resources mapping
#include <stdio.h>
#include <string.h>
#include "defines.h"

#define PATH_SHADER(filename) "./shaders/" filename
#define PATH_TEXTURE(filename) "./textures/" filename
#define PATH_FONT(filename) "./fonts/" filename
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

EXTERN_C_END

#endif
