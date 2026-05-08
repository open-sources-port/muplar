add_library(muplar_runtime_elf
    elf_loader.cpp
    elf_mapper.cpp
    execution_context.cpp

    # elfuse core
    ../../third_party/elfuse/src/core/elf.c

    # elfuse util
    ../../third_party/elfuse/src/debug/log.c
)

target_include_directories(muplar_runtime_elf PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/android/bionic/include
    ${CMAKE_SOURCE_DIR}/runtime/memory
    ${CMAKE_SOURCE_DIR}/runtime/elf
    ${CMAKE_SOURCE_DIR}/third_party/elfuse/src
)

target_link_libraries(muplar_runtime_elf
    PUBLIC
    muplar_runtime_memory
)

target_compile_features(muplar_runtime_elf PUBLIC cxx_std_17)
