file(GLOB_RECURSE platform_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(REMOVE_ITEM platform_sources "${CMAKE_CURRENT_SOURCE_DIR}/main.c")

add_library(airplay_platform OBJECT ${platform_sources})
target_link_libraries(airplay_platform PUBLIC ws2_32 winmm)

add_executable(airplay_player main.c)
target_link_libraries(airplay_player PRIVATE airplay_core airplay_platform iphlpapi)
