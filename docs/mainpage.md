# MQTT Client Library {#mainpage}

A high-performance, portable MQTT client library written in C.

## Features

- **Protocol Support**
  - MQTT 3.1.1 (full compliance)
  - MQTT 5.0 (with properties, enhanced auth, server redirect)

- **Transport Layers**
  - TCP (plain socket)
  - TLS/SSL (optional, via OpenSSL or mbedTLS)
  - WebSocket (ws:// and wss://)
  - HTTP CONNECT proxy support

- **Architecture**
  - Synchronous and asynchronous APIs
  - Event-driven with callbacks
  - Thread-safe operations (optional)
  - Custom memory allocators
  - Pool allocator for high performance

- **Quality**
  - Zero external dependencies (core library)
  - Comprehensive error handling
  - Valgrind clean
  - Unit and integration tested

## Quick Start

```c
#include <mqtt/mqtt.h>

int main(void) {
    mqtt_lib_init();

    mqtt_client_config_t config = {
        .protocol_version = MQTT_VERSION_3_1_1,
        .transport_type = MQTT_TRANSPORT_TCP
    };

    mqtt_client_t *client = mqtt_client_create(&config);

    mqtt_connect_opts_t opts = {
        .host = "broker.example.com",
        .port = 1883,
        .client_id = "my_client",
        .keepalive_sec = 60,
        .clean_session = true
    };

    if (mqtt_connect(client, &opts) == MQTT_OK) {
        // Connected! Publish, subscribe, etc.
        mqtt_disconnect(client);
    }

    mqtt_client_destroy(client);
    mqtt_lib_cleanup();
    return 0;
}
```

## API Modules

- @ref mqtt.h "Core API" - Main client interface
- @ref mqtt_types.h "Types" - Data structures and callbacks
- @ref mqtt_error.h "Error Handling" - Error codes and messages

## Building

```bash
mkdir build && cd build
cmake ..
make
```

### Build Options

| Option | Description | Default |
|--------|-------------|---------|
| `MQTT_ENABLE_V5` | Enable MQTT 5.0 support | ON |
| `MQTT_ENABLE_TLS` | Enable TLS/SSL support | OFF |
| `MQTT_ENABLE_WEBSOCKET` | Enable WebSocket transport | ON |
| `MQTT_THREAD_SAFE` | Enable thread safety | ON |
| `MQTT_ENABLE_POOL_ALLOCATOR` | Enable pool allocator | OFF |
| `MQTT_BUILD_TESTS` | Build unit tests | OFF |
| `MQTT_BUILD_EXAMPLES` | Build example programs | OFF |

## License

MIT License - see LICENSE file for details.
