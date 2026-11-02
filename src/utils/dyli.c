#include "dyli.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backend/logger.h"

/**
   Allocate necessary resource for dynamic list and update the capacity and
   count.
 */
DynamicListStatus dyli_create(void **entries, size_t *capacity, size_t *count,
                              size_t type_size, size_t num, const char *label) {

  *entries = calloc(num, type_size);
  *capacity = num;
  *count = 0;

  if (*entries == NULL) {
    logger_add(LoggerFlag_Error, "Couldn't create new dynamic list: %s\n",
               label);
    *capacity = 0;
    return DynamicListStatus_AllocFail;
  }

  return DynamicListStatus_Success;
}

/**
   Expand the dynamic list of 2n capacity.
 */
DynamicListStatus dyli_expand(void **entries, size_t *capacity, size_t *count,
                              size_t type_size, size_t scale,
                              const char *label) {

  if (*capacity == 0) {
    logger_add(LoggerFlag_Error,
               "Dynamic list '%s' has a capaicty of 0. Make sure it's been "
               "initialized corectly.\n",
               label);
    return DynamicListStatus_NotInit;
  }

  size_t new_capacity = scale * (*capacity);

  void *temp = (void *)realloc(*entries, new_capacity * type_size);

  if (temp == NULL) {
    logger_add(LoggerFlag_Error, "Couldn't expand list '%s' from %lu to %lu.",
               label, *capacity, new_capacity);
    return DynamicListStatus_AllocFail;
  }

  *entries = temp;
  *capacity = new_capacity;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_insert(void **entries, size_t *capacity, size_t *count,
                              size_t type_size, void *entry, size_t amount,
                              const char *label) {

  if (*entries == NULL || *capacity == 0) {
    logger_add(LoggerFlag_Error,
               "Dynamic list '%s' not initialized, insertion aborted. "
               "(Entries: <%p>, capacity: %lu, count: %lu)",
               label, *entries, *capacity, *count);
    return DynamicListStatus_NotInit;
  }

  while (*capacity < *count + amount) {
    if (dyli_expand(entries, capacity, count, type_size, 2, label) !=
        DynamicListStatus_Success)
      return DynamicListStatus_UndefError;
  }

  char *target = (char *)(*entries) + (*count * type_size);
  memcpy(target, entry, type_size * amount);
  *count += amount;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_insert_at(void **entries, size_t *capacity,
                                 size_t *count, size_t type_size,
                                 const void *entry, size_t index,
                                 float expand_threshold, const char *label) {
  if (!entries || !*entries || !capacity || !count) {
    logger_add(LoggerFlag_Error,
               "Dynamic list '%s' invalid or uninitialized (entries: %p).",
               label, *entries);
    return DynamicListStatus_NotInit;
  }

  if (index > *count) {
    logger_add(LoggerFlag_Error,
               "Dynamic list '%s' insert index %zu out of range (count=%zu).",
               label, index, *count);
    return DynamicListStatus_OutOfBound;
  }

  float usage =
      (*capacity > 0) ? ((float)(*count) / (float)(*capacity)) : 1.0f;
  if (usage >= expand_threshold) {
    if (dyli_expand(entries, capacity, count, type_size, 2, label) !=
        DynamicListStatus_Success) {
      return DynamicListStatus_UndefError;
    }
  }

  if (index < *count) {
    char *base = (char *)(*entries);
    memmove(base + (index + 1) * type_size, base + index * type_size,
            (*count - index) * type_size);
  }

  memcpy((char *)(*entries) + index * type_size, entry, type_size);
  (*count)++;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_empty(void *entries, size_t *count, size_t type_size) {

  memset(entries, 0, (*count) * type_size);
  *count = 0;
  return DynamicListStatus_Success;
}

DynamicListStatus dyli_free(void **entries, size_t *capacity, size_t *count) {

  // free set
  free(*entries);
  *entries = NULL;
  *capacity = 0;
  *count = 0;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_remove(void *entries, size_t *count, size_t type_size,
                              void *entry, const char *label) {

  void **list = (void **)entries;
  void *target = *(void **)entry;

  for (size_t i = 0; i < *count; i++) {

    if (list[i] == target) {

      if (i < *count - 1)
        memmove(&list[i], &list[i + 1], (*count - i - 1) * type_size);

      (*count)--;
      return DynamicListStatus_Success;
    }
  }

  return DynamicListStatus_UnfoundEntry;
}

DynamicListStatus dyli_remove_at_index(void *entries, size_t *count,
                                       size_t type_size, const size_t index,
                                       const char *label) {
  if (index >= *count)
    return DynamicListStatus_UnfoundEntry;

  char *list = (char *)entries;

  if (index < *count - 1) {
    memmove(list + index * type_size, list + (index + 1) * type_size,
            (*count - index - 1) * type_size);
  }

  (*count)--;
  return DynamicListStatus_Success;
}

void *dyli_new_entry(void **entries, size_t *capacity, size_t *count,
                     size_t type_size, const char *label) {

  if (*entries == NULL || *capacity == 0) {
    logger_add(LoggerFlag_Error,
               "Dynamic list '%s' not initialized, new entry aborted. "
               "(Entries: <%p>, capacity: %lu, count: %lu)",
               label, *entries, *capacity, *count);
    return NULL;
  }

  // Ensure capacity first
  if (*count == *capacity) {
    if (dyli_expand(entries, capacity, count, type_size, 2, label) !=
        DynamicListStatus_Success)
      return NULL;
  }

  // Compute pointer to new slot
  void *slot = (char *)(*entries) + ((*count) * type_size);

  // Zero-init for safety
  memset(slot, 0, type_size);

  // Increment count AFTER assignment
  (*count)++;

  return slot;
}

DynamicListStatus dyli_append(const void *src_entries, const size_t src_count,
                              void **dest_entries, size_t *dest_capacity,
                              size_t *dest_count, size_t type_size,
                              const char *label) {

  while (*dest_count + src_count >= *dest_capacity) {

    size_t new_capacity =
        (*dest_capacity > 0) ? 2 * *dest_capacity : src_count;

    void *temp_entries = realloc(*dest_entries, new_capacity * type_size);

    if (temp_entries) {
      *dest_capacity = new_capacity;
      *dest_entries = temp_entries;

    } else {
      logger_add(LoggerFlag_Error, "Couldn't transfert to %s.", label);
      return DynamicListStatus_AllocFail;
    }
  }

  memcpy((char *)(*dest_entries) + (*dest_count * type_size), src_entries,
         src_count * type_size);

  *dest_count += src_count;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_replace(const void *src_entries, const size_t src_count,
                               void **dest_entries, size_t *dest_capacity,
                               size_t *dest_count, size_t type_size,
                               const char *label) {

  while (src_count > *dest_capacity) {

    size_t new_capacity =
        (*dest_capacity > 0) ? 2 * *dest_capacity : src_count;

    void *temp_entries = realloc(*dest_entries, new_capacity * type_size);

    if (temp_entries) {
      *dest_capacity = new_capacity;
      *dest_entries = temp_entries;

    } else {
      logger_add(LoggerFlag_Error, "Couldn't transfert to %s.", label);
      return DynamicListStatus_AllocFail;
    }
  }

  memcpy((char *)(*dest_entries), src_entries, src_count * type_size);

  *dest_count = src_count;

  return DynamicListStatus_Success;
}

DynamicListStatus dyli_clone(const void *src_entries, const size_t src_count,
                             void **dest_entries, size_t *dest_capacity,
                             size_t *dest_count, size_t type_size,
                             const char *label) {

  size_t new_capacity = src_count;
  void *temp_entries = malloc(new_capacity * type_size);

  if (!temp_entries) {
    logger_add(LoggerFlag_Error, "Couldn't clone to %s.", label);
    return DynamicListStatus_AllocFail;
  }

  memcpy(temp_entries, src_entries, src_count * type_size);

  free(*dest_entries);
  *dest_entries = temp_entries;
  *dest_capacity = new_capacity;
  *dest_count = src_count;

  return DynamicListStatus_Success;
}


DynamicListStatus dyli_find_index(const void *entries, size_t count,
                                  size_t type_size, const void *target,
                                  size_t *out_index) {

  if (!entries || !target || !out_index || type_size == 0) {
    return DynamicListStatus_InvalidArgument; // or whatever you use
  }

  const unsigned char *base = (const unsigned char *)entries;

  for (size_t i = 0; i < count; i++) {
    if (memcmp(base + i * type_size, target, type_size) == 0) {
      *out_index = i;
      return DynamicListStatus_Success;
    }
  }

  return DynamicListStatus_UnfoundEntry;
}
