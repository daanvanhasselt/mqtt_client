# =============================================================================
# Fuzz Testing for MQTT Client Library
# =============================================================================

# Fuzz test sources need access to internal headers
set(FUZZ_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/include
)

# Core source files needed for fuzz testing (to link parsing code)
set(FUZZ_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/mqtt_varint.c
    ${CMAKE_SOURCE_DIR}/src/core/mqtt_utf8.c
    ${CMAKE_SOURCE_DIR}/src/core/mqtt_fixed_header.c
    ${CMAKE_SOURCE_DIR}/src/protocol/mqtt_v311/mqtt_v311_parser.c
    ${CMAKE_SOURCE_DIR}/src/protocol/mqtt_v311/mqtt_v311_codec.c
    ${CMAKE_SOURCE_DIR}/src/memory/mqtt_memory.c
    ${CMAKE_SOURCE_DIR}/src/memory/mqtt_buffer.c
    ${CMAKE_SOURCE_DIR}/src/util/mqtt_error.c
)

# =============================================================================
# Standalone fuzz test (always available)
# =============================================================================

add_executable(fuzz_packet_standalone
    fuzz_packet_parser.c
    ${FUZZ_CORE_SOURCES}
)

target_include_directories(fuzz_packet_standalone PRIVATE ${FUZZ_INCLUDE_DIRS})
target_compile_definitions(fuzz_packet_standalone PRIVATE FUZZ_STANDALONE)

# =============================================================================
# Corpus generator
# =============================================================================

add_executable(generate_corpus generate_corpus.c)

# Custom target to generate corpus
add_custom_target(generate_fuzz_corpus
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/corpus
    COMMAND generate_corpus
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS generate_corpus
    COMMENT "Generating fuzz test corpus"
)

# =============================================================================
# AFL fuzz test (optional)
# =============================================================================

find_program(AFL_GCC afl-gcc)
find_program(AFL_CLANG afl-clang-fast)

if(AFL_GCC OR AFL_CLANG)
    message(STATUS "AFL fuzzer found - enabling AFL fuzz target")

    # Prefer afl-clang-fast if available
    if(AFL_CLANG)
        set(AFL_COMPILER ${AFL_CLANG})
    else()
        set(AFL_COMPILER ${AFL_GCC})
    endif()

    # Create AFL-instrumented version
    add_custom_target(fuzz_packet_afl
        COMMAND ${AFL_COMPILER}
            -DFUZZ_AFL
            -I${CMAKE_SOURCE_DIR}/include
            -I${CMAKE_SOURCE_DIR}/src
            -I${CMAKE_BINARY_DIR}/include
            -o fuzz_packet_afl
            ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_packet_parser.c
            ${FUZZ_CORE_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Building AFL-instrumented fuzz test"
    )

    # Add target to run AFL
    add_custom_target(run_afl_fuzz
        COMMAND ${CMAKE_COMMAND} -E echo "Starting AFL fuzzing..."
        COMMAND ${CMAKE_COMMAND} -E echo "Run: afl-fuzz -i corpus -o findings ./fuzz_packet_afl"
        DEPENDS fuzz_packet_afl generate_fuzz_corpus
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
else()
    message(STATUS "AFL fuzzer not found - AFL fuzz target disabled")
endif()

# =============================================================================
# libFuzzer test (optional, requires Clang)
# =============================================================================

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Clang detected - enabling libFuzzer target")

    add_executable(fuzz_packet_libfuzzer
        fuzz_packet_parser.c
        ${FUZZ_CORE_SOURCES}
    )

    target_include_directories(fuzz_packet_libfuzzer PRIVATE ${FUZZ_INCLUDE_DIRS})
    target_compile_definitions(fuzz_packet_libfuzzer PRIVATE FUZZ_LIBFUZZER)
    target_compile_options(fuzz_packet_libfuzzer PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_packet_libfuzzer PRIVATE -fsanitize=fuzzer,address)

    # Add target to run libFuzzer
    add_custom_target(run_libfuzzer
        COMMAND fuzz_packet_libfuzzer corpus/ -max_total_time=60
        DEPENDS fuzz_packet_libfuzzer generate_fuzz_corpus
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running libFuzzer for 60 seconds"
    )
else()
    message(STATUS "Clang not detected - libFuzzer target disabled")
endif()

# =============================================================================
# Convenience target to run standalone fuzz test
# =============================================================================

add_custom_target(run_fuzz_standalone
    COMMAND fuzz_packet_standalone -n 100000
    DEPENDS fuzz_packet_standalone
    COMMENT "Running standalone fuzz test (100k iterations)"
)

# =============================================================================
# CTest integration
# =============================================================================

add_test(
    NAME fuzz_packet_quick
    COMMAND fuzz_packet_standalone -n 10000
)

set_tests_properties(fuzz_packet_quick PROPERTIES
    TIMEOUT 60
    LABELS "fuzz"
)
