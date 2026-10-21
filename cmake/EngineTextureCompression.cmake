#[=============================================================================[
EngineTextureCompression.cmake

Drop-in module for the ENGINE's top CMakeLists.txt:

    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    include(EngineTextureCompression)

When the CMake option ENGINE_KTX is ON (default) the module, at the end of the
including directory's CMakeLists.txt:

  1. CONVERTS EVERY .png/.jpg/.jpeg found under the resources root
         <engine dir>/src/resources/assets          (hardcoded)
     with `ktx create` into the build tree, keeping the path below the root:
         src/resources/assets/texture/skybox/a.jpg
      -> <build>/compressed_assets/texture/skybox/a.ktx2
         src/resources/assets/gltf/path/to/tex.png
      -> <build>/compressed_assets/gltf/path/to/tex.ktx2
     New images are picked up automatically (CONFIGURE_DEPENDS glob).

  2. DEFINES the preprocessor symbol ENGINE_KTX (PUBLIC) on every engine target
     (library/executable of this directory and its subdirectories, vendors
     excluded), so code and the preload/remap module can react to it.

  3. PATCHES THE SOURCES: for each C/C++ file a copy is generated in
         <build>/compressed_sources/<path relative to the engine dir>
     where every string literal ending in .png/.jpg/.jpeg gets its extension
     replaced by .ktx2, e.g. PATH_TEXTURE("skybox/a.jpg") ->
     PATH_TEXTURE("skybox/a.ktx2"). The copy is compiled instead of the
     original; originals are never touched.

Remapping virtual paths to <build>/compressed_assets is NOT done here (that is
the job of the preload module).

Settings (set BEFORE include(), all optional):

  ENGINE_KTX                      cache option, ON BY DEFAULT. -DENGINE_KTX=OFF
                                  disables everything (no define, no patching,
                                  no conversion).
  ENGINE_TEXTURES_EXTENSION       extension of the compressed files and of the
                                  patched literals. Default: ktx2
  ENGINE_TEXTURES_TARGETS         explicit target list (default: all targets of
                                  this directory and its subdirectories)
  ENGINE_TEXTURES_EXCLUDE_TARGETS targets to skip
  ENGINE_TEXTURES_EXCLUDE_TARGET_REGEX
                                  targets whose NAME matches are skipped.
                                  Default "[Vv]endor|[Tt]hird_?[Pp]arty" ("" = off)
  ENGINE_TEXTURES_EXCLUDE_PATH_REGEX
                                  C/C++ files whose path relative to the engine
                                  dir matches are not patched. Default: a
                                  vendor/, vendors/, third_party/ or external/
                                  directory ("" = off). Not applied to images.
  ENGINE_TEXTURES_OUTPUT_DIR      default: ${CMAKE_BINARY_DIR}/compressed_assets
  ENGINE_TEXTURES_KTX_ARGS        arguments for "ktx create", placed before
                                  <input> <output>. Default:
                                    --format R8G8B8A8_SRGB --encode uastc
                                    --generate-mipmap --zstd 19
  ENGINE_TEXTURES_VERBOSE         ON to log every conversion / rewrite
  ENGINE_KTX_EXECUTABLE           cache path to ktx (auto-detected)

