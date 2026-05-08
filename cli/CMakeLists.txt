add_executable(mup
    src/main.cpp
)

# target_link_libraries(mup
#     PRIVATE
#         muplar_runtime
#         muplar_core
#         muplar_compositor
# )

target_link_libraries(mup
    PRIVATE
    muplar_runtime
)

target_include_directories(mup PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/android/bionic/include
    ${CMAKE_SOURCE_DIR}/runtime
    ${CMAKE_SOURCE_DIR}/runtime/elf
    ${CMAKE_SOURCE_DIR}/runtime/memory
)
