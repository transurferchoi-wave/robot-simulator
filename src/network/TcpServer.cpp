#include "network/TcpServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

TcpServer::TcpServer(int port) : port_(port) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::start(MessageHandler handler) {
    handler_ = std::move(handler);

    serverFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) throw std::runtime_error("socket() 실패");

    int opt = 1;
    ::setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() 실패 (포트 " + std::to_string(port_) + ")");

    ::listen(serverFd_, 10);
    running_.store(true);

    std::cout << "[TcpServer] 포트 " << port_ << " 에서 수신 대기 중...\n";
    acceptThread_ = std::thread(&TcpServer::acceptLoop, this);
}

void TcpServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (serverFd_ >= 0) { ::close(serverFd_); serverFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
    // 클라이언트 스레드 정리
    std::lock_guard<std::mutex> lk(clientMutex_);
    for (auto& t : clientThreads_)
        if (t.joinable()) t.detach();
    clientThreads_.clear();
}

void TcpServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t   addrLen = sizeof(clientAddr);
        int clientFd = ::accept(serverFd_,
            reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (running_.load())
                std::cerr << "[TcpServer] accept() 오류\n";
            break;
        }
        std::cout << "[TcpServer] 클라이언트 연결: "
                  << inet_ntoa(clientAddr.sin_addr) << "\n";

        std::lock_guard<std::mutex> lk(clientMutex_);
        clientThreads_.emplace_back(&TcpServer::clientLoop, this, clientFd);
    }
}

void TcpServer::clientLoop(int clientFd) {
    char buf[4096];
    while (running_.load()) {
        ssize_t n = ::recv(clientFd, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        std::string msg(buf);
        std::string response;
        if (handler_) response = handler_(msg);
        else           response = R"({"ok":true})";

        response += "\n";
        ::send(clientFd, response.c_str(), response.size(), 0);
    }
    ::close(clientFd);
}
