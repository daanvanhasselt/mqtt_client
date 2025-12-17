# Example programs
add_executable(simple_publish simple_publish.c)
target_link_libraries(simple_publish PRIVATE mqtt_client_static)

add_executable(simple_subscribe simple_subscribe.c)
target_link_libraries(simple_subscribe PRIVATE mqtt_client_static)

# TLS example (always build, but requires TLS to be enabled at runtime)
add_executable(tls_publish tls_publish.c)
target_link_libraries(tls_publish PRIVATE mqtt_client_static)
