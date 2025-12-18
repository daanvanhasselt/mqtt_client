# MQTT Client Library

A modern, lightweight MQTT client library written in C99 with support for MQTT 3.1.1 and 5.0, TLS encryption, WebSocket transport, and both synchronous and asynchronous APIs.

## Features

- **MQTT 3.1.1 & 5.0 Protocol** - Full support for both protocol versions
- **Quality of Service** - QoS 0 (at most once), QoS 1 (at least once), QoS 2 (exactly once)
- **TLS/SSL Encryption** - Secure connections via OpenSSL backend
- **WebSocket Transport** - ws:// and wss:// connections with MQTT subprotocol
- **Synchronous API** - Simple blocking operations for straightforward use cases
- **Asynchronous API** - Non-blocking operations with callback support
- **Event Loop Integration** - Easy integration with external event loops (select/poll/epoll/kqueue)
- **Thread Safety** - Optional mutex protection for multi-threaded applications
- **Memory Pool Allocator** - Optional high-performance fixed-size block allocator
- **Cross-Platform** - POSIX-compliant (Linux, macOS), Windows stubs included
- **Zero External Dependencies** - Only standard C library and optional TLS backend

## Quick Start

### Building

```bash
# Clone the repository
git clone https://github.com/yourusername/mqtt_client.git
cd mqtt_client

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Run tests
ctest --output-on-failure
```

### Building with TLS Support

```bash
# Install OpenSSL development libraries
# Ubuntu/Debian:
sudo apt-get install libssl-dev

# RHEL/Fedora:
sudo dnf install openssl-devel

# macOS:
brew install openssl

# Configure with TLS enabled
cmake .. -DMQTT_ENABLE_TLS=ON -DMQTT_TLS_BACKEND=openssl
make
```

### Basic Usage

```c
#include "mqtt/mqtt.h"

int main() {
    // Initialize library
    mqtt_lib_init();

    // Create client
    mqtt_client_config_t config = {0};
    config.protocol_version = MQTT_VERSION_3_1_1;
    config.send_buffer_size = 4096;
    config.recv_buffer_size = 4096;

    mqtt_client_t *client = mqtt_client_create(&config);

    // Connect to broker
    mqtt_connect_opts_t opts = {0};
    opts.host = "test.mosquitto.org";
    opts.port = 1883;
    opts.client_id = "my_client";
    opts.clean_session = true;
    opts.keepalive_sec = 60;

    mqtt_connect(client, &opts);

    // Publish a message
    mqtt_publish_opts_t pub = {0};
    pub.topic = "test/topic";
    pub.payload = (uint8_t *)"Hello MQTT!";
    pub.payload_len = 11;
    pub.qos = MQTT_QOS_1;

    mqtt_publish(client, &pub);

    // Process incoming messages
    mqtt_loop(client, 1000);

    // Cleanup
    mqtt_disconnect(client);
    mqtt_client_destroy(client);
    mqtt_lib_cleanup();

    return 0;
}
```

## API Reference

### Initialization

| Function | Description |
|----------|-------------|
| `mqtt_lib_init()` | Initialize the MQTT library (call once at startup) |
| `mqtt_lib_cleanup()` | Cleanup library resources (call once at shutdown) |
| `mqtt_client_create()` | Create a new MQTT client instance |
| `mqtt_client_destroy()` | Destroy client and free resources |

### Connection

| Function | Description |
|----------|-------------|
| `mqtt_connect()` | Connect to MQTT broker (blocking) |
| `mqtt_connect_async()` | Connect to MQTT broker (non-blocking) |
| `mqtt_disconnect()` | Disconnect from broker |
| `mqtt_is_connected()` | Check connection status |

### Publishing

| Function | Description |
|----------|-------------|
| `mqtt_publish()` | Publish message (blocking for QoS 1/2) |
| `mqtt_publish_async()` | Publish message (non-blocking) |

### Subscribing

| Function | Description |
|----------|-------------|
| `mqtt_subscribe()` | Subscribe to topic(s) (blocking) |
| `mqtt_subscribe_async()` | Subscribe to topic(s) (non-blocking) |
| `mqtt_unsubscribe()` | Unsubscribe from topic(s) |
| `mqtt_unsubscribe_async()` | Unsubscribe (non-blocking) |

### Event Loop

| Function | Description |
|----------|-------------|
| `mqtt_loop()` | Process network I/O and callbacks (blocking with timeout) |
| `mqtt_get_socket_fd()` | Get socket file descriptor for external polling |
| `mqtt_want_write()` | Check if client has data to send |
| `mqtt_process_read()` | Process incoming data (for event loop integration) |
| `mqtt_process_write()` | Send pending data (for event loop integration) |

### Callbacks

