# MQTT Client Library - Tests CMakeLists.txt

# We use a minimal test framework included in the project
# Tests are built as executables that can be run individually or via ctest

# Include the test framework
add_library(test_framework INTERFACE)
target_include_directories(test_framework INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Common test utilities
set(TEST_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_framework.h
)

# Function to add a unit test
function(add_mqtt_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${TEST_COMMON_SOURCES})

    target_link_libraries(${TEST_NAME} PRIVATE
        mqtt_client_static
        test_framework
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # Set test properties
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 30
        LABELS "unit"
    )
endfunction()

# Unit Tests
add_mqtt_test(test_packet_id unit/test_packet_id.c)
add_mqtt_test(test_inflight unit/test_inflight.c)
add_mqtt_test(test_buffer unit/test_buffer.c)
add_mqtt_test(test_varint unit/test_varint.c)

# MQTT 5.0 Tests
if(MQTT_ENABLE_V5)
    add_mqtt_test(test_mqtt_v5_properties unit/test_mqtt_v5_properties.c)
endif()

# Platform Tests
add_mqtt_test(test_io_mux unit/test_io_mux.c)

# Integration tests (require network, run separately)
# add_mqtt_test(test_broker_connect integration/test_broker_connect.c)
