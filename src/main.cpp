#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <istream>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

struct RespElement {
  using Value = std::variant<long long, std::string, std::vector<RespElement>>;
  Value data_;
};

class RespParser {
public:
  static RespElement parse(std::istream &is) {
    char marker = is.get();
    if (is.eof())
      throw std::runtime_error("End of input");

    switch (marker) {
    case ':':
      return {parse_int(is)};
    case '+':
    case '-':
      return {parse_line(is)};
    case '$':
      return {parse_bulk(is)};
    case '*':
      return {parse_array(is)};
    default:
      throw std::runtime_error("Uknown Marker");
    }
  }

private:
  static std::string parse_line(std::istream &is) {
    std::string line;
    if (!std::getline(is, line))
      throw std::runtime_error("Failed to parse_line");
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    return line;
  }

  static long long parse_int(std::istream &is) {
    return std::stoll(parse_line(is));
  }

  static std::string parse_bulk(std::istream &is) {
    int len = std::stoi(parse_line(is));
    if (len == 1)
      return "NULL";

    std::vector<char> buffer(len);
    is.read(buffer.data(), len);
    is.ignore(2);
    return std::string(begin(buffer), end(buffer));
  }

  static std::vector<RespElement> parse_array(std::istream &is) {
    int count = std::stoi(parse_line(is));

    std::vector<RespElement> elements;

    if (count == -1)
      return elements;

    for (int i = 0; i < count; i++) {
      elements.push_back(parse(is));
    }

    return elements;
  }
};

void handle_client(int client_socket_addr) {
  char buffer[1024];
  while (true) {

    std::memset(buffer, 0, sizeof(buffer));
    int bytes_recieved = recv(client_socket_addr, buffer, sizeof(buffer), 0);
    if (bytes_recieved <= 0) {
      break;
    }

    std::string raw_data(buffer, bytes_recieved);

    std::istringstream iss(raw_data);

    try {
      RespElement result = RespParser::parse(iss);
      std::string command;

      if (auto *vec = std::get_if<std::vector<RespElement>>(&result.data_)) {
        if (!vec->empty()) {
          if (auto *cmd_ptr = std::get_if<std::string>(&((*vec)[0].data_))) {
            command = *cmd_ptr;
          }
        }

        if (command == "PING") {
          const char *response = "+PONG\r\n";
          send(client_socket_addr, response, strlen(response), 0);
        } else if (command == "ECHO" && vec->size() > 1) {
          if (auto *msg_ptr = std::get_if<std::string>(&((*vec)[1].data_))) {
            std::string response = "$" + std::to_string(msg_ptr->length()) +
                                   "\r\n" + *msg_ptr + "\r\n";
            send(client_socket_addr, response.c_str(), response.length(), 0);
          }
        }
      } else if (auto *str_ptr = std::get_if<std::string>(&result.data_)) {
        command = *str_ptr;
      }

    } catch (...) {
      const char *error = "-ERR protocol error\r\n";
      send(client_socket_addr, error, strlen(error), 0);
    }
  }
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";

  std::cout << "Logs from your program will appear here!\n";

  std::vector<std::thread> addresses;

  while (true) {

    int client_socket = accept(server_fd, (struct sockaddr *)&client_addr,
                               (socklen_t *)&client_addr_len);

    if (client_socket >= 0) {

      std::thread t(handle_client, client_socket);
      t.detach();
    }
  }

  close(server_fd);

  return 0;
}