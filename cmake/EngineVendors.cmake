# ----------------------------
# Vendor Library (imgui, cglm, stb, cgltf)
# ----------------------------

add_library(ktx STATIC IMPORTED)
set_target_properties(ktx PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ktx/lib/libktx.a
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ktx/include")


file(GLOB_RECURSE IMGUI "vendor/imgui/*.cpp" "vendor/imgui/*.h")
file(GLOB_RECURSE CGLM  "vendor/cglm/*.h")
file(GLOB_RECURSE STB   "vendor/stb/*.h")
file(GLOB_RECURSE CGLTF "vendor/cgltf/*.h")

add_library(${PROJECT_NAME}_vendors STATIC ${IMGUI} ${CGLM} ${STB} ${CGLTF})
target_link_libraries(${PROJECT_NAME}_vendors PUBLIC ktx)
