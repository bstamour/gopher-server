/*
 * gopher_server
 * =============
 *
 * An experimental gopher server. Simply reads requests from a tcp
 * socket and serves up the asked-for content. No frills.
 */

#include "tcp_server_socket.hxx"

#include <array>
#include <atomic>
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

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace gopher {
namespace {

void drop_privileges(const std::string &username) {
  ::passwd *pw = ::getpwnam(username.c_str());
  if (!pw) {
    throw std::runtime_error("getpwnam failed");
  }
  if (::initgroups(pw->pw_name, pw->pw_gid) != 0) {
    throw std::runtime_error("initgroups failed");
  }
  if (::setgid(pw->pw_gid) != 0) {
    throw std::runtime_error("setgid failed");
  }
  if (::setuid(pw->pw_uid) != 0) {
    throw std::runtime_error("setuid failed");
  }
  if (::setuid(0) == 0 || ::geteuid() != pw->pw_uid) {
    throw std::runtime_error("Failed to permanently drop privileges");
  }
}

struct program_args {
  std::string address;
  int port;
  std::filesystem::path doc_root;
  std::string user;
};

class session {
  program_args args_;
  tcp_server_socket sock_;

public:
  session(program_args args, tcp_server_socket sock)
      : args_(std::move(args)), sock_(std::move(sock)) {}

  session(session &&) = delete;

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
        write_resource(args_.doc_root / resource_str);
      }
    } catch (const std::exception &e) {
      std::clog << "do_session: " << e.what() << std::endl;
    }
  }

private:
  void write_error(std::string msg) {
    const std::string error_msg = "ERROR: " + msg + "\n";
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

      line += "\r\n";
      sock_.write(std::as_bytes(std::span(line)));
    }

    std::string lastline = ".\r\n";
    sock_.write(std::as_bytes(std::span(lastline)));
  }

  bool is_within_doc_root(std::filesystem::path p) {
    bool good = false;
    p = std::filesystem::canonical(p);
    for (auto dir = p.parent_path(); not std::filesystem::equivalent(dir, "/");
         dir = dir.parent_path()) {
      if (std::filesystem::equivalent(dir, args_.doc_root)) {
        good = true;
        break;
      }
    }
    return good;
  }

  void write_resource(const std::filesystem::path &file) {
    if (not is_within_doc_root(file)) {
      return write_error("Resource not found");
    }

    if (std::filesystem::is_regular_file(file)) {
      std::clog << "Serving regular file: " << file << std::endl;

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
      return write_error("Unknown resource type");
    }
  }
};

static_assert(not std::is_move_constructible_v<session>);
static_assert(not std::is_copy_constructible_v<session>);
static_assert(not std::is_copy_assignable_v<session>);
static_assert(not std::is_move_assignable_v<session>);

int run(program_args args) {
  static std::atomic<bool> running = true;
  std::signal(SIGINT, [](int sig) {
    if (not running) {
      std::exit(EXIT_SUCCESS);
    }
    running = false;
  });

  tcp_server_socket socket(args.address, args.port);

  if (::geteuid() == 0) {
    drop_privileges(args.user);
  }

  while (running) {
    auto session_sock = socket.connect();
    if (not session_sock) {
      continue;
    }

    // Hand the session off to a new thread to service it. One
    // thread per session should be fine for now.
    std::thread{[args, session_sock = std::move(session_sock)]() mutable {
      session session(std::move(args), std::move(*session_sock));
      session.run();
    }}.detach();
  }

  return EXIT_SUCCESS;
}

} // namespace
} // namespace gopher

int main(int argc, char *argv[]) {
  try {
    if (argc != 5) {
      std::cerr << "Usage: " << argv[0] << " hostname port doc-root user\n";
      return EXIT_FAILURE;
    }

    const gopher::program_args args{.address = argv[1],
                                    .port = std::atoi(argv[2]),
                                    .doc_root = argv[3],
                                    .user = argv[4]};
    return gopher::run(args);
  } catch (const std::exception &e) {
    std::cerr << "Uncaught exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "Uncaught exception" << std::endl;
    return EXIT_FAILURE;
  }
}
