# ----------------------------
# Shader generation -> WGSL_OUT_FILES
# ----------------------------
set(WGSL_OUT_FILES "")
set(WGSL_OUT_DIR "${CMAKE_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY ${WGSL_OUT_DIR})

# ---- 1) *.wgsl.in -> preprocessed with cpp ----
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

# ---- 2) *.slang -> WGSL via slangc ----
find_program(SLANGC slangc REQUIRED)

set(SLANG_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${ENGINE_SRC_DIR}/backend/shaders")

file(GLOB_RECURSE SLANG_IN_FILES CONFIGURE_DEPENDS
    "${ENGINE_SRC_DIR}/backend/shaders/render/*.slang"
    "${ENGINE_SRC_DIR}/backend/shaders/compute/*.slang"
)
file(GLOB_RECURSE SLANG_MODULE_FILES CONFIGURE_DEPENDS
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
