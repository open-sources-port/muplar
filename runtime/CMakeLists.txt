add_subdirectory(elf)
add_subdirectory(memory)
add_subdirectory(jni)

add_library(muplar_runtime
    runtime_builder.cpp
)

target_include_directories(muplar_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(muplar_runtime PUBLIC
    muplar_runtime_elf
    muplar_runtime_memory
)
