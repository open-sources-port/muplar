add_library(muplar_runtime_elf
    elf_loader.cpp
    elf_validator.cpp
    elf_program_headers.cpp
    elf_sections.cpp
)

target_include_directories(muplar_runtime_elf PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/android/bionic/include
    ${CMAKE_SOURCE_DIR}/runtime/memory
)

target_link_libraries(muplar_runtime_elf
    PUBLIC
    muplar_runtime_memory
)