Requires CMake >= 3.19 and the ktx tool from KTX-Software >= 4.3
(https://github.com/KhronosGroup/KTX-Software). If ktx is missing, a warning is
printed, ENGINE_KTX is NOT defined and the engine builds unmodified.

Notes / limitations
  * EVERY string literal ending in .png/.jpg/.jpeg gets its extension changed
    (except ones containing * % or {). Paths assembled at runtime are not seen.
  * a.png and a.jpg in the same folder would both become a.ktx2 (a warning is
    printed and only the first one is converted).
  * Patched copies start with a #line directive, so diagnostics, debuggers and
    __FILE__ still refer to the ORIGINAL file, and the original directory is
    added as include dir so #include "local.h" keeps working.
  * Editing a C/C++ source re-runs configure (needed to re-patch it).
    Editing an image only re-runs ktx.
  * Per-source COMPILE_OPTIONS / COMPILE_DEFINITIONS set on an original file
    are not carried over to its copy.
#]=============================================================================]

include_guard(DIRECTORY)

option(ENGINE_KTX
    "Convert assets to KTX2, switch image literals to .ktx2 and define ENGINE_KTX" ON)
if(NOT ENGINE_KTX)
    return()
endif()

if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR
        "EngineTextureCompression needs CMake >= 3.19 (cmake_language(DEFER)); "
        "found ${CMAKE_VERSION}")
endif()

find_program(ENGINE_KTX_EXECUTABLE ktx
    DOC "KTX-Software ktx tool (used as: ktx create) to compress textures")

# ---------------------------------------------------------------------------
# Patches the C/C++ sources of ONE target (internal)
# ---------------------------------------------------------------------------
function(_engine_tex_process_target)
    cmake_parse_arguments(ARG
        "VERBOSE"
        "TARGET;SRC_BASE;EXTENSION;EXCLUDE_PATH_REGEX"
        ""
        ${ARGN})

    get_target_property(_target_src_dir ${ARG_TARGET} SOURCE_DIR)
    set(_patched_dir "${CMAKE_BINARY_DIR}/compressed_sources")

    # Lets the engine / preload module know compressed assets are in use
    target_compile_definitions(${ARG_TARGET} PUBLIC ENGINE_KTX)

    get_target_property(_sources ${ARG_TARGET} SOURCES)
    if(NOT _sources)
        return()
    endif()

    set(_new_sources "")
    set(_n_scanned 0)
    set(_n_literals 0)

    foreach(_src IN LISTS _sources)
        # Leave generator expressions untouched
        if(_src MATCHES "\\$<")
            list(APPEND _new_sources "${_src}")
            continue()
        endif()

        get_filename_component(_abs "${_src}" ABSOLUTE BASE_DIR "${_target_src_dir}")
        string(TOLOWER "${_abs}" _abs_lower)

        # Only C / C++ translation units, never our own generated files
        if(NOT _abs_lower MATCHES "\\.(c|cc|cpp|cxx|c\\+\\+)$"
           OR _abs MATCHES "^${_patched_dir}/"
           OR NOT EXISTS "${_abs}")
            list(APPEND _new_sources "${_src}")
            continue()
        endif()

        # Location relative to the engine dir (also used for the copy's location)
        file(RELATIVE_PATH _src_rel "${ARG_SRC_BASE}" "${_abs}")
        if(_src_rel MATCHES "^\\.\\.")
            string(MD5 _h "${_abs}")
            string(SUBSTRING "${_h}" 0 8 _h)
            get_filename_component(_n "${_abs}" NAME)
            set(_src_rel "_external/${_h}/${_n}")
        endif()

        # Skip excluded locations (e.g. vendor/)
        if(ARG_EXCLUDE_PATH_REGEX AND _src_rel MATCHES "${ARG_EXCLUDE_PATH_REGEX}")
            list(APPEND _new_sources "${_src}")
            continue()
        endif()

        math(EXPR _n_scanned "${_n_scanned} + 1")
        file(READ "${_abs}" _content)
        string(REGEX MATCHALL
            "\"[^\"\n;*%{\\\\]+\\.([pP][nN][gG]|[jJ][pP][eE]?[gG])\""
            _matches "${_content}")
        list(REMOVE_DUPLICATES _matches)

        if(NOT _matches)
            list(APPEND _new_sources "${_src}")
            continue()
        endif()

        set(_patched "${_content}")
        foreach(_m IN LISTS _matches)
            math(EXPR _n_literals "${_n_literals} + 1")
            string(LENGTH "${_m}" _len)
            math(EXPR _inner_len "${_len} - 2")
            string(SUBSTRING "${_m}" 1 ${_inner_len} _ref)
            string(REGEX REPLACE "\\.[^./]*$" ".${ARG_EXTENSION}" _new_ref "${_ref}")
            string(REPLACE "${_m}" "\"${_new_ref}\"" _patched "${_patched}")
            if(ARG_VERBOSE)
                message(STATUS "[textures] ${_src_rel}: ${_ref} -> ${_new_ref}")
            endif()
        endforeach()

        # --- write the patched copy (only if content changed) --------------
        set(_dest "${_patched_dir}/${_src_rel}")
        get_filename_component(_src_dir "${_abs}" DIRECTORY)

        set(_final "#line 1 \"${_abs}\"\n${_patched}")
        unset(_old)
        if(EXISTS "${_dest}")
            file(READ "${_dest}" _old)
        endif()
        if(NOT "${_old}" STREQUAL "${_final}")
            file(WRITE "${_dest}" "${_final}")
        endif()

        # Re-run configure when the original changes
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_abs}")

        # Keep #include "local.h" working from the new location
        set_source_files_properties("${_dest}"
            TARGET_DIRECTORY ${ARG_TARGET}
            PROPERTIES INCLUDE_DIRECTORIES "${_src_dir}")

        list(APPEND _new_sources "${_dest}")
    endforeach()

    set_property(TARGET ${ARG_TARGET} PROPERTY SOURCES "${_new_sources}")

    message(STATUS "EngineTextureCompression: ${ARG_TARGET}: scanned ${_n_scanned} "
        "C/C++ file(s), ${_n_literals} image literal(s) switched to .${ARG_EXTENSION}")
