cmake_minimum_required(VERSION 3.22)
project(nkengine LANGUAGES C CXX)

# Set C and C++ Standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ----------------------------
# Engine Paths (relative to engine root)
# ----------------------------
set(ENGINE_SRC_DIR "src")

# ----------------------------
# Engine Sources
# ----------------------------
file(GLOB_RECURSE ENGINE_C_SRC "${ENGINE_SRC_DIR}/*.c")
file(GLOB_RECURSE ENGINE_CXX_SRC "${ENGINE_SRC_DIR}/*.cpp")
file(GLOB_RECURSE ENGINE_C_EXAMPLES "examples/*.c")

# ----------------------------
# Define Engine Includes
# These paths are PUBLIC for the consuming project (nkproject)
# ----------------------------
# Define the target as an INTERFACE library before setting its properties
add_library(${PROJECT_NAME}_vendor_includes INTERFACE)

# The target_include_directories command tells the compiler:
# "Add this path to your search list (-I<path>)."
target_include_directories(${PROJECT_NAME}_vendor_includes INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples>
)


# ----------------------------
# Vendor Library (imgui, cglm, stb, cgltf)
# ----------------------------
file(GLOB_RECURSE IMGUI
          "vendor/imgui/*.cpp"
          "vendor/imgui/*.h"
          )
file(GLOB_RECURSE CGLM
          "vendor/cglm/*.h"
          )
file(GLOB_RECURSE STB
          "vendor/stb/*.h"
          )
file(GLOB_RECURSE CGLTF
          "vendor/cgltf/*.h"
          )

# Create a static library for all vendors
add_library(${PROJECT_NAME}_vendors STATIC ${IMGUI} ${CGLM} ${STB} ${CGLTF})


# ----------------------------
# Preprocess WGSL shaders
# ----------------------------
# This custom command will generate the .wgsl files inside the build directory
# and the engine library will depend on them.
set(WGSL_OUT_FILES "")

