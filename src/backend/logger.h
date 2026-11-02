#ifndef _LOGGER_H_
#define _LOGGER_H_

#include "utils/color.h"
#include "utils/defines.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <webgpu/webgpu.h>

#define LOGGER_MAX_ENTRY 2048
#define LOGGER_FLAG_COUNT 11
#define LOGGER_MESSAGE_COUNT 256

#ifdef VERBOSE
#ifdef VERBOSE_LINE
#define PRINT_LINE(...) printf(" (%s:%d)\n", __FILE__, __LINE__)
#else
#define PRINT_LINE() printf("\n")
#endif

#define VERBOSE_PRINT(...)                                                     \
  do {                                                                         \
    printf(__VA_ARGS__);                                                       \
    PRINT_LINE();                                                              \
  } while (0)
#else
#define VERBOSE_PRINT(...) // no-op
#endif                     // VERBOSE

typedef enum {
  LoggerFlag_Print,
  LoggerFlag_Info,
  LoggerFlag_Error,
  LoggerFlag_Warning,
  LoggerFlag_Debug,
  LoggerFlag_ShaderCreate,
  LoggerFlag_MeshBuild,
  LoggerFlag_MeshCreate,
  LoggerFlag_Import,
  LoggerFlag_Success,
  LoggerFlag_Process,
} LoggerFlag;

typedef struct {
  char messages[LOGGER_MAX_ENTRY][LOGGER_MESSAGE_COUNT];
  uint64_t timestamps[LOGGER_MAX_ENTRY];
  LoggerFlag flags[LOGGER_MAX_ENTRY];
  size_t count;
} Logger;

extern Logger g_logger;

EXTERN_C_BEGIN

void logger_add(const LoggerFlag, const char *, ...);

// TODO replace logs

// clang-format off
#define LOG_METHOD(_)\
_(               , LoggerFlag_Print         ) \
_( wrn           , LoggerFlag_Warning       ) \
_( err           , LoggerFlag_Error         ) \
_( dbg           , LoggerFlag_Debug         ) \
_( shader_create , LoggerFlag_ShaderCreate  )\
_( mesh_build    , LoggerFlag_MeshBuild     )\
_( mesh_create   , LoggerFlag_MeshCreate    )\
_( imp           , LoggerFlag_Import        )\
_( scs           , LoggerFlag_Success       )\
_( proc          , LoggerFlag_Process       )
// clang-format on
#define _(Label, Type)                                                         \
  static inline void log_##Label(const char *fmt, ...) {                       \
    va_list args;                                                              \
    va_start(args, fmt);                                                       \
    logger_add(Type, fmt, args);                                               \
    va_end(args);                                                              \
  }
LOG_METHOD(_)
#undef _

static inline void dbg(const char *fm, ...) {
  va_list args;
  va_start(args, fm);

  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), fm, args);
  logger_add(LoggerFlag_Debug, "%s", buffer);

  va_end(args);
}

EXTERN_C_END

/*

  DEBUG_TIME

 */

#ifdef DEBUG_TIME

#define TIMER(name, code)                                                      \
  do {                                                                         \
    struct timespec _start, _end;                                              \
    clock_gettime(CLOCK_MONOTONIC, &_start);                                   \
    code clock_gettime(CLOCK_MONOTONIC, &_end);                                \
    double _elapsed = (_end.tv_sec - _start.tv_sec) * 1000.0 +                 \
                      (_end.tv_nsec - _start.tv_nsec) / 1000000.0;             \
    logger_add(LoggerFlag_Print, "%s > %.3f ms", name, _elapsed);              \
  } while (0)

#else
#define TIMER(name, code)                                                      \
  do {                                                                         \
    code                                                                       \
  } while (0)
#endif

#endif