```c
typedef struct {
    mqtt_on_connect_cb on_connect;           // Connection established
    mqtt_on_disconnect_cb on_disconnect;     // Connection lost
    mqtt_on_message_cb on_message;           // Message received
    mqtt_on_publish_complete_cb on_publish_complete;  // QoS 1/2 publish acknowledged
    mqtt_on_publish_failed_cb on_publish_failed;      // Publish failed (max retries)
    mqtt_on_subscribe_cb on_subscribe;       // Subscribe acknowledged
    void *user_data;                         // User context pointer
} mqtt_callbacks_t;

mqtt_set_callbacks(client, &callbacks);
```

## Configuration Options

### CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `MQTT_ENABLE_V311` | ON | Enable MQTT 3.1.1 support |
| `MQTT_ENABLE_V5` | ON | Enable MQTT 5.0 support |
| `MQTT_ENABLE_TLS` | ON | Enable TLS/SSL support |
| `MQTT_TLS_BACKEND` | openssl | TLS backend: `openssl` or `mbedtls` |
| `MQTT_ENABLE_WEBSOCKET` | OFF | Enable WebSocket transport |
| `MQTT_THREAD_SAFE` | ON | Enable thread-safe operations |
| `MQTT_ENABLE_POOL_ALLOCATOR` | OFF | Enable memory pool allocator |
| `MQTT_BUILD_SHARED` | ON | Build shared library |
| `MQTT_BUILD_STATIC` | ON | Build static library |
| `MQTT_BUILD_EXAMPLES` | ON | Build example programs |
| `MQTT_BUILD_TESTS` | ON | Build unit tests |
| `MQTT_BUILD_BENCHMARKS` | OFF | Build performance benchmarks |

### Client Configuration

```c
typedef struct {
    mqtt_protocol_version_t protocol_version;  // MQTT_VERSION_3_1_1 or MQTT_VERSION_5_0
    size_t send_buffer_size;                   // Send buffer size (default: 4096)
    size_t recv_buffer_size;                   // Receive buffer size (default: 4096)
    size_t max_inflight_messages;              // Max QoS 1/2 in-flight (default: 10)
    uint32_t retry_timeout_ms;                 // Retry interval (default: 5000)
    uint32_t max_retries;                      // Max retry attempts (default: 3)
} mqtt_client_config_t;
```

### TLS Configuration

```c
typedef struct {
    const char *ca_cert_path;      // Path to CA certificate (PEM)
    const uint8_t *ca_cert_buffer; // CA certificate in memory
    size_t ca_cert_len;            // Length of CA cert buffer
    const char *client_cert_path;  // Client certificate for mTLS
    const char *client_key_path;   // Client private key for mTLS
    const char **alpn_protocols;   // ALPN protocol list (NULL-terminated)
    bool verify_peer;              // Verify server certificate
    bool verify_hostname;          // Verify hostname matches cert
    uint16_t min_tls_version;      // Minimum TLS version (0x0303=TLS1.2)
} mqtt_tls_config_t;
```

## Examples

### Secure TLS Connection

```c
mqtt_tls_config_t tls = {0};
tls.verify_peer = true;
tls.verify_hostname = true;
// Uses system CA store by default

mqtt_connect_opts_t opts = {0};
opts.host = "test.mosquitto.org";
opts.port = 8883;  // TLS port
opts.transport_type = MQTT_TRANSPORT_TLS;
opts.tls_config = &tls;

mqtt_connect(client, &opts);
```

### WebSocket Connection

```c
mqtt_connect_opts_t opts = {0};
opts.host = "broker.example.com";
opts.port = 80;
opts.transport_type = MQTT_TRANSPORT_WEBSOCKET;
opts.ws_path = "/mqtt";  // WebSocket path

mqtt_connect(client, &opts);
```

### WebSocket over TLS (wss://)

```c
mqtt_tls_config_t tls = {0};
tls.verify_peer = true;

mqtt_connect_opts_t opts = {0};
opts.host = "broker.example.com";
opts.port = 443;
opts.transport_type = MQTT_TRANSPORT_WEBSOCKET_TLS;
opts.ws_path = "/mqtt";
opts.tls_config = &tls;

mqtt_connect(client, &opts);
```

### MQTT 5.0 with Properties

```c
mqtt_client_config_t config = {0};
config.protocol_version = MQTT_VERSION_5_0;

mqtt_client_t *client = mqtt_client_create(&config);

// Publish with MQTT 5.0 properties
mqtt_publish_opts_t pub = {0};
pub.topic = "sensor/data";
pub.payload = data;
pub.payload_len = len;
pub.qos = MQTT_QOS_1;
pub.content_type = "application/json";
pub.message_expiry_interval = 3600;  // 1 hour expiry
pub.response_topic = "response/topic";

mqtt_publish(client, &pub);
```

