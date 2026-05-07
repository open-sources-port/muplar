add_library(muplar_runtime_memory
    virtual_memory.cpp
)

target_include_directories(muplar_runtime_memory PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/android/bionic/include
)
