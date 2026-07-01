add_library(muplar_services_client STATIC
    muplard_client.cpp
)

target_compile_features(muplar_services_client PUBLIC cxx_std_17)
target_compile_options(muplar_services_client PRIVATE -Wall -Wextra)
target_include_directories(muplar_services_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(muplard
    muplard.cpp
)

target_compile_features(muplard PRIVATE cxx_std_17)
target_compile_options(muplard PRIVATE -Wall -Wextra)

set_target_properties(muplard PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
