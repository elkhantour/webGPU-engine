#include "compute.h"
#include "backend/context.h"
#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "utils/file.h"
#include "webgpu/webgpu.h"

/**
  Initialize the default pipeline with a preset descriptor
 */
void compute_pipeline_create(ComputePipeline *pipeline,
                             const ComputePipelineCreateDescriptor *desc) {

  // Define core data
  pipeline->id = reg_register(pipeline, RegEntryType_ComputePipeline);
  pipeline->handle = NULL;
  pipeline->label = desc->label;
  pipeline->shader_pso = desc->pso;
  pipeline->bindings = pipeline->shader_pso->bindings;

  char *source; // shader source code

  // store shader string in memory
  store_file(&source, desc->path);

  // compile shader module intro GPU device
  pipeline->module =
      rem_new_shader_module(source, pipeline->label, REMWriteFlag_FreeData);
}

/**
   Release pipeline if exists and create i new one
 */
void compute_pipeline_build(ComputePipeline *pipeline,
                            const WGPUPipelineLayout *layout) {

  // update bind group layout
  pipeline->layout = *layout;

  // DEBUG
  // if (pipeline->handle)
  //  compute_pipeline_destroy(pipeline);

  pipeline->handle = wgpuDeviceCreateComputePipeline(
      context_device(), &(WGPUComputePipelineDescriptor){
                            .label = pipeline->label,
                            .layout = pipeline->layout,
                            .compute =
                                (WGPUProgrammableStageDescriptor){
                                    .module = pipeline->module,
                                    .entryPoint = "main",
                                },
                        });
}

/**
   Release pipeline and set back the handle to null
 */
void compute_pipeline_destroy(ComputePipeline *pipeline) {

  // rem_destroy_shader_module(&pipeline->module);
  wgpuComputePipelineRelease(pipeline->handle);
  pipeline->handle = NULL;

  // DO NOT uncomment, it causes crashes,
  // probably cause the layout is still in use
  wgpuPipelineLayoutRelease(pipeline->layout);
  pipeline->layout = NULL;
}

// TODO: move to logger
void compute_pipeline_handle_validation_error(WGPUErrorType type,
                                              char const *message,
                                              void *userdata) {

  WGPUShaderModule module = (WGPUShaderModule)userdata;
  // DEBUG
  //printf("Validation error for module: %p\n", module);
  //printf("Validation error message: %s\n", message);
  // wgpuShaderModuleGetCompilationInfo(
  //     module, compute_pipeline_compilation_info_callback, NULL);
}

// TODO: move to logger
void compute_pipeline_compilation_info_callback(
    WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info,
    void *userdata) {

    //printf("Get info of: %p\n", userdata);

  if (status != WGPUCompilationInfoRequestStatus_Success) {
    printf("Failed to get compilation info!\n");
    return;
  }

  // Print the number of messages
  // printf("Shader compilation messages: %lu\n", info->messageCount);

  // Iterate over messages
  for (uint32_t i = 0; i < info->messageCount; i++) {
    const WGPUCompilationMessage *msg = &info->messages[i];

    // Map message type to string
    const char *typeStr = "Unknown";
    switch (msg->type) {
    case WGPUCompilationMessageType_Error:
      typeStr = "Error";
      break;
    case WGPUCompilationMessageType_Warning:
      typeStr = "Warning";
      break;
    case WGPUCompilationMessageType_Info:
      typeStr = "Info";
      break;
    case WGPUCompilationMessageType_Force32:
      typeStr = "Force32";
      break;
    }

    //printf("[%s] %s\n", typeStr, msg->message);

    // Optional: print location if available
    if (msg->lineNum != 0 || msg->linePos != 0) {
      printf("  at line %llu, column %llu\n", msg->lineNum, msg->linePos);
    }
  }
}
