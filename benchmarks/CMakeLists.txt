# MQTT Client Library - Benchmarks CMakeLists.txt

# Function to add a benchmark
function(add_mqtt_benchmark BENCH_NAME BENCH_SOURCE)
    add_executable(${BENCH_NAME} ${BENCH_SOURCE})

    target_link_libraries(${BENCH_NAME} PRIVATE
        mqtt_client_static
    )

    target_include_directories(${BENCH_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
    )

    # Optimize for benchmarks
    target_compile_options(${BENCH_NAME} PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-O3>
    )
endfunction()

# Memory pool benchmark (only if pool allocator is enabled)
if(MQTT_ENABLE_POOL_ALLOCATOR)
    add_mqtt_benchmark(bench_pool bench_pool.c)
endif()
