/*
 * gopher_server
 * =============
 *
 * An experimental gopher server. Simply reads requests from a tcp
 * socket and serves up the asked-for content. No frills.
 */

#include <array>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace gopher {
namespace {

/**
 * Wrapper around a TCP socket.
 */
class TcpSocket {
  std::string address_;
  short port_;
  std::optional<int> sock_;

  TcpSocket(int sock_fd, std::string address, short port)
      : address_(std::move(address)), port_(port), sock_(sock_fd) {}

public:
  /**
   * Construct a TcpSocket from an address/port pair.
   */
  TcpSocket(std::string address, short port)
      : address_(std::move(address)), port_(port),
        sock_(open_socket(address_, port)) {}

  ~TcpSocket() noexcept {
    if (sock_.has_value()) {
      ::close(*sock_);
    }
  }

  TcpSocket(const TcpSocket &) = delete;

  TcpSocket(TcpSocket &&other)
      : address_(std::move(other.address_)), port_(other.port_),
        sock_(std::exchange(other.sock_, std::nullopt)) {}

  /**
   * For server-side. Wait for a new connection to arrive, and
   * returns a new TcpSocket object for handling the new session.
   */
  std::optional<TcpSocket> connect() {
    assert(sock_.has_value());
    ::sockaddr_in address;
    ::socklen_t addr_len = sizeof(address);
    int client_fd =
        ::accept(*sock_, reinterpret_cast<sockaddr *>(&address), &addr_len);
    if (client_fd < 0) {
      return std::nullopt;
    }

    TcpSocket client(client_fd, inet_ntoa(address.sin_addr),
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
          "TcpSocket::open_socket: call to ::socket() failed");
    }

    if (int opt = 1; ::setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                                  sizeof(opt)) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "TcpSocket::open_socket: call to ::setsockopt() failed");
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // TODO: fixme
    address.sin_port = htons(port);

    if (::bind(sock_fd, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "TcpSocket::open_socket: call to ::bind() failed");
    }

    if (::listen(sock_fd, 3) < 0) {
      ::close(sock_fd);
      throw std::runtime_error(
          "TcpSocket::open_socket: call to ::listen() failed");
    }

    return sock_fd;
  }
};

static_assert(std::is_move_constructible_v<TcpSocket>);
static_assert(not std::is_copy_constructible_v<TcpSocket>);
static_assert(not std::is_copy_assignable_v<TcpSocket>);
static_assert(not std::is_move_assignable_v<TcpSocket>);

struct program_args {
  std::string address;
  int port;
  std::filesystem::path doc_root;
};

class Session {
  program_args args_;
  TcpSocket sock_;

public:
  Session(program_args args, TcpSocket sock)
      : args_(std::move(args)), sock_(std::move(sock)) {}

  /**
   * Handle a single request/response session.
   */
  void run() {
    try {
      std::array<char, 1024> buffer;
      auto bytes_read = sock_.read(std::as_writable_bytes(std::span(buffer)));
      if (not bytes_read.has_value()) {
        return;
      }
      std::string resource_str(buffer.data(), buffer.data() + *bytes_read);
      if (resource_str == "\r\n") {
        write_gophermap(args_.doc_root / "gophermap");
      } else {
        if (resource_str.ends_with("\r\n")) {
          resource_str = resource_str.substr(0, resource_str.size() - 2);
        }
        write_file(args_.doc_root / resource_str);
      }
    } catch (const std::exception &e) {
      std::clog << "do_session: " << e.what() << std::endl;
    }
  }

private:
  void write_error(std::string msg) {
    const std::string error_msg = "ERROR: " + msg;
    sock_.write(std::as_bytes(std::span(error_msg)));
  }

  void write_gophermap(const std::filesystem::path &map_file) {
    std::ifstream ifs(map_file);
    if (not ifs.is_open()) {
      return write_error("Resource not found");
    }

    std::string line;
    while (std::getline(ifs, line)) {
      switch (line[0]) {
      case '0':
      case 'h':
        line += "\t" + args_.address + "\t" + std::to_string(args_.port);
        break;
      case 'i':
        line += "\tnull.host\t1";
        break;
      default:
        break;
      }

      static const std::string crlf = "\r\n";
      line += crlf;
      sock_.write(std::as_bytes(std::span(line)));
    }

    std::string lastline = ".\r\n";
    sock_.write(std::as_bytes(std::span(lastline)));
  }

  bool is_within_doc_root(std::filesystem::path p) {
    bool good = false;
    auto canonical_file = std::filesystem::canonical(p);
    for (auto dir = canonical_file.parent_path();
         not std::filesystem::equivalent(dir, "/"); dir = dir.parent_path()) {
      if (std::filesystem::equivalent(dir, args_.doc_root)) {
        good = true;
        break;
      }
    }
    return good;
  }

  void write_file(std::filesystem::path file) {
    if (std::filesystem::is_regular_file(file)) {
      std::clog << "Serving regular file: " << file << std::endl;

      if (not is_within_doc_root(file)) {
        return write_error("Resource not found");
      }

      std::ifstream ifs(file);
      if (not ifs.is_open()) {
        return write_error("Resource not found");
      }

      std::array<char, 1024> out_buffer;
      while (ifs) {
        ifs.read(out_buffer.data(), out_buffer.size());
        const auto chars_read = ifs.gcount();
        sock_.write(
            std::as_bytes(std::span<char>(out_buffer.data(), chars_read)));
      }
    } else if (std::filesystem::is_directory(file)) {
      write_gophermap(file / "gophermap");
    } else {
      return write_error("Unknown resource");
    }
  }
};

int run(program_args args) {
  static std::atomic<bool> running = true;
  std::signal(SIGINT, [](int sig) {
    if (not running) {
      std::exit(EXIT_SUCCESS);
    }
    running = false;
  });

  TcpSocket socket(args.address, args.port);

  while (running) {
    auto session_sock = socket.connect();
    if (not session_sock) {
      continue;
    }

    // Hand the session off to a new thread to service it. One
    // thread per session should be fine for now.
    std::thread{[args, session_sock = std::move(session_sock)]() mutable {
      Session session(std::move(args), std::move(*session_sock));
      session.run();
    }}.detach();
  }

  return EXIT_SUCCESS;
}

} // namespace
} // namespace gopher

int main(int argc, char *argv[]) {
  try {
    if (argc != 4) {
      return EXIT_FAILURE;
    }

    const gopher::program_args args{
        .address = argv[1], .port = std::atoi(argv[2]), .doc_root = argv[3]};
    return gopher::run(args);
  } catch (const std::exception &e) {
    std::cerr << "Uncaught exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "Uncaught exception" << std::endl;
    return EXIT_FAILURE;
  }
}
