# ----------------------------
# Resource routing (Emscripten --preload-file flags)
# ----------------------------
set(ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/resources/assets")
set(GLTF_ROOT  "${ASSET_ROOT}/gltf")

# KTX: jpg/jpeg/png are taken from the compressed output instead of the sources
set(COMPRESSED_ROOT "${CMAKE_BINARY_DIR}/compressed_assets")
set(KTX_EXT "ktx2" CACHE STRING "Extension of the compressed texture files")
set(KTX_SOURCE_EXTS jpg jpeg png)

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
# TODO: remove the RESOURCE_ROOT instead of rerouting manually
set(RES_DEST_wgsl shaders)
set(RES_DEST_jpg  texture)
set(RES_DEST_jpeg texture)
set(RES_DEST_png  texture)
set(RES_DEST_hdr  texture)
set(RES_DEST_ttf  font)
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
        get_filename_component(ext "${f}" LAST_EXT)
        if(ext STREQUAL "")
            continue()
        endif()
        string(TOLOWER "${ext}" ext)
        string(SUBSTRING "${ext}" 1 -1 ext)

        if(NOT DEFINED RES_DEST_${ext})
            continue()
        endif()

        # Default: preload the source file under its own name
        set(src  "${f}")
        set(vext "")                                     # extension override for the virtual path

        if(ENGINE_KTX AND ext IN_LIST KTX_SOURCE_EXTS)
            # Mirror the layout of ASSET_ROOT inside COMPRESSED_ROOT, swapping the extension
            file(RELATIVE_PATH rel_asset "${ASSET_ROOT}" "${f}")
            get_filename_component(rel_dir  "${rel_asset}" DIRECTORY)
            get_filename_component(rel_name "${rel_asset}" NAME_WLE)
            if(rel_dir STREQUAL "")
                set(src "${COMPRESSED_ROOT}/${rel_name}.${KTX_EXT}")
            else()
                set(src "${COMPRESSED_ROOT}/${rel_dir}/${rel_name}.${KTX_EXT}")
            endif()
            set(vext "${KTX_EXT}")

            if(NOT EXISTS "${src}")
                message(WARNING "[preload] missing compressed asset: ${src}")
            endif()
        endif()

        cmake_path(IS_PREFIX GLTF_ROOT "${f}" NORMALIZE under_gltf)
        if(under_gltf)
            file(RELATIVE_PATH rel "${GLTF_ROOT}" "${f}")
            set(dest "/gltf/${rel}")
        else()
            file(RELATIVE_PATH rel "${root}" "${f}")
            set(dest "/${RES_DEST_${ext}}/${rel}")
        endif()

        # Swap the extension in the virtual path too (foo.png -> foo.ktx2)
        if(vext)
            get_filename_component(d_dir  "${dest}" DIRECTORY)
            get_filename_component(d_name "${dest}" NAME_WLE)
            set(dest "${d_dir}/${d_name}.${vext}")
        endif()

        register_preload("${src}" "${dest}")
    endforeach()
endforeach()

# 2) Generated shaders: they don't exist at configure time, so use the known output list
foreach(f IN LISTS WGSL_OUT_FILES)
    get_filename_component(fname "${f}" NAME)
    get_filename_component(dname "${f}" NAME_WE)
    register_preload("${f}" "/shaders/${dname}/${fname}")
endforeach()

message(STATUS "[preload] Total files: ${PRELOAD_FILE_COUNT}")

# Single space-separated string for LINK_FLAGS
string(JOIN " " PRELOAD_FLAGS_STRING ${PRELOAD_ARGS_LIST})

# Export to parent scope (consumed by nkproject)
set(${PROJECT_NAME}_PRELOAD_FLAGS   ${PRELOAD_FLAGS_STRING} PARENT_SCOPE)
set(${PROJECT_NAME}_WGSL_OUT_FILES  ${WGSL_OUT_FILES}       PARENT_SCOPE)
