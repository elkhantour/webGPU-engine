#ifndef _LOADER_KTX_H_
#define _LOADER_KTX_H_

#include <stdbool.h>
#include <stddef.h>

unsigned char *load_ktx2(const char *, const unsigned char *, size_t, int *,
                         int *, int *, int);

bool is_ktx2_memory(const unsigned char *, size_t);

bool has_ext(const char *, const char *);

unsigned char *load_ktx2_rgba8(const char *, int *, int *);

#endif
