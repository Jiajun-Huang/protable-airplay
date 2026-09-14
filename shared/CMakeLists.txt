# A board project can supply its configured mbedcrypto target before add_subdirectory.
if(NOT TARGET mbedcrypto)
    # mbedTLS 2.28 declares an older minimum; CMake 4 requires this policy floor.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    if(AIRPLAY_MBEDTLS_SOURCE_DIR)
        add_subdirectory("${AIRPLAY_MBEDTLS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/mbedtls")
    elseif(AIRPLAY_PLATFORM STREQUAL "embedded")
        message(FATAL_ERROR "Embedded: supply a board-configured mbedcrypto target or AIRPLAY_MBEDTLS_SOURCE_DIR")
    else()
        include(FetchContent)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        FetchContent_Declare(mbedtls
            URL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz)
        FetchContent_MakeAvailable(mbedtls)
    endif()
endif()

# The shared crypto layer uses mbedTLS's fixed-buffer allocator. The mbedcrypto
# target must be built with the same memory and threading configuration.
get_target_property(AIRPLAY_MBEDCRYPTO_ALIAS mbedcrypto ALIASED_TARGET)
if(AIRPLAY_MBEDCRYPTO_ALIAS)
    set(AIRPLAY_MBEDCRYPTO_TARGET ${AIRPLAY_MBEDCRYPTO_ALIAS})
else()
    set(AIRPLAY_MBEDCRYPTO_TARGET mbedcrypto)
endif()
get_target_property(AIRPLAY_MBEDCRYPTO_IMPORTED ${AIRPLAY_MBEDCRYPTO_TARGET} IMPORTED)
if(AIRPLAY_MBEDCRYPTO_IMPORTED)
    message(FATAL_ERROR
        "The mbedcrypto target must be built from source with the AirPlay static allocator settings")
endif()
target_include_directories(${AIRPLAY_MBEDCRYPTO_TARGET} PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/shared"
    "${PROJECT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/crypto/mbedtls")
target_compile_definitions(${AIRPLAY_MBEDCRYPTO_TARGET} PUBLIC
    MBEDTLS_PLATFORM_MEMORY=1
    MBEDTLS_MEMORY_BUFFER_ALLOC_C=1
    MBEDTLS_THREADING_C=1
    MBEDTLS_THREADING_ALT=1)

if(AIRPLAY_ENABLE_FDK_AAC AND NOT AIRPLAY_PLATFORM STREQUAL "embedded")
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(FDK_AAC_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(FDK_AAC_INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(fdk_aac
        URL https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v2.0.3.tar.gz)
    FetchContent_MakeAvailable(fdk_aac)
endif()

file(GLOB_RECURSE shared_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_library(airplay_core STATIC ${shared_sources})
set_target_properties(airplay_core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(airplay_core PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/platform")
target_link_libraries(airplay_core PUBLIC mbedcrypto)
if(AIRPLAY_ENABLE_FDK_AAC AND NOT AIRPLAY_PLATFORM STREQUAL "embedded")
    target_link_libraries(airplay_core PRIVATE FDK-AAC::fdk-aac)
    target_compile_definitions(airplay_core PRIVATE AIRPLAY_ENABLE_FDK_AAC=1)
endif()
if(NOT WIN32)
    target_link_libraries(airplay_core PUBLIC m)
endif()