endfunction()

# ---------------------------------------------------------------------------
# Converts every image under ROOT, mirroring the layout (internal)
# ---------------------------------------------------------------------------
function(_engine_tex_convert_all)
    cmake_parse_arguments(ARG "VERBOSE" "ROOT;OUTPUT_DIR;KTX;EXTENSION;TARGET" "KTX_ARGS" ${ARGN})

    file(GLOB_RECURSE _imgs LIST_DIRECTORIES false CONFIGURE_DEPENDS
        "${ARG_ROOT}/*.png"  "${ARG_ROOT}/*.PNG"
        "${ARG_ROOT}/*.jpg"  "${ARG_ROOT}/*.JPG"
        "${ARG_ROOT}/*.jpeg" "${ARG_ROOT}/*.JPEG")
    list(REMOVE_DUPLICATES _imgs)

    set(_outs "")
    set(_ins  "")
    foreach(_img IN LISTS _imgs)
        file(RELATIVE_PATH _rel "${ARG_ROOT}" "${_img}")
        string(REGEX REPLACE "\\.[^./]*$" ".${ARG_EXTENSION}" _ktx_rel "${_rel}")
        set(_out "${ARG_OUTPUT_DIR}/${_ktx_rel}")

        list(FIND _outs "${_out}" _idx)
        if(NOT _idx EQUAL -1)
            list(GET _ins ${_idx} _prev)
            message(WARNING "EngineTextureCompression: ${_prev} and ${_img} both "
                "map to ${_out}; only the first is converted")
            continue()
        endif()
        list(APPEND _outs "${_out}")
        list(APPEND _ins  "${_img}")

        get_filename_component(_out_dir "${_out}" DIRECTORY)
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
            COMMAND "${ARG_KTX}" create ${ARG_KTX_ARGS} "${_img}" "${_out}"
            DEPENDS "${_img}"
            COMMENT "Compressing texture ${_rel}"
            VERBATIM)
        if(ARG_VERBOSE)
            message(STATUS "[textures] ${_rel} -> ${_out}")
        endif()
    endforeach()

    list(LENGTH _outs _n)
    message(STATUS "EngineTextureCompression: ${_n} texture(s) found under ${ARG_ROOT}")
    if(_n GREATER 0)
        add_custom_target(${ARG_TARGET} ALL DEPENDS ${_outs})
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Collects the targets of a directory tree (internal)
# ---------------------------------------------------------------------------
function(_engine_tex_collect_targets dir out_var)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subdirs)
        _engine_tex_collect_targets("${_s}" _sub_targets)
        list(APPEND _targets ${_sub_targets})
    endforeach()
    set(${out_var} "${_targets}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Entry point, runs deferred at the end of the including directory (internal)
# ---------------------------------------------------------------------------
function(_engine_tex_run)
    cmake_parse_arguments(ARG
        "VERBOSE"
        "DIR;ROOT;OUTPUT_DIR;KTX;EXTENSION;EXCLUDE_TARGET_REGEX;EXCLUDE_PATH_REGEX"
        "KTX_ARGS;TARGETS;EXCLUDE_TARGETS"
        ${ARGN})

    if(NOT ARG_KTX)
        message(WARNING
            "EngineTextureCompression: ktx not found, textures will NOT be "
            "compressed and ENGINE_KTX is NOT defined. Install KTX-Software "
            "(>= 4.3) or pass -DENGINE_KTX_EXECUTABLE=/path/to/ktx "
            "(or -DENGINE_KTX=OFF to silence this).")
        return()
    endif()
    if(NOT IS_DIRECTORY "${ARG_ROOT}")
        message(WARNING "EngineTextureCompression: resources root not found: "
            "${ARG_ROOT} (nothing done, ENGINE_KTX NOT defined)")
        return()
    endif()

    set(_verbose "")
    if(ARG_VERBOSE)
        set(_verbose VERBOSE)
    endif()

    # 1) convert all images
    _engine_tex_convert_all(
        ROOT "${ARG_ROOT}" OUTPUT_DIR "${ARG_OUTPUT_DIR}" KTX "${ARG_KTX}"
        EXTENSION "${ARG_EXTENSION}" TARGET engine_compressed_assets
        KTX_ARGS ${ARG_KTX_ARGS} ${_verbose})

    # 2) define + patch sources of the engine targets
    if(ARG_TARGETS)
        set(_targets ${ARG_TARGETS})
    else()
        _engine_tex_collect_targets("${ARG_DIR}" _targets)
    endif()
    list(REMOVE_DUPLICATES _targets)
    if(ARG_EXCLUDE_TARGETS)
        list(REMOVE_ITEM _targets ${ARG_EXCLUDE_TARGETS})
    endif()
    if(NOT _targets)
        message(WARNING "EngineTextureCompression: no targets found under ${ARG_DIR}")
        return()
    endif()
    message(STATUS "EngineTextureCompression: targets: ${_targets}")

    foreach(_t IN LISTS _targets)
        if(NOT TARGET ${_t})
            continue()
        endif()
        if(ARG_EXCLUDE_TARGET_REGEX AND _t MATCHES "${ARG_EXCLUDE_TARGET_REGEX}")
            if(ARG_VERBOSE)
                message(STATUS "EngineTextureCompression: skipping excluded target ${_t}")
            endif()
            continue()
        endif()
        get_target_property(_type ${_t} TYPE)
        get_target_property(_imported ${_t} IMPORTED)
        if(_imported OR NOT _type MATCHES
           "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            continue()
        endif()

        _engine_tex_process_target(
            TARGET ${_t}
            SRC_BASE "${ARG_DIR}"
            EXTENSION "${ARG_EXTENSION}"
            EXCLUDE_PATH_REGEX "${ARG_EXCLUDE_PATH_REGEX}"
            ${_verbose})

        # make sure the compressed assets exist whenever the engine is built
        if(TARGET engine_compressed_assets)
            add_dependencies(${_t} engine_compressed_assets)
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Capture settings NOW (include time) and schedule the work
# ---------------------------------------------------------------------------
# Hardcoded resources root (the directory the virtual-drive paths are based on)
set(ENGINE_TEXTURES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/assets")

if(NOT DEFINED ENGINE_TEXTURES_OUTPUT_DIR)
    set(ENGINE_TEXTURES_OUTPUT_DIR "${CMAKE_BINARY_DIR}/compressed_assets")
endif()
if(NOT DEFINED ENGINE_TEXTURES_EXTENSION)
    set(ENGINE_TEXTURES_EXTENSION "ktx2")
endif()
if(NOT DEFINED ENGINE_TEXTURES_KTX_ARGS)
    set(ENGINE_TEXTURES_KTX_ARGS
        --format R8G8B8A8_SRGB --encode uastc --generate-mipmap --zstd 19)
endif()

# Vendors are excluded by default; set the variable to "" to disable.
if(NOT DEFINED ENGINE_TEXTURES_EXCLUDE_TARGET_REGEX)
    set(ENGINE_TEXTURES_EXCLUDE_TARGET_REGEX "[Vv]endor|[Tt]hird_?[Pp]arty")
endif()
if(NOT DEFINED ENGINE_TEXTURES_EXCLUDE_PATH_REGEX)
    set(ENGINE_TEXTURES_EXCLUDE_PATH_REGEX "(^|/)([Vv]endors?|[Tt]hird_?[Pp]arty|[Ee]xternal)/")
endif()

set(_engine_tex_verbose "")
if(ENGINE_TEXTURES_VERBOSE)
    set(_engine_tex_verbose VERBOSE)
    message(STATUS "EngineTextureCompression: scheduled for ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

cmake_language(DEFER CALL _engine_tex_run
    DIR             "${CMAKE_CURRENT_SOURCE_DIR}"
    ROOT            "${ENGINE_TEXTURES_ROOT}"
    OUTPUT_DIR      "${ENGINE_TEXTURES_OUTPUT_DIR}"
    KTX             "${ENGINE_KTX_EXECUTABLE}"
    EXTENSION       "${ENGINE_TEXTURES_EXTENSION}"
    EXCLUDE_TARGET_REGEX "${ENGINE_TEXTURES_EXCLUDE_TARGET_REGEX}"
    EXCLUDE_PATH_REGEX   "${ENGINE_TEXTURES_EXCLUDE_PATH_REGEX}"
    KTX_ARGS        ${ENGINE_TEXTURES_KTX_ARGS}
    TARGETS         ${ENGINE_TEXTURES_TARGETS}
    EXCLUDE_TARGETS ${ENGINE_TEXTURES_EXCLUDE_TARGETS}
    ${_engine_tex_verbose})
