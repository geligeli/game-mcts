#ifndef GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_HTTP_SERVER_H
#define GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_HTTP_SERVER_H

#include <functional>
#include <map>
#include <string>

namespace visualisations {

// Simple HTTP server that serves HTML content
class Server {
 public:
  // Constructor takes a port number
  explicit Server(int port);

  // Destructor closes the socket
  ~Server();

  // Serve a single HTML response and then stop
  // content_generator is called to produce the HTML when a request arrives
  void serve_once(std::function<std::string()> content_generator);

  // Serve HTML content continuously until interrupted (Ctrl+C)
  // content_generator is called for each request
  void serve(std::function<std::string()> content_generator);

  // Get the URL where the server is accessible
  std::string get_url() const;

 private:
  struct ClientState {
    std::string request_buffer;
    bool request_complete = false;
  };

  int port_;
  int server_fd_;
  std::map<int, ClientState> clients_;

  // Initialize the server socket
  void initialize_socket();

  // Accept a new client connection
  void accept_client();

  // Handle data from a client
  void handle_client_data(int client_fd,
                          std::function<std::string()> content_generator);

  // Send HTTP response
  void send_response(int client_fd, const std::string& content);

  // Close a client connection
  void close_client(int client_fd);
};

}  // namespace visualisations

#endif  // GAME_MCTS_GAME_MCTS_CPP_VISUALISATIONS_HTTP_SERVER_H