### Event Loop Integration

```c
// Get socket for external polling
int fd = mqtt_get_socket_fd(client);

// In your event loop:
struct pollfd pfd = {fd, POLLIN | (mqtt_want_write(client) ? POLLOUT : 0), 0};
poll(&pfd, 1, timeout_ms);

if (pfd.revents & POLLIN) {
    mqtt_process_read(client);
}
if (pfd.revents & POLLOUT) {
    mqtt_process_write(client);
}
```

### QoS 2 Publishing with Callbacks

```c
void on_complete(mqtt_client_t *client, void *data, uint16_t packet_id) {
    printf("Message %u delivered exactly once!\n", packet_id);
}

mqtt_callbacks_t cb = {.on_publish_complete = on_complete};
mqtt_set_callbacks(client, &cb);

mqtt_publish_opts_t pub = {0};
pub.topic = "important/data";
pub.payload = data;
pub.payload_len = len;
pub.qos = MQTT_QOS_2;  // Exactly-once delivery

mqtt_publish(client, &pub);

// Process until acknowledged
while (!done) {
    mqtt_loop(client, 100);
}
```

## Project Structure

```
mqtt_client/
├── include/mqtt/          # Public headers
│   ├── mqtt.h             # Main API
│   ├── mqtt_types.h       # Type definitions
│   ├── mqtt_error.h       # Error codes
│   └── mqtt_config.h      # Build configuration (generated)
├── src/
│   ├── client/            # MQTT client implementation
│   ├── core/              # Protocol utilities (varint, UTF-8)
│   ├── memory/            # Buffer management, pool allocator
│   ├── platform/          # Platform abstraction (POSIX, Windows)
│   │   ├── posix/         # Linux/macOS implementation
│   │   └── windows/       # Windows stubs
│   ├── protocol/          # Protocol codecs
│   │   ├── mqtt_v311/     # MQTT 3.1.1 codec
│   │   └── mqtt_v5/       # MQTT 5.0 codec
│   ├── transport/         # Transport layer
│   │   ├── tcp/           # TCP transport
│   │   ├── tls/           # TLS transport (OpenSSL)
│   │   └── websocket/     # WebSocket transport
│   └── util/              # Utilities
├── examples/              # Example programs
├── tests/                 # Unit tests
├── benchmarks/            # Performance benchmarks
└── cmake/                 # CMake modules
```

## Error Handling

All functions return `mqtt_error_t`. Use `mqtt_error_str()` for human-readable messages:

```c
mqtt_error_t err = mqtt_connect(client, &opts);
if (err != MQTT_OK) {
    fprintf(stderr, "Connect failed: %s\n", mqtt_error_str(err));
}
```

Common error codes:
- `MQTT_OK` - Success
- `MQTT_ERR_NOMEM` - Out of memory
- `MQTT_ERR_TIMEOUT` - Operation timed out
- `MQTT_ERR_NOT_CONNECTED` - Not connected to broker
- `MQTT_ERR_TLS_HANDSHAKE` - TLS handshake failed
- `MQTT_ERR_WOULD_BLOCK` - Non-blocking operation in progress

## Testing

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test
./tests/test_buffer
./tests/test_pool
./tests/test_utf8

# Run benchmarks (if enabled)
cmake .. -DMQTT_BUILD_BENCHMARKS=ON -DMQTT_ENABLE_POOL_ALLOCATOR=ON
make
./benchmarks/bench_pool

# Test against live broker
./examples/simple_publish test.mosquitto.org 1883 test/topic "Hello"
./examples/tls_publish test.mosquitto.org 8883 test/topic "Secure Hello"
```

## Installation

```bash
# Build and install
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make
sudo make install

# Use with pkg-config
pkg-config --cflags --libs mqtt_client

# Use with CMake find_package
find_package(mqtt_client REQUIRED)
target_link_libraries(myapp mqtt::mqtt_client_shared)
```

## Roadmap

- [x] Phase 1: Foundation (MQTT 3.1.1 over TCP)
- [x] Phase 2: QoS & Session Management
- [x] Phase 3: Async API & Event Loop
- [x] Phase 4: TLS Support (OpenSSL)
- [x] Phase 5: MQTT 5.0 Protocol
- [x] Phase 6: WebSocket Transport
- [x] Phase 7: Platform Ports (Linux, macOS, Windows stubs)
- [x] Phase 8: Production Polish

**All phases complete!** See [PROJECT_PLAN.md](PROJECT_PLAN.md) for detailed progress.

## License

MIT License - see LICENSE file for details.

## Contributing

Contributions are welcome! Please read the contributing guidelines before submitting pull requests.

## Acknowledgments

- Eclipse Mosquitto for the public test broker
- OpenSSL project for TLS implementation
