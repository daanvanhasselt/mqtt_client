################################################################################
# MQTT Client Library - Root CMakeLists.txt
#
# A modern, flexible MQTT client library supporting MQTT v3.1.1 and v5.0
################################################################################

cmake_minimum_required(VERSION 3.14)

project(mqtt_client
    VERSION 1.0.0
    LANGUAGES C
    DESCRIPTION "Modern MQTT client library with multi-version support"
)

################################################################################
# C Standard
################################################################################
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

################################################################################
# Build Options
################################################################################

# Protocol Version Support
option(MQTT_ENABLE_V311 "Enable MQTT v3.1.1 support" ON)
option(MQTT_ENABLE_V5 "Enable MQTT v5.0 support" ON)

# Transport Layer Options
option(MQTT_ENABLE_TLS "Enable TLS/SSL support" ON)
option(MQTT_ENABLE_WEBSOCKET "Enable WebSocket transport" OFF)

# TLS Backend Selection
set(MQTT_TLS_BACKEND "openssl" CACHE STRING "TLS backend (openssl or mbedtls)")
set_property(CACHE MQTT_TLS_BACKEND PROPERTY STRINGS "openssl" "mbedtls")

# Threading and Memory Options
option(MQTT_THREAD_SAFE "Enable thread-safe operations" ON)
option(MQTT_ENABLE_POOL_ALLOCATOR "Use memory pool allocator" OFF)

# Library Type Options
option(MQTT_BUILD_SHARED "Build shared library" ON)
option(MQTT_BUILD_STATIC "Build static library" ON)

# Additional Components
option(MQTT_BUILD_EXAMPLES "Build example programs" ON)
option(MQTT_BUILD_TESTS "Build unit tests" ON)
option(MQTT_ENABLE_LOGGING "Enable debug logging" ON)

################################################################################
# Platform Detection
################################################################################
if(WIN32)
    set(MQTT_PLATFORM_WINDOWS TRUE)
    message(STATUS "Platform: Windows")
elseif(APPLE)
    set(MQTT_PLATFORM_MACOS TRUE)
    message(STATUS "Platform: macOS")
elseif(UNIX)
    set(MQTT_PLATFORM_LINUX TRUE)
    message(STATUS "Platform: Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

################################################################################
# Configuration Header Generation
################################################################################
include(GNUInstallDirs)

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/mqtt_config.h.in"
    "${PROJECT_BINARY_DIR}/include/mqtt/mqtt_config.h"
    @ONLY
)

################################################################################
# Source Files - Core Module
################################################################################
set(MQTT_CORE_SOURCES
    src/core/mqtt_fixed_header.c
    src/core/mqtt_utf8.c
    src/core/mqtt_varint.c
)

################################################################################
# Source Files - Protocol Modules
################################################################################
set(MQTT_V311_SOURCES
    src/protocol/mqtt_v311/mqtt_v311_codec.c
    src/protocol/mqtt_v311/mqtt_v311_parser.c
)

set(MQTT_V5_SOURCES
    src/protocol/mqtt_v5/mqtt_v5_properties.c
    src/protocol/mqtt_v5/mqtt_v5_codec.c
)

################################################################################
# Source Files - Client Module
################################################################################
set(MQTT_CLIENT_SOURCES
    src/client/mqtt_client.c
    src/client/mqtt_packet_id.c
    src/client/mqtt_inflight.c
    src/client/mqtt_qos2_recv.c
)

################################################################################
# Source Files - Transport Layer
################################################################################
set(MQTT_TRANSPORT_SOURCES
    src/transport/mqtt_transport.c
    src/transport/tcp/mqtt_tcp.c
)

if(MQTT_ENABLE_TLS)
    if(MQTT_TLS_BACKEND STREQUAL "openssl")
        list(APPEND MQTT_TRANSPORT_SOURCES src/transport/tls/mqtt_tls_openssl.c)
    elseif(MQTT_TLS_BACKEND STREQUAL "mbedtls")
        list(APPEND MQTT_TRANSPORT_SOURCES src/transport/tls/mqtt_tls_mbedtls.c)
    endif()
