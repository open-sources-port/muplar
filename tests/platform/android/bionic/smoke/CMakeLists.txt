add_executable(bionic-smoke
    smoke_main.c
    smoke_core.c
    smoke_syscall.c
    smoke_report.c
)
target_link_libraries(bionic-smoke
    PRIVATE bionic
)

target_include_directories(bionic-smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/platform/android/bionic/include
)

target_link_libraries(bionic-smoke PRIVATE
    pthread
)

