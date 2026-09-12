add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE airplay_core airplay_platform)
add_test(NAME core_protocol COMMAND test_core)

add_executable(test_net test_net.c)
target_link_libraries(test_net PRIVATE airplay_core airplay_platform)
add_test(NAME network_contract COMMAND test_net)
set_tests_properties(network_contract PROPERTIES TIMEOUT 15)

add_executable(test_server test_server.c fake_platform.c)
target_link_libraries(test_server PRIVATE airplay_core)
add_test(NAME service_lifecycle COMMAND test_server)
set_tests_properties(service_lifecycle PROPERTIES TIMEOUT 15)

add_executable(test_rtsp test_rtsp.c)
target_link_libraries(test_rtsp PRIVATE airplay_core)
add_test(NAME rtsp_stream COMMAND test_rtsp)
set_tests_properties(rtsp_stream PROPERTIES TIMEOUT 15)

add_executable(test_mdns test_mdns.c)
target_link_libraries(test_mdns PRIVATE airplay_core)
add_test(NAME mdns_wire COMMAND test_mdns)

add_executable(test_alac test_alac.c)
target_link_libraries(test_alac PRIVATE airplay_core)
add_test(NAME alac_samples COMMAND test_alac)

add_executable(test_timing test_timing.c)
target_link_libraries(test_timing PRIVATE airplay_core)
add_test(NAME scheduled_audio COMMAND test_timing)

add_executable(test_airplay2 test_airplay2.c)
target_link_libraries(test_airplay2 PRIVATE airplay_core)
add_test(NAME airplay2_protocol COMMAND test_airplay2)
set_tests_properties(airplay2_protocol PROPERTIES TIMEOUT 15)