endif()

if(MQTT_ENABLE_WEBSOCKET)
    list(APPEND MQTT_TRANSPORT_SOURCES
        src/transport/websocket/mqtt_websocket.c
        src/transport/websocket/mqtt_ws_transport.c
    )
endif()

################################################################################
# Source Files - Platform Abstraction Layer
################################################################################
if(MQTT_PLATFORM_WINDOWS)
    set(MQTT_PLATFORM_SOURCES
        # TODO: Add Windows platform sources
    )
elseif(MQTT_PLATFORM_LINUX OR MQTT_PLATFORM_MACOS)
    set(MQTT_PLATFORM_SOURCES
        src/platform/posix/mqtt_socket_posix.c
        src/platform/posix/mqtt_time_posix.c
        src/platform/posix/mqtt_thread_posix.c
    )
endif()

################################################################################
# Source Files - Memory Management
################################################################################
set(MQTT_MEMORY_SOURCES
    src/memory/mqtt_memory.c
    src/memory/mqtt_buffer.c
)

if(MQTT_ENABLE_POOL_ALLOCATOR)
    # TODO: list(APPEND MQTT_MEMORY_SOURCES src/memory/mqtt_pool.c)
endif()

################################################################################
# Source Files - Utilities
################################################################################
set(MQTT_UTIL_SOURCES
    src/util/mqtt_error.c
)

################################################################################
# Combine All Sources
################################################################################
set(MQTT_ALL_SOURCES
    ${MQTT_CORE_SOURCES}
    ${MQTT_CLIENT_SOURCES}
    ${MQTT_TRANSPORT_SOURCES}
    ${MQTT_IO_SOURCES}
    ${MQTT_PLATFORM_SOURCES}
    ${MQTT_MEMORY_SOURCES}
    ${MQTT_UTIL_SOURCES}
)

# Add protocol-specific sources based on options
if(MQTT_ENABLE_V311)
    list(APPEND MQTT_ALL_SOURCES ${MQTT_V311_SOURCES})
endif()

if(MQTT_ENABLE_V5)
    list(APPEND MQTT_ALL_SOURCES ${MQTT_V5_SOURCES})
endif()

################################################################################
# Include Directories
################################################################################
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
message(STATUS "PROJECT_BINARY_DIR: ${PROJECT_BINARY_DIR}")
set(MQTT_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
)
message(STATUS "MQTT_INCLUDE_DIRS: ${MQTT_INCLUDE_DIRS}")

