# ----------------------------
# Engine Library Definition
# ----------------------------
add_library(${PROJECT_NAME} STATIC
    ${ENGINE_C_SRC}
    ${ENGINE_CXX_SRC}
    ${ENGINE_C_EXAMPLES}
    ${WGSL_OUT_FILES} # Ensures shaders are generated before linking
)

if(ENGINE_KTX)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENGINE_KTX)
endif()

target_compile_options(${PROJECT_NAME} PUBLIC  ${MACROS})
target_compile_options(${PROJECT_NAME} PRIVATE ${FLAGS} ${ASAN})

target_link_libraries(${PROJECT_NAME} PUBLIC  ${PROJECT_NAME}_vendors)
target_link_libraries(${PROJECT_NAME} PUBLIC  ${PROJECT_NAME}_vendor_includes)

# Required for Emscripten's WebGPU support if targeting WASM
target_link_libraries(${PROJECT_NAME} PUBLIC  webgpu)

target_link_libraries(${PROJECT_NAME} PRIVATE ${ASAN})
