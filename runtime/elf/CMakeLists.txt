add_library(muplar_runtime_elf
    elf_loader.cpp
)

target_include_directories(muplar_runtime_elf PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/android/bionic/include
)
