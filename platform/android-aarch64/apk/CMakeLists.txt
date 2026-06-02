add_library(muplar_runtime_apk STATIC
    apk_envelope.cpp
)

find_package(ZLIB REQUIRED)

target_include_directories(muplar_runtime_apk
    PUBLIC
        ${CMAKE_SOURCE_DIR}/platform/android-aarch64/apk
)

target_link_libraries(muplar_runtime_apk
    PRIVATE
        ZLIB::ZLIB
)

target_compile_options(muplar_runtime_apk PRIVATE
    -Wall
    -Wextra
)
