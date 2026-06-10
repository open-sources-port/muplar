add_library(muplar_runtime_supervisor
    supervisor_service.cpp
)

target_include_directories(muplar_runtime_supervisor PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platform/commons/prefix
)

target_link_libraries(muplar_runtime_supervisor
    PUBLIC
        muplar_runtime_prefix
)
