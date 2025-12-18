# MQTT Client Library - Quick Start Guide

## Installation

### Building from Source

```bash
git clone https://github.com/your-repo/mqtt_client.git
cd mqtt_client
mkdir build && cd build
cmake ..
make
sudo make install
```

### CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `MQTT_ENABLE_V5` | Enable MQTT 5.0 | ON |
| `MQTT_ENABLE_TLS` | Enable TLS | OFF |
| `MQTT_ENABLE_WEBSOCKET` | Enable WebSocket | ON |
| `MQTT_THREAD_SAFE` | Thread safety | ON |
| `MQTT_BUILD_TESTS` | Build tests | OFF |
| `MQTT_BUILD_EXAMPLES` | Build examples | OFF |

Example with TLS:
```bash
cmake -DMQTT_ENABLE_TLS=ON -DMQTT_TLS_BACKEND=OpenSSL ..
```

## Basic Usage

### 1. Initialize the Library

```c
#include <mqtt/mqtt.h>

int main(void) {
    mqtt_error_t err = mqtt_lib_init();
    if (err != MQTT_OK) {
        fprintf(stderr, "Init failed: %s\n", mqtt_error_str(err));
        return 1;
    }

    // ... use the library ...

    mqtt_lib_cleanup();
    return 0;
}
```

### 2. Create a Client

```c
mqtt_client_config_t config = {
    .protocol_version = MQTT_VERSION_3_1_1,
    .transport_type = MQTT_TRANSPORT_TCP
};

mqtt_client_t *client = mqtt_client_create(&config);
if (!client) {
    fprintf(stderr, "Failed to create client\n");
    return 1;
}
```

### 3. Set Up Callbacks (Async Mode)

```c
void on_connect(mqtt_client_t *client, void *user_data, bool session_present) {
    printf("Connected! Session present: %d\n", session_present);
}

void on_message(mqtt_client_t *client, void *user_data,
                const mqtt_message_t *msg) {
    printf("Received on '%s': %.*s\n",
           msg->topic,
           (int)msg->payload_len,
           (char *)msg->payload);
}

mqtt_callbacks_t callbacks = {
    .on_connect = on_connect,
    .on_message = on_message,
    .user_data = NULL
};

mqtt_set_callbacks(client, &callbacks);
```

### 4. Connect to Broker

```c
mqtt_connect_opts_t opts = {
    .host = "test.mosquitto.org",
    .port = 1883,
    .client_id = "my_client_123",
    .keepalive_sec = 60,
    .clean_session = true
};

// Synchronous connect
err = mqtt_connect(client, &opts);
if (err != MQTT_OK) {
    fprintf(stderr, "Connect failed: %s\n", mqtt_error_str(err));
}

// Or asynchronous
err = mqtt_connect_async(client, &opts);
// Then call mqtt_loop() repeatedly to process events
```

### 5. Subscribe to Topics

```c
mqtt_subscribe_opts_t sub_opts = {
    .topic_filter = "sensors/#",
    .max_qos = MQTT_QOS_1
};

err = mqtt_subscribe(client, &sub_opts, 1);
```

### 6. Publish Messages

```c
mqtt_publish_opts_t pub_opts = {
    .topic = "sensors/temperature",
    .payload = (uint8_t *)"23.5",
    .payload_len = 4,
    .qos = MQTT_QOS_1,
    .retain = false
};

err = mqtt_publish(client, &pub_opts);
```

### 7. Event Loop (Async Mode)

```c
while (running) {
    err = mqtt_loop(client, 100);  // 100ms timeout
    if (err != MQTT_OK && err != MQTT_ERR_TIMEOUT) {
        break;
    }
}
```

### 8. Disconnect and Cleanup

```c
mqtt_disconnect(client);
mqtt_client_destroy(client);
```

## Common Patterns

### Reconnection Logic

