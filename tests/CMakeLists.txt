# cmake_minimum_required(VERSION 3.15)
# project(airplay_speaker C)

# set(CMAKE_C_STANDARD 11)

# # shared core library
# file(GLOB RAOP_SHARED_SOURCES CONFIGURE_DEPENDS
#     "shared/*.c"
# )


# # include directories

# add_library(raop_shared ${RAOP_SHARED_SOURCES})


# target_include_directories(raop_shared PUBLIC include ${CMAKE_CURRENT_SOURCE_DIR}/shared)

# # # unit tests (always build)
# # add_executable(test_rtsp tests/test_rtsp.c)
# # target_link_libraries(test_rtsp raop_shared)

# # add_executable(test_sdp tests/test_sdp.c)
# # target_link_libraries(test_sdp raop_shared)

# # enable_testing()
# # add_test(NAME rtsp_parser COMMAND test_rtsp)
# # add_test(NAME sdp_parser COMMAND test_sdp)

# # platform-specific targets
# if(WIN32)
#     add_subdirectory(platform/windows)
# endif()

# # embedded target stub (not buildable without lwIP)

