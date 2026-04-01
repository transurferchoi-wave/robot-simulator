#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

/**
 * 비동기 TCP 서버
 * 포트 8765에서 수신 대기
 * JSON 명령 수신 → 콜백 호출
 */
class TcpServer {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    explicit TcpServer(int port = 8765);
    ~TcpServer();

    void start(MessageHandler handler);
    void stop();

    bool isRunning() const { return running_.load(); }

private:
    void acceptLoop();
    void clientLoop(int clientFd);

    int              port_;
    int              serverFd_{-1};
    std::atomic<bool> running_{false};
    std::thread      acceptThread_;

    MessageHandler   handler_;

    // 클라이언트 스레드 목록
    std::mutex              clientMutex_;
    std::vector<std::thread> clientThreads_;
};
