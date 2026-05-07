#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <netdb.h>
#include <ostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

std::map<std::string, std::string> gStorage;

std::string respParser(std::string s) {
  std::vector<std::string> out;
  std::istringstream iss(std::move(s));
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    out.push_back(line);
  }

  if (out.size() < 4) {
    return "+PONG\r\n";
  }

  std::string command = out[2];

  for (auto &x : command) {
    x = std::tolower(x);
  }

  std::cout << command;

  std::ostringstream response;
  if (command == "echo") {
    response << out[3] << "\r\n" << out[4] << "\r\n";
    return response.str();
  } else if (command == "set") {
    gStorage[out[4]] = out[6];
    return "+OK\r\n";
  } else if (command == "get") {
    response << "$" << gStorage.at(out[4]).length() << "\r\n"
             << gStorage.at(out[4]) << "\r\n";
    return response.str();
  }
  return command;
}

int createListeningSocket(uint16_t port) {
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
  server_addr.sin_port = htons(port);

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

  return server_fd;
}

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd{createListeningSocket(6379)};
  std::vector<pollfd> fds;

  fds.push_back(pollfd{server_fd, POLLIN, 0});
  for (;;) {
    int rc = poll(fds.data(), fds.size(), -1);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "poll: " << std::strerror(errno) << "\n";
      break;
    }

    if (fds[0].revents & POLLIN) {
      int client = accept(server_fd, nullptr, nullptr);
      if (client >= 0) {
        fds.push_back(pollfd{client, POLLIN, 0});
        std::cout << "client fd " << client << " connected\n";
      }
    }

    for (size_t i = 1; i < fds.size(); ++i) {
      if (fds[i].fd == -1)
        continue;

      if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
        close(fds[i].fd);
        fds[i].fd = -1;
        continue;
      }

      if (fds[i].revents & POLLIN) {
        char buf[4096];
        ssize_t n = read(fds[i].fd, buf, sizeof(buf));
        const std::string pong = respParser(buf);
        if (n == 0) {
          std::cout << "client fd " << fds[i].fd << " closed\n";
          close(fds[i].fd);
          fds[i].fd = -1;
        } else if (n > 0) {
          send(fds[i].fd, pong.c_str(), pong.length(), 0);
        }
      }
    }
  }
  close(server_fd);

  return 0;
}