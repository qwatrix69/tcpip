#pragma once

#include <arpa/inet.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <print>
#include <stdexcept>
#include <string>

// Fallback definitions for zero-copy (Linux 4.14+). Older headers may lack them.
#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif
#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

/// Create a TCP listening socket bound to port.
inline int create_server_socket(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind(): ") + std::strerror(errno));

    if (::listen(fd, 1) < 0)
        throw std::runtime_error(std::string("listen(): ") + std::strerror(errno));

    return fd;
}

/// Accept one connection from listening fd.
inline int accept_connection(int server_fd)
{
    sockaddr_in client{};
    socklen_t   len = sizeof(client);
    int fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0) throw std::runtime_error(std::string("accept(): ") + std::strerror(errno));
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client.sin_addr, buf, sizeof(buf));
    std::println("[Server] Connection from {}:{}", buf, ntohs(client.sin_port));
    return fd;
}

/// Connect to server, return connected socket fd.
inline int connect_to_server(const std::string& addr, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error(std::string("socket(): ") + std::strerror(errno));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) <= 0)
        throw std::runtime_error("Invalid address: " + addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0)
        throw std::runtime_error(std::string("connect(): ") + std::strerror(errno));

    return fd;
}

/// Apply TCP_NODELAY and match socket buffers to the transfer buffer size.
inline void configure_socket(int fd, std::size_t buf_size)
{
    int nodelay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // Set SO_SNDBUF / SO_RCVBUF to at least num_buffers * buffer_size
    // (capped by kernel max, usually /proc/sys/net/core/wmem_max)
    int sz = static_cast<int>(buf_size * 4);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
}

/// Enable zero-copy sending (SO_ZEROCOPY, requires Linux 4.14+).
/// Returns true if successfully enabled.
inline bool enable_zero_copy(int fd)
{
    int opt = 1;
    int rc = ::setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &opt, sizeof(opt));
    return rc == 0;
}

/// Drain the error queue after MSG_ZEROCOPY send to confirm kernel is done.
/// Must be called after each send() with MSG_ZEROCOPY to avoid buffer reuse.
inline void drain_zerocopy_completions(int fd)
{
    char control[256];
    msghdr msg{};
    msg.msg_control    = control;
    msg.msg_controllen = sizeof(control);
    // Non-blocking peek; real benchmark should poll properly
    ::recvmsg(fd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT);
}

/// Send exactly n bytes (handles partial sends).
/// Returns false if connection closed or error.
inline bool send_all(int fd, const void* buf, std::size_t n, int flags = 0)
{
    const auto* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t sent = ::send(fd, p, n, flags);
        if (sent <= 0) return false;
        p += sent;
        n -= static_cast<std::size_t>(sent);
    }
    return true;
}

/// Receive exactly n bytes (handles partial recvs).
/// Returns false if connection closed or error.
inline bool recv_all(int fd, void* buf, std::size_t n)
{
    auto* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t got = ::recv(fd, p, n, MSG_WAITALL);
        if (got <= 0) return false;
        p += got;
        n -= static_cast<std::size_t>(got);
    }
    return true;
}
