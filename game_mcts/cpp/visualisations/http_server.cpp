#include "game_mcts/cpp/visualisations/http_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "absl/log/log.h"

namespace visualisations {

Server::Server(int port) : port_(port), server_fd_(-1) { initialize_socket(); }

Server::~Server() {
  // Close all client connections
  for (const auto &[fd, _] : clients_) {
    close(fd);
  }
  clients_.clear();

  if (server_fd_ >= 0) {
    close(server_fd_);
  }
}

void Server::initialize_socket() {
  // Create socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    throw std::runtime_error("Failed to create socket");
  }

  // Set socket options to reuse address
  int opt = 1;
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close(server_fd_);
    throw std::runtime_error("Failed to set socket options");
  }

  // Bind socket to port
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
    close(server_fd_);
    throw std::runtime_error("Failed to bind to port " + std::to_string(port_));
  }

  // Listen for connections
  if (listen(server_fd_, 10) < 0) {
    close(server_fd_);
    throw std::runtime_error("Failed to listen on socket");
  }

  // Set server socket to non-blocking mode
  int flags = fcntl(server_fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(server_fd_);
    throw std::runtime_error("Failed to set socket to non-blocking mode");
  }
}

void Server::accept_client() {
  struct sockaddr_in client_address;
  socklen_t client_len = sizeof(client_address);

  int client_fd =
      accept(server_fd_, (struct sockaddr *)&client_address, &client_len);
  if (client_fd < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      LOG(ERROR) << "Failed to accept connection: " << strerror(errno);
    }
    return;
  }

  // Set client socket to non-blocking mode
  int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG(WARNING) << "Failed to set client socket to non-blocking mode";
    close(client_fd);
    return;
  }

  // Set socket timeout
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)) < 0) {
    LOG(WARNING) << "Failed to set socket timeout";
  }

  clients_[client_fd] = ClientState();
}

void Server::handle_client_data(
    int client_fd, std::function<std::string()> content_generator) {
  auto it = clients_.find(client_fd);
  if (it == clients_.end()) {
    return;
  }

  ClientState &state = it->second;
  if (state.request_complete) {
    return;
  }

  constexpr std::size_t kMaxRequestSize = 8192;
  const std::string header_terminator = "\r\n\r\n";
  char buffer[512];

  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
  if (bytes_read <= 0) {
    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      LOG(INFO) << "Client disconnected or error (fd=" << client_fd << ")";
      close_client(client_fd);
    }
    return;
  }

  state.request_buffer.append(buffer, buffer + bytes_read);

  // Check if we have a complete request
  if (state.request_buffer.find(header_terminator) != std::string::npos) {
    state.request_complete = true;

    auto end_of_line = state.request_buffer.find("\r\n");
    if (end_of_line != std::string::npos) {
      std::string request_line = state.request_buffer.substr(0, end_of_line);
      if (request_line.rfind("GET ", 0) == 0) {
        std::string html_content = content_generator();
        send_response(client_fd, html_content);
      }
    }
    close_client(client_fd);
  } else if (state.request_buffer.size() >= kMaxRequestSize) {
    LOG(WARNING) << "Request too large from client (fd=" << client_fd << ")";
    close_client(client_fd);
  }
}

void Server::send_response(int client_fd, const std::string &content) {
  std::ostringstream response;
  response << "HTTP/1.1 200 OK\r\n";
  response << "Content-Type: text/html; charset=utf-8\r\n";
  response << "Content-Length: " << content.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "\r\n";
  response << content;

  std::string response_str = response.str();
  send(client_fd, response_str.c_str(), response_str.size(), 0);
}

void Server::close_client(int client_fd) {
  clients_.erase(client_fd);
  close(client_fd);
}

void Server::serve_once(std::function<std::string()> content_generator) {
  std::cout << "Server listening on " << get_url() << std::endl;
  std::cout << "Waiting for connection..." << std::endl;

  fd_set read_fds;
  int max_fd = server_fd_;

  while (clients_.empty() ||
         std::none_of(clients_.begin(), clients_.end(), [](const auto &p) {
           return p.second.request_complete;
         })) {
    FD_ZERO(&read_fds);
    FD_SET(server_fd_, &read_fds);

    for (const auto &[fd, _] : clients_) {
      FD_SET(fd, &read_fds);
      max_fd = std::max(max_fd, fd);
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (activity < 0) {
      LOG(ERROR) << "select() error: " << strerror(errno);
      break;
    }

    if (activity == 0) {
      continue;  // Timeout, check loop condition
    }

    // Check for new connections
    if (FD_ISSET(server_fd_, &read_fds)) {
      accept_client();
      if (activity == 1 && clients_.size() == 1) {
        break;  // We got our first connection, serve it next iteration
      }
    }

    // Check for client data
    std::vector<int> client_fds;
    for (const auto &[fd, _] : clients_) {
      client_fds.push_back(fd);
    }

    for (int client_fd : client_fds) {
      if (FD_ISSET(client_fd, &read_fds)) {
        handle_client_data(client_fd, content_generator);
      }
    }
  }

  std::cout << "Served one request. Server stopping." << std::endl;
}

void Server::serve(std::function<std::string()> content_generator) {
  LOG(INFO) << "Server listening on " << get_url();
  LOG(INFO) << "Press Ctrl+C to stop.";

  fd_set read_fds;
  int max_fd;

  while (true) {
    FD_ZERO(&read_fds);
    FD_SET(server_fd_, &read_fds);
    max_fd = server_fd_;

    // Add all client sockets to the set
    for (const auto &[fd, _] : clients_) {
      FD_SET(fd, &read_fds);
      max_fd = std::max(max_fd, fd);
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (activity < 0) {
      LOG(ERROR) << "select() error: " << strerror(errno);
      break;
    }

    if (activity == 0) {
      continue;  // Timeout, loop again
    }

    // Check for new incoming connections
    if (FD_ISSET(server_fd_, &read_fds)) {
      accept_client();
    }

    // Check all client sockets for incoming data
    // Make a copy of the keys to avoid iterator invalidation
    std::vector<int> client_fds;
    for (const auto &[fd, _] : clients_) {
      client_fds.push_back(fd);
    }

    for (int client_fd : client_fds) {
      if (FD_ISSET(client_fd, &read_fds)) {
        handle_client_data(client_fd, content_generator);
      }
    }
  }
}
auto Server::get_url() const -> std::string {
  std::string host = "localhost";

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock >= 0) {
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) ==
        0) {
      sockaddr_in local{};
      socklen_t len = sizeof(local);
      if (getsockname(sock, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
        char addr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &local.sin_addr, addr, sizeof(addr))) {
          host = addr;
        }
      }
    }
    close(sock);
  }

  return "http://" + host + ":" + std::to_string(port_);
}

}  // namespace visualisations