```c
void reconnect_with_backoff(mqtt_client_t *client, mqtt_connect_opts_t *opts) {
    int delay = 1;
    const int max_delay = 60;

    while (true) {
        mqtt_error_t err = mqtt_connect(client, opts);
        if (err == MQTT_OK) {
            // Restore subscriptions if needed
            if (mqtt_get_stored_subscription_count(client) > 0) {
                mqtt_restore_subscriptions(client);
            }
            break;
        }

        fprintf(stderr, "Connect failed, retry in %ds\n", delay);
        sleep(delay);
        delay = (delay * 2 > max_delay) ? max_delay : delay * 2;
    }
}
```

### Will Message (Last Will and Testament)

```c
mqtt_connect_opts_t opts = {
    .host = "broker.example.com",
    .port = 1883,
    .client_id = "sensor_01",
    .will = {
        .topic = "sensors/sensor_01/status",
        .payload = (uint8_t *)"offline",
        .payload_len = 7,
        .qos = MQTT_QOS_1,
        .retain = true
    }
};
```

### WebSocket Connection

```c
mqtt_client_config_t config = {
    .transport_type = MQTT_TRANSPORT_WEBSOCKET
};

mqtt_connect_opts_t opts = {
    .host = "broker.example.com",
    .port = 8080,
    .client_id = "ws_client",
    .ws = {
        .path = "/mqtt",
        .subprotocol = "mqtt"
    }
};
```

### TLS Connection

```c
mqtt_client_config_t config = {
    .transport_type = MQTT_TRANSPORT_TLS
};

mqtt_connect_opts_t opts = {
    .host = "broker.example.com",
    .port = 8883,
    .client_id = "secure_client",
    .tls = {
        .ca_cert_path = "/path/to/ca.crt",
        .verify_server = true
    }
};
```

## Error Handling

All functions return `mqtt_error_t`. Check for `MQTT_OK`:

```c
mqtt_error_t err = mqtt_publish(client, &opts);
if (err != MQTT_OK) {
    fprintf(stderr, "Error: %s\n", mqtt_error_str(err));
}
```

Common error codes:
- `MQTT_OK` - Success
- `MQTT_ERR_NOMEM` - Out of memory
- `MQTT_ERR_INVALID_ARG` - Invalid argument
- `MQTT_ERR_NOT_CONNECTED` - Not connected
- `MQTT_ERR_NETWORK` - Network error
- `MQTT_ERR_TIMEOUT` - Operation timed out
- `MQTT_ERR_PROTOCOL` - Protocol error

## Thread Safety

The library is thread-safe when built with `MQTT_THREAD_SAFE=ON` (default).
Multiple threads can share a client, but callbacks are invoked from the
thread calling `mqtt_loop()` or `mqtt_process_read()`.

## Memory Management

### Custom Allocator

```c
void *my_malloc(size_t size, void *ctx) {
    return custom_alloc(size);
}

mqtt_allocator_t alloc = {
    .malloc_fn = my_malloc,
    .realloc_fn = my_realloc,
    .free_fn = my_free,
    .ctx = NULL
};

mqtt_set_allocator(&alloc);  // Before mqtt_lib_init()
mqtt_lib_init();
```

### Pool Allocator

For high-performance scenarios:

```c
mqtt_lib_init();
mqtt_pool_init(NULL);  // Use defaults

mqtt_client_config_t config = {
    .use_pool_allocator = true
};
```

## MQTT 5.0 Features

### User Properties

```c
mqtt_property_t *props = mqtt_property_create(MQTT_PROP_USER_PROPERTY);
mqtt_property_set_string_pair(props, "key", "value");

mqtt_publish_opts_t opts = {
    .topic = "test",
    .payload = data,
    .payload_len = len,
    .properties = props
};

mqtt_publish(client, &opts);
mqtt_property_free_all(props);
```

### Response Information

```c
void on_connect(mqtt_client_t *client, void *user_data, bool session_present) {
    const char *assigned_id = mqtt_get_assigned_client_id(client);
    if (assigned_id) {
        printf("Broker assigned client ID: %s\n", assigned_id);
    }
}
```

## Linking

### pkg-config

```bash
gcc -o myapp myapp.c $(pkg-config --cflags --libs mqtt-client)
```

### CMake

```cmake
find_package(MqttClient REQUIRED)
target_link_libraries(myapp PRIVATE MqttClient::mqtt_client)
```
