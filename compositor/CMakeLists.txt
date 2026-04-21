add_library(muplar_compositor
    src/compositor.cpp
)

target_include_directories(muplar_compositor
    PUBLIC include
)

target_link_libraries(muplar_compositor
    PUBLIC muplar_core
)
