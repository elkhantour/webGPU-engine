#include "utils.h"
#include "backend/compute/mipmap.h"
#include "backend/logger.h"
#include <stdint.h>
/**
   Returns the sum of group uniforms, textures and samplers.
   Useful when creating each bind groups layout where the number of total
   entries is required.
 */
uint16_t shader_bind_group_entries_count(const ShaderBindGroup *group) {

  return group->uniforms.count + group->textures.count + group->samplers.count;
}

/**
   Check if the shader has every requirements before binding groups
   Also checks if the bind groups array isn't already at full capacity
 */

// TODO add more validation by uniforms type (UNIFORM/ TEX/ SAMPLER...) check
// if it doesn't overflow with max accepted count
bool shader_validate_binding(Shader *shader) {

  if (shader->bind_groups.count >= SHADER_MAX_BIND_GROUP) {
    logger_add(LoggerFlag_Error, "Bind group list at full capacity.");
    return SHADER_BIND_UNVALID;
  }

  return SHADER_BIND_VALID;
}

