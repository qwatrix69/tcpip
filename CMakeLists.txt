cmake_minimum_required(VERSION 3.20)
project(tcpip_benchmark VERSION 1.0.0 LANGUAGES CXX)

# ── C++23 (matches Inflare) ──────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimisation flags for a fair comparison with the RDMA path
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# ── Common include directory ─────────────────────────────────────────────────
set(TCPIP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Server ───────────────────────────────────────────────────────────────────
add_executable(tcp_bench_server src/tcp_server.cpp)
target_include_directories(tcp_bench_server PRIVATE ${TCPIP_INCLUDE_DIR})
target_link_libraries(tcp_bench_server PRIVATE pthread)

# ── Client ───────────────────────────────────────────────────────────────────
add_executable(tcp_bench_client src/tcp_client.cpp)
target_include_directories(tcp_bench_client PRIVATE ${TCPIP_INCLUDE_DIR})
target_link_libraries(tcp_bench_client PRIVATE pthread)

# ── Multi-channel server (серия Д — аналог server chain) ─────────────────────
add_executable(tcp_bench_server_multi src/tcp_server_multi.cpp)
target_include_directories(tcp_bench_server_multi PRIVATE ${TCPIP_INCLUDE_DIR})
target_link_libraries(tcp_bench_server_multi PRIVATE pthread)

# ── Multi-channel client ─────────────────────────────────────────────────────
add_executable(tcp_bench_client_multi src/tcp_client_multi.cpp)
target_include_directories(tcp_bench_client_multi PRIVATE ${TCPIP_INCLUDE_DIR})
target_link_libraries(tcp_bench_client_multi PRIVATE pthread)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS tcp_bench_server tcp_bench_client
                tcp_bench_server_multi tcp_bench_client_multi
        RUNTIME DESTINATION bin)

message(STATUS "tcpip_benchmark: configured (C++23, ${CMAKE_BUILD_TYPE})")
