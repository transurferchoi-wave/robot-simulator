#pragma once
#include "Grid.h"
#include "Robot.h"
#include "ReservationTable.h"
#include "MessageBus.h"
#include "EventLogger.h"
#include "TcpServer.h"
#include "WebServer.h"
#include <vector>
#include <memory>
#include <string>
#include <atomic>

class Simulator {
public:
    Simulator();
    ~Simulator();

    void run();
    void shutdown();

private:
    void initGrid();
    void initRobots();

    std::string getStateJson()  const;
    std::string handleCommand(const std::string& json);

    void printStatus() const;

    // 공유 인프라
    Grid              grid_;
    ReservationTable  rt_;
    MessageBus        bus_;
    EventLogger       logger_;

    std::vector<std::unique_ptr<Robot>> robots_;
    TcpServer  tcpServer_;
    WebServer  webServer_;
    std::atomic<bool> running_{false};
};
