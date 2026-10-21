cmake_minimum_required(VERSION 3.22)
project(nkengine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Engine paths (relative to engine root)
set(ENGINE_SRC_DIR "src")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Order matters: later modules consume variables set by earlier ones.
include(EngineOptions)    # MACROS, ASAN, FLAGS
include(EngineSources)    # ENGINE_*_SRC, <name>_vendor_includes
include(EngineVendors)    # <name>_vendors
include(EngineTextureCompression)  # KTX converter
include(EngineShaders)    # WGSL_OUT_FILES (wgsl.in + slang)
include(EngineResources)  # preload flags (exported to parent)
include(EngineTarget)     # the engine library itself
