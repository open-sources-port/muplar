add_library(muplar_core
    src/log.cpp
    src/config.cpp
)

target_include_directories(muplar_core
    PUBLIC include
)

target_compile_features(muplar_core PUBLIC cxx_std_20)