################################################################################
# Shared Library Target
################################################################################
if(MQTT_BUILD_SHARED)
    add_library(mqtt_client_shared SHARED ${MQTT_ALL_SOURCES})

    set_target_properties(mqtt_client_shared PROPERTIES
        OUTPUT_NAME mqtt_client
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
    )

    target_include_directories(mqtt_client_shared
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    target_compile_definitions(mqtt_client_shared
        PRIVATE MQTT_BUILDING_DLL
        PUBLIC MQTT_SHARED_LIB
    )

    # Platform-specific libraries
    if(MQTT_PLATFORM_WINDOWS)
        target_link_libraries(mqtt_client_shared PRIVATE ws2_32)
    elseif(MQTT_PLATFORM_LINUX)
        target_link_libraries(mqtt_client_shared PRIVATE pthread)
    endif()

    # TLS libraries
    if(MQTT_ENABLE_TLS)
        if(MQTT_TLS_BACKEND STREQUAL "openssl")
            find_package(OpenSSL REQUIRED)
            target_link_libraries(mqtt_client_shared PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        elseif(MQTT_TLS_BACKEND STREQUAL "mbedtls")
            find_package(MbedTLS REQUIRED)
            target_link_libraries(mqtt_client_shared PRIVATE ${MBEDTLS_LIBRARIES})
            target_include_directories(mqtt_client_shared PRIVATE ${MBEDTLS_INCLUDE_DIRS})
        endif()
    endif()
endif()

################################################################################
# Static Library Target
################################################################################
if(MQTT_BUILD_STATIC)
    add_library(mqtt_client_static STATIC ${MQTT_ALL_SOURCES})

    set_target_properties(mqtt_client_static PROPERTIES
        OUTPUT_NAME mqtt_client_static
        VERSION ${PROJECT_VERSION}
        C_VISIBILITY_PRESET hidden
    )

    target_include_directories(mqtt_client_static
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    target_compile_definitions(mqtt_client_static
        PUBLIC MQTT_STATIC_LIB
    )

    # Platform-specific libraries
    if(MQTT_PLATFORM_WINDOWS)
        target_link_libraries(mqtt_client_static PRIVATE ws2_32)
    elseif(MQTT_PLATFORM_LINUX)
        target_link_libraries(mqtt_client_static PRIVATE pthread)
    endif()

    # TLS libraries
    if(MQTT_ENABLE_TLS)
        if(MQTT_TLS_BACKEND STREQUAL "openssl")
            find_package(OpenSSL REQUIRED)
            target_link_libraries(mqtt_client_static PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        elseif(MQTT_TLS_BACKEND STREQUAL "mbedtls")
            find_package(MbedTLS REQUIRED)
            target_link_libraries(mqtt_client_static PRIVATE ${MBEDTLS_LIBRARIES})
            target_include_directories(mqtt_client_static PRIVATE ${MBEDTLS_INCLUDE_DIRS})
        endif()
    endif()
endif()

################################################################################
# Compiler Warnings
################################################################################
if(MQTT_BUILD_SHARED)
    target_compile_options(mqtt_client_shared PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
        $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
    )
endif()

if(MQTT_BUILD_STATIC)
    target_compile_options(mqtt_client_static PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
        $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
    )
endif()

################################################################################
# Installation Rules
################################################################################
include(GNUInstallDirs)

# Install libraries
if(MQTT_BUILD_SHARED)
    install(TARGETS mqtt_client_shared
        EXPORT mqtt_client_targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

if(MQTT_BUILD_STATIC)
    install(TARGETS mqtt_client_static
        EXPORT mqtt_client_targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# Install headers
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/mqtt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Install generated config header
install(FILES ${PROJECT_BINARY_DIR}/include/mqtt/mqtt_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mqtt
)

# Install CMake config files
install(EXPORT mqtt_client_targets
    FILE mqtt_client_targets.cmake
    NAMESPACE mqtt::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtt_client
)

# Generate and install package config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/mqtt_client-config.cmake.in"
    "${PROJECT_BINARY_DIR}/mqtt_client-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtt_client
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/mqtt_client-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/mqtt_client-config.cmake"
    "${PROJECT_BINARY_DIR}/mqtt_client-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtt_client
)

################################################################################
# Subdirectories
################################################################################
if(MQTT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MQTT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

################################################################################
# Summary
################################################################################
message(STATUS "")
message(STATUS "MQTT Client Library Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "")
message(STATUS "Protocol Support:")
message(STATUS "  MQTT v3.1.1: ${MQTT_ENABLE_V311}")
message(STATUS "  MQTT v5.0: ${MQTT_ENABLE_V5}")
message(STATUS "")
message(STATUS "Transport Options:")
message(STATUS "  TLS Support: ${MQTT_ENABLE_TLS}")
if(MQTT_ENABLE_TLS)
message(STATUS "  TLS Backend: ${MQTT_TLS_BACKEND}")
endif()
message(STATUS "  WebSocket Support: ${MQTT_ENABLE_WEBSOCKET}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Thread Safe: ${MQTT_THREAD_SAFE}")
message(STATUS "  Pool Allocator: ${MQTT_ENABLE_POOL_ALLOCATOR}")
message(STATUS "  Logging: ${MQTT_ENABLE_LOGGING}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Build Shared: ${MQTT_BUILD_SHARED}")
message(STATUS "  Build Static: ${MQTT_BUILD_STATIC}")
message(STATUS "  Build Examples: ${MQTT_BUILD_EXAMPLES}")
message(STATUS "  Build Tests: ${MQTT_BUILD_TESTS}")
message(STATUS "")
