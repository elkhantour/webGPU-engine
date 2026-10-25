#include "async.h"
#include "backend/logger.h"
#include "runtime/texture/create.h"
#include <string.h>

typedef enum {
  TextureJobKind_File,
  TextureJobKind_MipFile,
  TextureJobKind_MipMemory,
  TextureJobKind_CubeMap,
} TextureJobKind;

/** Heap job payload. Owns the result storage and copies of path / data.
 *  Freed in texture_job_done(). */
typedef struct {
  TextureJobKind kind;
  TextureStatus status;

  // result storage, only handed to the callback once complete
  Texture tex;                      // File
  TextureMip mip;                   // MipFile, MipMemory
  Texture cube[TEXTURE_CUBE_LAYER]; // CubeMap, order: +X -X +Y -Y +Z -Z

  TextureAsyncDoneFn cb_tex;
  TextureMipAsyncDoneFn cb_mip;
  void *user;

  // descriptor copy
  TextureResolution width;
  TextureResolution height;
  uint8_t channels;
  bool flip;
  char *path;                     // owned (File, MipFile)
  unsigned char *data;            // owned (MipMemory)
  char *side[TEXTURE_CUBE_LAYER]; // owned (CubeMap)
  size_t size;
} TextureJob;

static char *dup_string(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

/** Worker thread: only CPU work (disk, decode, downscale). No WebGPU here. */
static void texture_job_work(void *p) {
  TextureJob *j = (TextureJob *)p;

  switch (j->kind) {
  case TextureJobKind_File:
    j->status =
        texture_create_from_file(&j->tex, &(TextureCreateFileDescriptor){
                                              .width = j->width,
                                              .height = j->height,
                                              .channels = j->channels,
                                              .path = j->path,
                                              .flip = j->flip,
                                          });
    break;
  case TextureJobKind_MipFile:
    j->status =
        texture_mip_create_from_file(&j->mip, &(TextureCreateFileDescriptor){
                                                  .width = j->width,
                                                  .height = j->height,
                                                  .channels = j->channels,
                                                  .path = j->path,
                                                  .flip = j->flip,
                                              });
    break;
  case TextureJobKind_MipMemory:
    j->status = texture_mip_create_from_memory(&j->mip,
                                               &(TextureCreateMemoryDescriptor){
                                                   .width = j->width,
                                                   .height = j->height,
                                                   .channels = j->channels,
                                                   .data = j->data,
                                                   .size = j->size,
                                                   .flip = j->flip,
                                               });
    break;

  case TextureJobKind_CubeMap: {
    size_t loaded = 0;
    for (; loaded < TEXTURE_CUBE_LAYER; loaded++) {
      j->status = texture_create_from_file(&j->cube[loaded],
                                           &(TextureCreateFileDescriptor){
                                               .width = j->width,
                                               .height = j->height,
                                               .channels = j->channels,
                                               .path = j->side[loaded],
                                               .flip = j->flip,
                                           });
      if (j->status != TextureStatus_Success)
        break;
    }

    // all-or-nothing: drop the faces that did decode
    if (j->status != TextureStatus_Success)
      for (size_t i = 0; i < loaded; i++)
        texture_destroy(&j->cube[i]);
    break;
  }
  }
}

/** Main thread: hand the finished result (or NULL) to the caller, then
 *  release the job. Buffer ownership moves to the callback on success. */
static void texture_job_done(void *p) {
  TextureJob *j = (TextureJob *)p;
  const bool ok = j->status == TextureStatus_Success;

  switch (j->kind) {
  case TextureJobKind_File:
    j->cb_tex(j->status, ok ? &j->tex : NULL, j->user);
    break;
  case TextureJobKind_CubeMap:
    if (!ok)
      logger_add(LoggerFlag_Error, "Couldn't read cubemap texture.");
    j->cb_tex(j->status, ok ? j->cube : NULL, j->user);
    break;
  default:
    j->cb_mip(j->status, ok ? &j->mip : NULL, j->user);
    break;
  }

  free(j->path);
  free(j->data);
  for (size_t i = 0; i < TEXTURE_CUBE_LAYER; i++)
    free(j->side[i]);
  free(j);
}

/** Allocates the job (copying path / data) and queues it.
 *  Returns TextureStatus_Pending when queued. */
static TextureStatus texture_job_submit(JobSystem *js, const TextureJob *proto,
                                        const char *path,
                                        const unsigned char *data) {
  TextureJob *j = (TextureJob *)calloc(1, sizeof *j);
  if (!j)
    return TextureStatus_AllocFail;
  *j = *proto;

  if (path) {
    j->path = dup_string(path);
    if (!j->path) {
      free(j);
      return TextureStatus_AllocFail;
    }
  }

  if (data && j->size) {
    j->data = (unsigned char *)malloc(j->size);
    if (!j->data) {
      free(j->path);
      free(j);
      return TextureStatus_AllocFail;
    }
    memcpy(j->data, data, j->size);
  }

  job_system_submit(js, NULL, texture_job_work, texture_job_done, j);
  return TextureStatus_Pending;
}

TextureStatus
texture_create_from_file_async(JobSystem *js,
                               const TextureCreateFileDescriptor *desc,
                               TextureAsyncDoneFn done, void *user) {
  if (!done)
    return TextureStatus_AllocFail;

  TextureJob proto = {
      .kind = TextureJobKind_File,
      .cb_tex = done,
      .user = user,
      .width = desc->width,
      .height = desc->height,
      .channels = desc->channels,
      .flip = desc->flip,
  };
  return texture_job_submit(js, &proto, desc->path, NULL);
}

TextureStatus texture_create_cubemap_from_file_async(
    JobSystem *js, const TextureCreateCubeMapDescriptor *desc,
    TextureAsyncDoneFn done, void *user) {
  if (!done)
    return TextureStatus_AllocFail;

  // put path in order
  const char *path_sort[TEXTURE_CUBE_LAYER] = {
      desc->path->right,  // +X
      desc->path->left,   // -X
      desc->path->top,    // +Y
      desc->path->bottom, // -Y
      desc->path->front,  // +Z
      desc->path->back,   // -Z
  };

  for (size_t i = 0; i < TEXTURE_CUBE_LAYER; i++)
    if (!path_sort[i])
      return TextureStatus_FileError;

  TextureJob *j = (TextureJob *)calloc(1, sizeof *j);
  if (!j)
    return TextureStatus_AllocFail;

  j->kind = TextureJobKind_CubeMap;
  j->cb_tex = done;
  j->user = user;
  j->width = desc->resolution;
  j->height = desc->resolution;
  j->channels = TextureChannel_Undefined;
  j->flip = false;

  // copy the paths: the caller's strings may be gone by the time the worker
  // runs
  for (size_t i = 0; i < TEXTURE_CUBE_LAYER; i++) {
    j->side[i] = dup_string(path_sort[i]);
    if (!j->side[i]) {
      for (size_t k = 0; k < i; k++)
        free(j->side[k]);
      free(j);
      return TextureStatus_AllocFail;
    }
  }

  job_system_submit(js, NULL, texture_job_work, texture_job_done, j);
  return TextureStatus_Pending;
}

TextureStatus
texture_mip_create_from_file_async(JobSystem *js,
                                   const TextureCreateFileDescriptor *desc,
                                   TextureMipAsyncDoneFn done, void *user) {
  if (!done)
    return TextureStatus_AllocFail;

  TextureJob proto = {
      .kind = TextureJobKind_MipFile,
      .cb_mip = done,
      .user = user,
      .width = desc->width,
      .height = desc->height,
      .channels = desc->channels,
      .flip = desc->flip,
  };
  return texture_job_submit(js, &proto, desc->path, NULL);
}

TextureStatus
texture_mip_create_from_memory_async(JobSystem *js,
                                     const TextureCreateMemoryDescriptor *desc,
                                     TextureMipAsyncDoneFn done, void *user) {
  if (!done)
    return TextureStatus_AllocFail;

  TextureJob proto = {
      .kind = TextureJobKind_MipMemory,
      .cb_mip = done,
      .user = user,
      .width = desc->width,
      .height = desc->height,
      .channels = desc->channels,
      .flip = desc->flip,
      .size = desc->size,
  };

  // The buffer is copied too: the caller's memory may be gone by the time the
  // worker runs. Drop the copy if you guarantee it stays valid.
  return texture_job_submit(js, &proto, NULL, desc->data);
}
