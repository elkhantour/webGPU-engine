#ifndef _FREE_LIST_H_
#define _FREE_LIST_H_

#include "utils/defines.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
  FreeListStatus_Success,
  FreeListStatus_AllocFail,
  FreeListStatus_NotInit,
  FreeListStatus_OutOfBound,
  FreeListStatus_UnfoundEntry,
  FreeListStatus_UndefError
} FreeListStatus;

#define FREE_LIST(Type)                                                        \
  struct {                                                                     \
    Type *entries;                                                             \
    uint8_t *used;                                                             \
    size_t capacity;                                                           \
    size_t count;                                                              \
    size_t type_size;                                                          \
    const char *label;                                                         \
  }

EXTERN_C_BEGIN

//             entries, used,       capacity, count,   type_size, num, label
FreeListStatus frli_create(void **, uint8_t **, size_t *, size_t *, size_t,
                           size_t, const char *);

FreeListStatus frli_empty(void *, uint8_t *, size_t, size_t *, size_t);

FreeListStatus frli_free(void **, uint8_t **, size_t *, size_t *);

void *frli_new_entry(void **, uint8_t *, size_t *, size_t *, size_t, size_t *,
                     const char *);

FreeListStatus frli_remove(void *, uint8_t *, size_t, size_t *, size_t, void *,
                           const char *);

FreeListStatus frli_remove_at_index(void *, uint8_t *, size_t, size_t *, size_t,
                                    size_t, const char *);

//  src_entries, src_count, dest_entries, dest_used, dest_capacity, dest_count
FreeListStatus frli_append(const void *, size_t, void **, uint8_t *, size_t *,
                           size_t *, size_t, const char *);

FreeListStatus frli_replace(const void *, size_t, void **, uint8_t *, size_t *,
                            size_t *, size_t, const char *);

FreeListStatus frli_clone(const void *, size_t, void **, uint8_t **, size_t *,
                          size_t *, size_t, const char *);

EXTERN_C_END

/* ---------- helpers: the only place that knows the argument order ----------
 */

#define FRLI__TSIZE(l) sizeof(*(l).entries)
#define FRLI__LABEL(l) #l

/* pointer forms, for functions that may reallocate (entries, used, cap, count)
 */
#define FRLI__PTRS2(l)                                                         \
  (void **)&(l).entries, &(l).used, &(l).capacity, &(l).count

/* mixed form used by new_entry/append/replace: void** entries, uint8_t* used,
   size_t* capacity, size_t* count */
#define FRLI__PTRS_MIXED(l)                                                    \
  (void **)&(l).entries, (l).used, &(l).capacity, &(l).count

/* by-value form used by remove/empty: entries, used, capacity (value), count*
 */
#define FRLI__VALS(l) (l).entries, (l).used, (l).capacity, &(l).count

/* ---------- wrappers: pass only the list ---------- */

/* frli_create(entries**, used**, cap*, count*, type_size, num, label) */
#define FRLI_CREATE(l, initial_cap)                                            \
  frli_create(FRLI__PTRS2(l), FRLI__TSIZE(l), (initial_cap), FRLI__LABEL(l))

#define FRLI_FREE(l)                                                           \
  frli_free((void **)&(l).entries, &(l).used, &(l).capacity, &(l).count)

/* frli_empty(entries, used, cap, count*, type_size) */
#define FRLI_EMPTY(l) frli_empty(FRLI__VALS(l), FRLI__TSIZE(l))

/* returns typed pointer to the new slot; out_index is an lvalue size_t */
#define FRLI_NEW_ENTRY(l, out_index)                                           \
  ((__typeof__((l).entries))frli_new_entry(                                    \
      FRLI__PTRS_MIXED(l), FRLI__TSIZE(l), &(out_index), FRLI__LABEL(l)))

/* remove by pointer to the element */
#define FRLI_REMOVE(l, item_ptr)                                               \
  frli_remove(FRLI__VALS(l), FRLI__TSIZE(l), (item_ptr), FRLI__LABEL(l))

#define FRLI_REMOVE_AT(l, index)                                               \
  frli_remove_at_index(FRLI__VALS(l), FRLI__TSIZE(l), (index), FRLI__LABEL(l))

/* src_entries, src_count, then dest pieces */
#define FRLI_APPEND(l, src_ptr, n)                                             \
  frli_append((src_ptr), (n), FRLI__PTRS_MIXED(l), FRLI__TSIZE(l),             \
              FRLI__LABEL(l))

#define FRLI_REPLACE(l, src_ptr, n)                                            \
  frli_replace((src_ptr), (n), FRLI__PTRS_MIXED(l), FRLI__TSIZE(l),            \
               FRLI__LABEL(l))

/* clone src -> dst (dst gets entries**, used**) */
#define FRLI_CLONE(dst, src)                                                   \
  frli_clone((src).entries, (src).count, (void **)&(dst).entries, &(dst).used, \
             &(dst).capacity, &(dst).count, FRLI__TSIZE(dst),                  \
             FRLI__LABEL(dst))

#endif
