# ----------------------------
# Engine Sources
# ----------------------------
file(GLOB_RECURSE ENGINE_C_SRC        "${ENGINE_SRC_DIR}/*.c")
file(GLOB_RECURSE ENGINE_CXX_SRC      "${ENGINE_SRC_DIR}/*.cpp")
file(GLOB_RECURSE ENGINE_C_EXAMPLES   "examples/*.c")

# ----------------------------
# Public include paths for the consuming project (nkproject)
# ----------------------------
add_library(${PROJECT_NAME}_vendor_includes INTERFACE)

target_include_directories(${PROJECT_NAME}_vendor_includes INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples>
)
