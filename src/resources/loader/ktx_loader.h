#ifndef _LOADER_KTX_H_
#define _LOADER_KTX_H_

#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include <ktx.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  LoaderKTXStatus_Success,
  LoaderKTXStatus_FailLoad,
  LoaderKTXStatus_FailTranscode,
  LoaderKTXStatus_UnsupportedFormat,
  LoaderKTXStatus_UndefError,
} LoaderKTXStatus;

LoaderKTXStatus ktx_texture_destroy(ktxTexture2 **);

LoaderKTXStatus ktx_texture_load(ktxTexture2 **, const char *,
                                 const unsigned char *, size_t);

LoaderKTXStatus ktx_texture_get_mips(ktxTexture2 *, int, TextureMip *);

unsigned char *load_ktx2(const char *, const unsigned char *, size_t, int *,
                         int *, int *, int);

bool is_ktx2_memory(const unsigned char *, size_t);

bool has_ext(const char *, const char *);

unsigned char *load_ktx2_rgba8(const char *, int *, int *);

#endif
