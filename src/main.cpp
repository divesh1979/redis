#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

std::unordered_map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> kv_store;
std::mutex kv_mutex;

std::vector<std::string> parse_redis_command(int client_fd) {
    std::vector<std::string> result;
    char buffer[1024];
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) return result;

    buffer[bytes_read] = '\0';
    std::string input(buffer);
    size_t pos = 0;

    if (input[pos] != '*') return result;

    pos++;
    int num_elems = std::stoi(input.substr(pos));
    pos = input.find("\r\n", pos) + 2;

    for (int i = 0; i < num_elems; ++i) {
        if (input[pos] != '$') return result;
        pos++;
        int len = std::stoi(input.substr(pos));
        pos = input.find("\r\n", pos) + 2;
        result.push_back(input.substr(pos, len));
        pos += len + 2; // Skip \r\n
    }

    return result;
}

void handle_client(int client_fd) {
    while (true) {
        std::vector<std::string> parts = parse_redis_command(client_fd);
        if (parts.empty()) break;

        std::string command = parts[0];
        for (char &c : command) c = toupper(c);

        if (command == "PING") {
            std::string response = "+PONG\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        } else if (command == "ECHO" && parts.size() == 2) {
            std::string msg = parts[1];
            std::string response = "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        } else if (command == "SET" && parts.size() >= 3) {
            std::string key = parts[1];
            std::string value = parts[2];
            std::chrono::steady_clock::time_point expiry_time = std::chrono::steady_clock::time_point::max();

            if (parts.size() >= 5 && strcasecmp(parts[3].c_str(), "px") == 0) {
                int expiry_ms = std::stoi(parts[4]);
                expiry_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(expiry_ms);
            }

            {
                std::lock_guard<std::mutex> lock(kv_mutex);
                kv_store[key] = {value, expiry_time};
            }

            std::string response = "+OK\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        } else if (command == "GET" && parts.size() == 2) {
            std::string key = parts[1];
            std::string response;

            {
                std::lock_guard<std::mutex> lock(kv_mutex);
                auto it = kv_store.find(key);
                if (it != kv_store.end()) {
                    auto [value, expiry_time] = it->second;
                    if (std::chrono::steady_clock::now() >= expiry_time) {
                        kv_store.erase(it);
                        response = "$-1\r\n";
                    } else {
                        response = "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
                    }
                } else {
                    response = "$-1\r\n";
                }
            }

            send(client_fd, response.c_str(), response.size(), 0);
        } else {
            std::string response = "-ERR unknown command\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
        }
    }

    close(client_fd);
}

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    std::cout << "Server is running on port 6379...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_len);
        if (client_fd < 0) {
            std::cerr << "Failed to accept client\n";
            continue;
        }
        std::thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}