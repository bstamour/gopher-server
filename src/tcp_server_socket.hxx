#ifndef BST_TCP_SERVER_SOCKET_HXX_
#define BST_TCP_SERVER_SOCKET_HXX_

#include <cassert>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gopher {

/**
 * Wrapper around a TCP socket.
 */
class tcp_server_socket {
  std::string address_;
  short port_;
  std::optional<int> sock_;

  tcp_server_socket(int sock_fd, std::string address, short port)
      : address_(std::move(address)), port_(port), sock_(sock_fd) {}

public:
  /**
   * Construct a tcp_server_socket from an address/port pair.
   */
  tcp_server_socket(std::string address, short port)
      : address_(std::move(address)), port_(port),
        sock_(open_socket(address_, port)) {}

  ~tcp_server_socket() noexcept {
    if (sock_.has_value()) {
      ::close(*sock_);
    }
  }

  tcp_server_socket(const tcp_server_socket &) = delete;

  tcp_server_socket(tcp_server_socket &&other)
      : address_(std::move(other.address_)), port_(other.port_),
        sock_(std::move(other.sock_)) {}

  /**
   * For server-side. Wait for a new connection to arrive, and
   * returns a new tcp_server_socket object for handling the new session.
   */
  std::optional<tcp_server_socket> connect() {
    assert(sock_.has_value());
    ::sockaddr_in address;
    ::socklen_t addr_len = sizeof(address);
    int client_fd =
        ::accept(*sock_, reinterpret_cast<sockaddr *>(&address), &addr_len);
    if (client_fd < 0) {
      return std::nullopt;
    }

    tcp_server_socket client(client_fd, inet_ntoa(address.sin_addr),
                             ntohs(address.sin_port));
    return client;
  }

  /**
   * Read data from the socket.
   */
  template <std::size_t N>
  std::optional<std::size_t> read(std::span<std::byte, N> buffer) {
    assert(sock_.has_value());
    const ssize_t bytes_read = ::recv(*sock_, buffer.data(), buffer.size(), 0);
    if (bytes_read < 0) {
      // Read error.
      return std::nullopt;
    }

    if (bytes_read == 0) {
      // No data.
      return std::nullopt;
    }

    // otherwise we're good.
    return bytes_read;
  }

  /**
   * Write data to the socket.
   */
  template <std::size_t N>
  std::optional<std::size_t> write(std::span<const std::byte, N> buffer) {
    assert(sock_.has_value());
    const ssize_t sent = ::send(*sock_, buffer.data(), buffer.size(), 0);
    if (sent < 0) {
      return std::nullopt;
    }
    return sent;
  }

private:
  static int open_socket(const std::string &addr, short port) {
    ::sockaddr_in address;

    int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
      throw std::runtime_error(
          "tcp_server_socket::open_socket: call to ::socket() failed");
    }

    if (int opt = 1; ::setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                                  sizeof(opt)) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "tcp_server_socket::open_socket: call to ::setsockopt() failed");
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // TODO: fixme
    address.sin_port = htons(port);

    if (::bind(sock_fd, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "tcp_server_socket::open_socket: call to ::bind() failed");
    }

    if (::listen(sock_fd, 3) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "tcp_server_socket::open_socket: call to ::listen() failed");
    }

    return sock_fd;
  }
};

static_assert(std::is_move_constructible_v<tcp_server_socket>);
static_assert(not std::is_copy_constructible_v<tcp_server_socket>);
static_assert(not std::is_copy_assignable_v<tcp_server_socket>);
static_assert(not std::is_move_assignable_v<tcp_server_socket>);

} // namespace gopher

#endif
