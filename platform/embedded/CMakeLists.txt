file(GLOB_RECURSE platform_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(REMOVE_ITEM platform_sources "${CMAKE_CURRENT_SOURCE_DIR}/runtime.c")

add_library(airplay_platform OBJECT ${platform_sources})
target_compile_definitions(airplay_core PUBLIC AIRPLAY_TARGET_EMBEDDED=1)
target_compile_definitions(airplay_platform PUBLIC AIRPLAY_TARGET_EMBEDDED=1)

add_library(airplay_embedded STATIC runtime.c)
target_include_directories(airplay_embedded PRIVATE ${AIRPLAY_PLATFORM_INCLUDE_DIRS})
target_link_libraries(airplay_embedded PUBLIC airplay_core airplay_platform)
set_target_properties(airplay_embedded PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
