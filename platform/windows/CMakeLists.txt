file(GLOB WIN_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_executable(raop_win ${WIN_SOURCES})
    
target_include_directories(raop_win PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(raop_win
    PRIVATE
        raop_shared
        ws2_32
        iphlpapi
        winmm
        ole32
        uuid
)

