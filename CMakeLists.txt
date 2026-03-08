cmake_minimum_required(VERSION 3.15)
project(airplay_speaker C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")

# Fetch mbedtls (using v2.28 LTS - no submodules needed)
include(FetchContent)
FetchContent_Declare(
    mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Configure mbedtls options
set(ENABLE_PROGRAMS OFF CACHE BOOL "")
set(ENABLE_TESTING OFF CACHE BOOL "")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "")

FetchContent_MakeAvailable(mbedtls)

# shared core library
file(GLOB RAOP_SHARED_SOURCES CONFIGURE_DEPENDS
    "shared/*.c"    # Top-level shared sources (e.g., mdns.c)
    "shared/**/*.c"  # Recursive - matches all .c files in shared and subdirs
)

add_library(raop_shared
    ${RAOP_SHARED_SOURCES}
)

target_include_directories(raop_shared PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/shared 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

target_link_libraries(raop_shared PUBLIC
    mbedtls
    mbedcrypto
    mbedx509
)




# mDNS multicast test (requires platform network library)
add_executable(test_mdns tests/test_mdns.c 
    platform/windows/udp.c)
target_include_directories(test_mdns PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)
target_link_libraries(test_mdns 
    raop_shared
    ws2_32
)

enable_testing()
add_test(NAME mdns_multicast COMMAND test_mdns)

# platform-specific targets
if(WIN32)
    add_subdirectory(platform/windows)
endif()

# embedded target stub (not buildable without lwIP)

