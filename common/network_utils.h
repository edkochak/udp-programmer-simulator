#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

#include "protocol.h"

class NetworkUtils {
   public:
    static int createUDPSocket() {
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            perror("Socket creation failed");
            return -1;
        }

        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        return sockfd;
    }

    static bool bindSocket(int sockfd, const std::string& ip, int port) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (ip.empty() || ip == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        }

        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Bind failed");
            return false;
        }

        return true;
    }

    static bool sendMessage(int sockfd, const Message& msg, const std::string& ip, int port) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        ssize_t sent =
            sendto(sockfd, &msg, sizeof(Message), 0, (struct sockaddr*)&addr, sizeof(addr));

        return sent == sizeof(Message);
    }

    static bool receiveMessage(int sockfd, Message& msg, std::string& from_ip, int& from_port) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t received =
            recvfrom(sockfd, &msg, sizeof(Message), 0, (struct sockaddr*)&from_addr, &from_len);

        if (received == sizeof(Message)) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            from_ip = ip_str;
            from_port = ntohs(from_addr.sin_port);
            return true;
        }

        return false;
    }

    static std::string getCurrentTime() {
        time_t now = time(nullptr);
        char* time_str = ctime(&now);
        std::string result(time_str);
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        return result;
    }

    static void printMessage(const std::string& prefix, const Message& msg) {
        std::cout << "[" << getCurrentTime() << "] " << prefix;

        switch (msg.type) {
            case REGISTER_PROGRAMMER:
                std::cout << "REGISTER_PROGRAMMER from client " << msg.client_id;
                break;
            case REGISTER_OBSERVER:
                std::cout << "REGISTER_OBSERVER from client " << msg.client_id;
                break;
            case SUBMIT_PROGRAM:
                std::cout << "SUBMIT_PROGRAM " << msg.program_id << " from " << msg.client_id
                          << " to " << msg.target_id;
                break;
            case REQUEST_REVIEW:
                std::cout << "REQUEST_REVIEW from " << msg.client_id;
                break;
            case REVIEW_RESULT:
                std::cout << "REVIEW_RESULT for program " << msg.program_id << " - "
                          << (msg.result == CORRECT ? "CORRECT" : "INCORRECT");
                break;
            case STATUS_UPDATE:
                std::cout << "STATUS_UPDATE for client " << msg.client_id;
                break;
            case DISCONNECT:
                std::cout << "DISCONNECT from client " << msg.client_id;
                break;
            case SHUTDOWN:
                std::cout << "SHUTDOWN command";
                break;
            case HEARTBEAT:
                std::cout << "HEARTBEAT from client " << msg.client_id;
                break;
            default:
                std::cout << "Unknown message type " << msg.type;
        }

        if (strlen(msg.data) > 0) {
            std::cout << " - " << msg.data;
        }

        std::cout << std::endl;
    }
};

#endif
