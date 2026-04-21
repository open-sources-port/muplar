add_library(muplar_runtime
    process/ProcessManager.cpp

    process/impl/HostProcess.cpp
    process/impl/WineProcess.cpp
    process/impl/AndroidProcess.cpp
)

target_include_directories(muplar_runtime
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(muplar_runtime
    PUBLIC muplar_core
)
