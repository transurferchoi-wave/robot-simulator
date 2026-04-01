#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <map>

/**
 * 경량 HTTP 서버 (포트 8080)
 * GET /           → 대시보드 HTML 반환
 * GET /state      → 시뮬레이터 JSON 상태 반환
 * POST /command   → 로봇 명령 수신
 */
class WebServer {
public:
    using StateProvider  = std::function<std::string()>;
    using CommandHandler = std::function<std::string(const std::string&)>;

    explicit WebServer(int port = 8080);
    ~WebServer();

    void start(StateProvider stateFn, CommandHandler cmdFn);
    void stop();

private:
    void acceptLoop();
    void handleClient(int fd);

    std::string buildResponse(int code, const std::string& contentType,
                              const std::string& body);
    std::string parsePath(const std::string& request);
    std::string parseBody(const std::string& request);
    std::string parseMethod(const std::string& request);

    std::string getDashboardHtml();

    int              port_;
    int              serverFd_{-1};
    std::atomic<bool> running_{false};
    std::thread      acceptThread_;

    StateProvider   stateFn_;
    CommandHandler  cmdFn_;
};
