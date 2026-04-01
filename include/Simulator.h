#pragma once
#include "Grid.h"
#include "Robot.h"
#include "TcpServer.h"
#include "WebServer.h"
#include <vector>
#include <memory>
#include <string>
#include <atomic>

/**
 * 시뮬레이터 최상위 클래스
 * - 그리드, 로봇들, 서버 통합 관리
 * - JSON 상태 직렬화
 * - 명령 디스패치
 */
class Simulator {
public:
    Simulator();
    ~Simulator();

    void run();
    void shutdown();

private:
    // 초기화
    void initGrid();
    void initRobots();

    // JSON 상태 생성
    std::string getStateJson() const;

    // 명령 처리 (TCP/HTTP 공통)
    std::string handleCommand(const std::string& json);

    // 콘솔 상태 출력
    void printStatus() const;

    Grid                           grid_;
    std::vector<std::unique_ptr<Robot>> robots_;
    TcpServer                      tcpServer_;
    WebServer                      webServer_;
    std::atomic<bool>              running_{false};
};
