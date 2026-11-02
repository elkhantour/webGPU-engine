#ifndef _UTILS_MATH_H_
#define _UTILS_MATH_H_

#include <cglm/cglm.h>

#include "runtime/geometry/vertex/vertex.h"
#include "utils/defines.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define PI 3.14159265359

EXTERN_C_BEGIN

float randf();
int clamp(const int, const int min, const int max);
float clampf(const float, const float, const float max);

EXTERN_C_END

#endif
