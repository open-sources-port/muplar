add_executable(mup
    src/main.cpp
)

target_link_libraries(mup
    PRIVATE
        muplar_runtime
        muplar_core
        muplar_compositor
)

target_include_directories(mup
    PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime
        ${CMAKE_SOURCE_DIR}/compositor/include
)
