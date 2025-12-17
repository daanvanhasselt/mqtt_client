# MQTT Client Library - Project Plan

## Phase 1: Foundation ✅
**Goal**: Basic MQTT 3.1.1 over TCP (sync API only)

- [x] Project directory structure
- [x] CMake build system with configuration options
- [x] Public API headers (mqtt.h, mqtt_types.h, mqtt_error.h)
- [x] Memory management (allocator interface, buffers)
- [x] POSIX platform abstraction (sockets, time, mutex)
- [x] Core protocol utilities (varint, UTF-8, fixed header)
- [x] MQTT 3.1.1 packet codec (CONNECT, CONNACK, PUBLISH, SUBSCRIBE, PING, DISCONNECT)
- [x] TCP transport with timeout handling
- [x] Basic synchronous client (connect, publish QoS 0, subscribe, loop)
- [x] Example programs (simple_publish, simple_subscribe)
- [x] Tested against public broker (test.mosquitto.org)

---

## Phase 2: QoS & Session Management ✅
**Goal**: Full QoS 0/1/2 support with reliable delivery

- [x] Packet ID generation and tracking
- [x] QoS 1 implementation
  - [x] PUBACK encoding/decoding
  - [x] Inflight message tracking
  - [x] Retry on timeout
- [x] QoS 2 implementation
  - [x] PUBREC/PUBREL/PUBCOMP state machine
  - [x] Exactly-once delivery guarantee
- [x] Message store (inflight queue with configurable limit)
- [x] Keepalive improvements
  - [x] Automatic PINGREQ scheduling
  - [x] Connection timeout detection
- [x] Session state management
  - [x] Clean session flag handling
  - [ ] Subscription restoration on reconnect (deferred to Phase 3)

---

## Phase 3: Async API & Event Loop ✅
**Goal**: Non-blocking operations and event loop integration

- [x] I/O abstraction layer
  - [x] poll() implementation (used in socket layer)
  - [x] Non-blocking socket support
- [x] Async client operations
  - [x] mqtt_connect_async()
  - [x] mqtt_publish_async()
  - [x] mqtt_subscribe_async()
  - [x] mqtt_unsubscribe_async()
- [x] Callback system
  - [x] on_connect callback
  - [x] on_disconnect callback
  - [x] on_message callback
  - [x] on_publish_complete callback (QoS 1/2)
  - [x] on_publish_failed callback (max retries exceeded)
  - [x] on_subscribe callback
- [x] Event loop integration
  - [x] mqtt_get_socket_fd()
  - [x] mqtt_want_write()
  - [x] mqtt_process_read()
  - [x] mqtt_process_write()
- [x] Thread safety
  - [x] Mutex protection for client state
  - [x] Thread-safe callback invocation

---

## Phase 4: TLS Support ✅
**Goal**: Secure connections with pluggable TLS backends

- [x] TLS interface abstraction (src/transport/tls/)
- [x] OpenSSL backend
  - [x] SSL context management
  - [x] Certificate loading (CA, client cert, key)
  - [x] SNI support
  - [x] Hostname verification
- [ ] mbedTLS backend (stub only)
  - [ ] Configuration and setup
  - [ ] Certificate parsing
  - [ ] Entropy/RNG initialization
- [x] TLS transport integration
  - [x] Non-blocking handshake
  - [x] Graceful shutdown
- [x] mTLS (mutual TLS) support
- [x] ALPN protocol negotiation

---

## Phase 5: MQTT 5.0 Protocol
**Goal**: Complete MQTT 5.0 support alongside 3.1.1

- [ ] Properties system
  - [ ] Property encode/decode for all types
  - [ ] Property list management
  - [ ] User properties support
- [ ] Extended CONNECT/CONNACK
  - [ ] Session expiry interval
  - [ ] Receive maximum
  - [ ] Maximum packet size
  - [ ] Topic alias maximum
  - [ ] Request response information
  - [ ] Request problem information
  - [ ] Authentication method/data
- [ ] Extended PUBLISH
  - [ ] Payload format indicator
  - [ ] Message expiry interval
  - [ ] Topic alias
  - [ ] Response topic
  - [ ] Correlation data
  - [ ] Content type
- [ ] Extended SUBSCRIBE
  - [ ] Subscription identifier
  - [ ] No local option
  - [ ] Retain as published
  - [ ] Retain handling
- [ ] AUTH packet (enhanced authentication)
- [ ] Reason codes (all packets)
- [ ] DISCONNECT with reason code and properties
- [ ] Server redirection handling
- [ ] Protocol version negotiation/fallback

---

## Phase 6: WebSocket Transport
**Goal**: WebSocket (ws:// and wss://) support

- [ ] WebSocket protocol implementation
  - [ ] HTTP upgrade handshake
  - [ ] Frame encoding/decoding
  - [ ] Masking (client to server)
  - [ ] Control frames (ping/pong/close)
  - [ ] Continuation frames
- [ ] ws:// transport (plain WebSocket)
- [ ] wss:// transport (WebSocket over TLS)
- [ ] MQTT subprotocol negotiation ("mqtt", "mqttv5")
- [ ] Custom HTTP headers support
- [ ] Proxy support (optional)

---

## Phase 7: Platform Ports
**Goal**: Full cross-platform support

- [ ] Windows platform
  - [ ] Winsock2 socket operations
  - [ ] Win32 threads/synchronization
  - [ ] QueryPerformanceCounter time
  - [ ] IOCP I/O (optional)
- [ ] macOS/BSD platform
  - [ ] kqueue I/O multiplexing
  - [ ] Darwin-specific optimizations
- [ ] Linux platform
  - [ ] epoll I/O multiplexing
  - [ ] eventfd support
- [ ] Build system updates
  - [ ] Windows build (MSVC, MinGW)
  - [ ] macOS build (Xcode, clang)
  - [ ] Cross-compilation support

---

## Phase 8: Polish & Production Readiness
**Goal**: Production-quality 1.0 release

- [ ] Pool allocator enhancements
  - [ ] Multiple size classes
  - [ ] Statistics and debugging
  - [ ] Fragmentation mitigation
- [ ] Testing
  - [ ] Unit tests for all modules
  - [ ] Integration tests with real brokers
  - [ ] Fuzz testing for packet parsing
  - [ ] Memory leak testing (valgrind)
  - [ ] Thread safety testing (helgrind/TSAN)
- [ ] Documentation
  - [ ] API reference (Doxygen)
  - [ ] Usage guides
  - [ ] Porting guide
  - [ ] Examples for all features
- [ ] Build system polish
  - [ ] CMake package config
  - [ ] pkg-config support
  - [ ] Install targets
  - [ ] CPack packaging
- [ ] Performance
  - [ ] Benchmarks
  - [ ] Optimization pass
  - [ ] Zero-copy improvements

---

## Progress Summary

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | ✅ Complete | Foundation - MQTT 3.1.1 over TCP |
| 2 | ✅ Complete | QoS & Session Management |
| 3 | ✅ Complete | Async API & Event Loop |
| 4 | ✅ Complete | TLS Support (OpenSSL backend) |
| 5 | ⬜ Not started | MQTT 5.0 Protocol |
| 6 | ⬜ Not started | WebSocket Transport |
| 7 | ⬜ Not started | Platform Ports |
| 8 | ⬜ Not started | Polish & Production Readiness |
