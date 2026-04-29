#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "utils/Logging.h"

#pragma comment(lib, "ws2_32.lib")

using namespace kvstore;

const int PORT = 6379;
const char* SERVER_IP = "127.0.0.1";
const int BUFFER_SIZE = 1024;

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Echo Client starting ===";

    // 初始化Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        LOG_ERROR << "WSAStartup failed: " << result;
        return 1;
    }

    // 创建客户端Socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        LOG_ERROR << "socket failed: " << WSAGetLastError();
        WSACleanup();
        return 1;
    }

    // 设置服务器地址
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    serverAddr.sin_port = htons(PORT);

    // 连接服务器
    LOG_INFO << "Connecting to server " << SERVER_IP << ":" << PORT;
    result = connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        LOG_ERROR << "connect failed: " << WSAGetLastError();
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    LOG_INFO << "Connected to server successfully!";
    LOG_INFO << "Type 'quit' to exit";

    std::string message;
    char buffer[BUFFER_SIZE];

    while (true) {
        // 从用户输入读取消息
        std::cout << "Enter message: ";
        std::getline(std::cin, message);

        if (message == "quit") {
            break;
        }

        // 发送消息到服务器
        result = send(clientSocket, message.c_str(), (int)message.length(), 0);
        if (result == SOCKET_ERROR) {
            LOG_ERROR << "send failed: " << WSAGetLastError();
            break;
        }
        LOG_INFO << "Sent: " << message;

        // 接收服务器回应
        result = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (result == SOCKET_ERROR) {
            LOG_ERROR << "recv failed: " << WSAGetLastError();
            break;
        } else if (result == 0) {
            LOG_INFO << "Server closed connection";
            break;
        }

        buffer[result] = '\0';
        LOG_INFO << "Received: " << buffer;
    }

    // 清理
    closesocket(clientSocket);
    WSACleanup();

    LOG_INFO << "=== Echo Client stopped ===";
    return 0;
}
