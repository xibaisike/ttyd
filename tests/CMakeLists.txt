find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)

include_directories(${CMAKE_SOURCE_DIR}/src ${CMOCKA_INCLUDE_DIRS} ${INCLUDE_DIRS})
link_directories(${CMOCKA_LIBRARY_DIRS})


set(SOURCE_FILES_API ${CMAKE_SOURCE_DIR}/src/api.c ${CMAKE_SOURCE_DIR}/src/utils.c)
# test_api — links real src/api.c with mocked lws functions provided by
# tests/libwebsocket_sub.c. Test-specific stubs for session_* symbols
# remain inline in test_api.c.
add_executable(test_api test_api.c libwebsocket_sub.c ${SOURCE_FILES_API})
target_link_libraries(test_api ${CMOCKA_LIBRARIES} ${JSON-C_LIBRARIES})
target_compile_definitions(test_api PRIVATE _GNU_SOURCE)
add_test(NAME test_api COMMAND test_api)

# test_utils — tests utility functions (uppercase, endswith, signals, etc.)
add_executable(test_utils test_utils.c ${CMAKE_SOURCE_DIR}/src/utils.c)
target_link_libraries(test_utils ${CMOCKA_LIBRARIES})
target_compile_definitions(test_utils PRIVATE _GNU_SOURCE)
add_test(NAME test_utils COMMAND test_utils)

# test_session — tests ring buffer and session management
add_executable(test_session test_session.c libwebsocket_sub.c ${CMAKE_SOURCE_DIR}/src/session.c ${CMAKE_SOURCE_DIR}/src/utils.c)
target_link_libraries(test_session ${CMOCKA_LIBRARIES})
target_compile_definitions(test_session PRIVATE _GNU_SOURCE)
add_test(NAME test_session COMMAND test_session)

# test_osc133 — tests OSC 133 sequence parser
add_executable(test_osc133 test_osc133.c ${CMAKE_SOURCE_DIR}/src/osc133.c)
target_link_libraries(test_osc133 ${CMOCKA_LIBRARIES})
target_compile_definitions(test_osc133 PRIVATE _GNU_SOURCE)
add_test(NAME test_osc133 COMMAND test_osc133)