# Use CMAKE_BINARY_DIR for a clean, reliable output location (relative to the build root)
set(WGSL_OUT_DIR "${CMAKE_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY ${WGSL_OUT_DIR})

file(GLOB_RECURSE WGSL_IN_FILES "${ENGINE_SRC_DIR}/backend/shaders/*.wgsl.in")
foreach(wgsl_in ${WGSL_IN_FILES})

    get_filename_component(fname ${wgsl_in} NAME_WE)
    set(wgsl_out "${WGSL_OUT_DIR}/${fname}.wgsl")

    add_custom_command(
        OUTPUT ${wgsl_out}
        COMMAND cpp
	ARGS -P ${MACROS} ${wgsl_in} > ${wgsl_out}
        DEPENDS ${wgsl_in}
        COMMENT "Preprocessing ${wgsl_in}"
    )
    
    list(APPEND WGSL_OUT_FILES ${wgsl_out})
endforeach()

# ----------------------------
# Preprocess Slang shaders
# ----------------------------
find_program(SLANGC slangc REQUIRED)

# include slang modules
set(SLANG_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/backend/shaders")

# compile slang main shaders
file(GLOB_RECURSE SLANG_IN_FILES
    CONFIGURE_DEPENDS
    "${ENGINE_SRC_DIR}/backend/shaders/render/*.slang"
    "${ENGINE_SRC_DIR}/backend/shaders/compute/*.slang"
)

file(GLOB_RECURSE SLANG_MODULE_FILES
    CONFIGURE_DEPENDS
    "${SLANG_MODULES_DIR}/*.slang"
)

foreach(slang_in IN LISTS SLANG_IN_FILES)

    get_filename_component(fname "${slang_in}" NAME_WE)
    set(wgsl_out "${WGSL_OUT_DIR}/${fname}.wgsl")

    add_custom_command(
        OUTPUT "${wgsl_out}"
        COMMAND "${SLANGC}"
            "-I${SLANG_MODULES_DIR}"
            "${slang_in}"
            -target wgsl
            -matrix-layout-column-major
            -o "${wgsl_out}"
        DEPENDS
            "${slang_in}"
            ${SLANG_MODULE_FILES}
        COMMENT "Compiling Slang ${slang_in} -> WGSL"
        VERBATIM
    )

    list(APPEND WGSL_OUT_FILES "${wgsl_out}")
endforeach()


# ----------------------------
# Resource routing
# ----------------------------
set(ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/resources/assets")
set(GLTF_ROOT  "${ASSET_ROOT}/gltf")

# Directories to scan (the gltf root is included, but handled specially below)
set(RES_SCAN_ROOTS
    "${GLTF_ROOT}"
    "${ASSET_ROOT}/font"
    "${ASSET_ROOT}/texture"
    "${ASSET_ROOT}/mbin"
    "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/backend/shaders/render"
    "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/backend/shaders/compute"
)

# extension (lowercase) -> virtual directory
set(RES_DEST_wgsl shaders)
set(RES_DEST_jpg  textures)
set(RES_DEST_jpeg textures)
set(RES_DEST_png  textures)
set(RES_DEST_hdr  textures)
set(RES_DEST_ttf  fonts)
set(RES_DEST_mbin mbin)
set(RES_DEST_gltf gltf)
set(RES_DEST_glb  gltf)
set(RES_DEST_bin  gltf)

set(PRELOAD_ARGS_LIST "")
set(PRELOAD_FILE_COUNT 0)

# Registers one file: <abs source path> -> /<virtual path>
function(register_preload src virt)
    file(RELATIVE_PATH rel_src "${CMAKE_BINARY_DIR}" "${src}")
    message(STATUS "[preload] ${virt}  <=  ${rel_src}")
    set(PRELOAD_ARGS_LIST ${PRELOAD_ARGS_LIST} "--preload-file" "${rel_src}@${virt}" PARENT_SCOPE)
    math(EXPR n "${PRELOAD_FILE_COUNT} + 1")
    set(PRELOAD_FILE_COUNT ${n} PARENT_SCOPE)
endfunction()

# 1) Static assets, routed by extension
foreach(root IN LISTS RES_SCAN_ROOTS)
    file(GLOB_RECURSE ALL_FILES CONFIGURE_DEPENDS "${root}/*")

    foreach(f IN LISTS ALL_FILES)
        get_filename_component(ext "${f}" LAST_EXT)      # ".JPG"
	if(ext STREQUAL "")
               continue()                                   # no extension: skip
    	endif()
        string(TOLOWER "${ext}" ext)
        string(SUBSTRING "${ext}" 1 -1 ext)              # "jpg"

        if(NOT DEFINED RES_DEST_${ext})
            continue()                                   # unknown ext: skipped (.wgsl.in, .blend, ...)
        endif()

        cmake_path(IS_PREFIX GLTF_ROOT "${f}" NORMALIZE under_gltf)
        if(under_gltf)
            # glTF assets keep their layout so relative URIs resolve
            file(RELATIVE_PATH rel "${GLTF_ROOT}" "${f}")
            register_preload("${f}" "/gltf/${rel}")
        else()
            file(RELATIVE_PATH rel "${root}" "${f}")
            register_preload("${f}" "/${RES_DEST_${ext}}/${rel}")
        endif()
    endforeach()
endforeach()

# 2) Generated shaders: they don't exist at configure time, so use the known output list
foreach(f IN LISTS WGSL_OUT_FILES)
    get_filename_component(fname "${f}" NAME)
    get_filename_component(dname "${f}" NAME_WE)
    register_preload("${f}" "/shaders/${dname}/${fname}")
endforeach()

message(STATUS "[preload] Total files: ${PRELOAD_FILE_COUNT}")

# Convert the list into a single, space-separated string for LINK_FLAGS
string(JOIN " " PRELOAD_FLAGS_STRING ${PRELOAD_ARGS_LIST})

# Export to parent scope
set(${PROJECT_NAME}_PRELOAD_FLAGS ${PRELOAD_FLAGS_STRING} PARENT_SCOPE)
set(${PROJECT_NAME}_WGSL_OUT_FILES ${WGSL_OUT_FILES} PARENT_SCOPE)

# ----------------------------
# Engine Library Definition
# ----------------------------
add_library(${PROJECT_NAME} STATIC
    ${ENGINE_C_SRC}
    ${ENGINE_CXX_SRC}
    ${ENGINE_C_EXAMPLES}
    ${WGSL_OUT_FILES} # Dependency: Ensures shaders are generated before linking
)


# ----------------------------
# Engine Macros (Compiler Definitions)
# ----------------------------
set(MACROS
    -DCGLM_FORCE_DEPTH_ZERO_TO_ONE
    -DVERBOSE
    -DENGINE_EDITOR
    -DDEBUG_TIME
    -DAO_BAKE_HIT_COUNT
)


# -fsanitize=address creates too much locals and throw wasm error
# see: https://github.com/emscripten-core/emscripten/issues/19346#issuecomment-1673579716
set(ASAN
    -fsanitize=undefined
    -ferror-limit=0
    -g
)

set(FLAGS
   -pthread
)

# Apply engine-specific macros
target_compile_options(${PROJECT_NAME} PUBLIC ${MACROS})
target_compile_options(${PROJECT_NAME} PRIVATE ${FLAGS} ${ASAN})

# Link the engine to its vendor libraries and set public includes
target_link_libraries(${PROJECT_NAME} PUBLIC ${PROJECT_NAME}_vendors)
target_link_libraries(${PROJECT_NAME} PUBLIC ${PROJECT_NAME}_vendor_includes)

# Required for Emscripten's WebGPU support if targeting WASM
target_link_libraries(${PROJECT_NAME} PUBLIC webgpu)

target_link_libraries(${PROJECT_NAME} PRIVATE ${ASAN})
