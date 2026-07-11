add_executable(dolphin-web
  HostWeb.cpp
  HostWeb.h
  MainWeb.cpp
)

target_link_libraries(dolphin-web
PRIVATE
  core
  uicommon
  xxhash::xxhash
)

if(EMSCRIPTEN)
  set_target_properties(dolphin-web PROPERTIES SUFFIX ".js")
  target_link_options(dolphin-web PRIVATE
    -sPROXY_TO_PTHREAD
    -sPTHREAD_POOL_SIZE=16
    -sINITIAL_MEMORY=805306368      # 768 MiB
    -sALLOW_MEMORY_GROWTH
    -sMAXIMUM_MEMORY=2147483648     # 2 GiB
    -sSTACK_SIZE=2097152
    -sDEFAULT_PTHREAD_STACK_SIZE=2097152
    -sENVIRONMENT=web,worker,node
    -sEXIT_RUNTIME
    # Node-only host-FS mount for the smoke/determinism runner (inert in browser)
    -lnodefs.js
    "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/pre.js"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/Data/Sys@/Sys"
  )
else()
  # Native twin used by Tools/wasm-determinism.sh to diff MEM1 hashes.
  set_target_properties(dolphin-web PROPERTIES OUTPUT_NAME dolphin-headless-smoke)
endif()
