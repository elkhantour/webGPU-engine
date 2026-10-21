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

# -fsanitize=address creates too many locals and throws a wasm error
# see: https://github.com/emscripten-core/emscripten/issues/19346#issuecomment-1673579716
set(ASAN
  -fsanitize=undefined
  -ferror-limit=0
  -g
)

set(FLAGS
  -pthread
)
