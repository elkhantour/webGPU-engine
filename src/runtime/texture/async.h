#ifndef _TEXTURE_ASYNC_H_
#define _TEXTURE_ASYNC_H_

#include "backend/job_system.h"
#include "core.h"
#include "create.h"
#include "utils/defines.h"

/** Called on the main thread (from job_system_draw_callback / wait_idle).
 *  On success `texture` / `mip` points to a finished result that is only valid
 *  during the call: copy the struct (`Texture t = *texture;`). Ownership of
 *  the pixel buffers transfers to the callee.
 *  On failure the pointer is NULL and nothing needs to be freed. */
typedef void (*TextureAsyncDoneFn)(TextureStatus status, const Texture *texture,
                                   void *user);
typedef void (*TextureMipAsyncDoneFn)(TextureStatus status,
                                      const TextureMip *mip, void *user);

EXTERN_C_BEGIN

/* ---- async variants -------------------------------------------------------
 * Return TextureStatus_Pending if the job was queued: the real status arrives
 * later in `done`. Anything else means it was NOT queued (allocation failure
 * or NULL `done`) and `done` will not be called.
 * `done` is mandatory: it is the only way to receive the result.
 * The descriptor (path / data) is copied, so it may be a temporary.
 * Jobs are submitted with a NULL group, so submission never blocks.
 * ------------------------------------------------------------------------- */

TextureStatus
texture_create_from_file_async(JobSystem *, const TextureCreateFileDescriptor *,
                               TextureAsyncDoneFn done, void *user);

TextureStatus
texture_mip_create_from_file_async(JobSystem *,
                                   const TextureCreateFileDescriptor *,
                                   TextureMipAsyncDoneFn done, void *user);

TextureStatus
texture_mip_create_from_memory_async(JobSystem *,
                                     const TextureCreateMemoryDescriptor *,
                                     TextureMipAsyncDoneFn done, void *user);

TextureStatus
texture_create_cubemap_from_file_async(JobSystem *,
                                       const TextureCreateCubeMapDescriptor *,
                                       TextureAsyncDoneFn done, void *user);

EXTERN_C_END

#endif
