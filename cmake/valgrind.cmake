# Valgrind configuration for MQTT Client Library
#
# This module provides targets for running tests under Valgrind
# to detect memory leaks, invalid memory access, and other errors.
#
# Usage:
#   cmake -DMQTT_ENABLE_VALGRIND=ON ..
#   make valgrind_test
#
# Or run individual tests:
#   make valgrind_test_buffer
#   make valgrind_test_pool

find_program(VALGRIND_EXECUTABLE valgrind)

if(NOT VALGRIND_EXECUTABLE)
    message(STATUS "Valgrind not found - memory testing targets disabled")
    return()
endif()

message(STATUS "Found Valgrind: ${VALGRIND_EXECUTABLE}")

# Valgrind options for memory checking
set(VALGRIND_OPTIONS
    --leak-check=full
    --show-leak-kinds=all
    --track-origins=yes
    --error-exitcode=1
    --suppressions=${CMAKE_CURRENT_LIST_DIR}/valgrind.supp
)

# Verbose options for detailed output
set(VALGRIND_VERBOSE_OPTIONS
    ${VALGRIND_OPTIONS}
    --verbose
    --log-file=valgrind-%p.log
)

# Create valgrind target for a test
function(add_valgrind_test TEST_NAME)
    if(TARGET ${TEST_NAME})
        add_custom_target(valgrind_${TEST_NAME}
            COMMAND ${VALGRIND_EXECUTABLE} ${VALGRIND_OPTIONS} $<TARGET_FILE:${TEST_NAME}>
            DEPENDS ${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running ${TEST_NAME} under Valgrind"
            VERBATIM
        )
    endif()
endfunction()

# Create a target that runs all unit tests under valgrind
add_custom_target(valgrind_test
    COMMENT "Running all tests under Valgrind"
)

# Add valgrind targets for each test when tests are built
if(MQTT_BUILD_TESTS)
    foreach(TEST_TARGET
        test_packet_id
        test_inflight
        test_buffer
        test_varint
        test_io_mux
        test_utf8
    )
        if(TARGET ${TEST_TARGET})
            add_valgrind_test(${TEST_TARGET})
            add_dependencies(valgrind_test valgrind_${TEST_TARGET})
        endif()
    endforeach()

    # MQTT 5.0 tests
    if(MQTT_ENABLE_V5 AND TARGET test_mqtt_v5_properties)
        add_valgrind_test(test_mqtt_v5_properties)
        add_dependencies(valgrind_test valgrind_test_mqtt_v5_properties)
    endif()

    # Pool allocator tests
    if(MQTT_ENABLE_POOL_ALLOCATOR AND TARGET test_pool)
        add_valgrind_test(test_pool)
        add_dependencies(valgrind_test valgrind_test_pool)
    endif()
endif()

# Create memcheck target (alias for consistency with CTest terminology)
add_custom_target(memcheck
    DEPENDS valgrind_test
    COMMENT "Memory check complete"
)
