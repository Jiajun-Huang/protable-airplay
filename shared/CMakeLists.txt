include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")

# mbedTLS 2.28 declares an older minimum; CMake 4 requires this policy floor.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

CPMAddPackage(
    NAME mbedtls
    VERSION 2.28.8
    URL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz
    OPTIONS
        "ENABLE_PROGRAMS OFF"
        "ENABLE_TESTING OFF"
        "MBEDTLS_FATAL_WARNINGS OFF"
)
CPMAddPackage(
    NAME fdk_aac
    VERSION 2.0.3
    URL https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v2.0.3.tar.gz
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "BUILD_PROGRAMS OFF"
        "FDK_AAC_INSTALL_CMAKE_CONFIG_MODULE OFF"
        "FDK_AAC_INSTALL_PKGCONFIG_MODULE OFF"
)

# The shared crypto layer uses mbedTLS's fixed-buffer allocator. The mbedcrypto
# target must be built with the same memory and threading configuration.
target_include_directories(mbedcrypto PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/shared"
    "${PROJECT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/crypto/mbedtls")
target_compile_definitions(mbedcrypto PUBLIC
    MBEDTLS_PLATFORM_MEMORY=1
    MBEDTLS_MEMORY_BUFFER_ALLOC_C=1
    MBEDTLS_THREADING_C=1
    MBEDTLS_THREADING_ALT=1)

file(GLOB_RECURSE shared_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_library(airplay_core STATIC ${shared_sources})
set_target_properties(airplay_core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(airplay_core PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/platform")
target_link_libraries(airplay_core PUBLIC mbedcrypto PRIVATE FDK-AAC::fdk-aac)
if(NOT WIN32)
    target_link_libraries(airplay_core PUBLIC m)
endif()
