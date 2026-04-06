#pragma once
#include "core/Grid.h"
#include "core/Robot.h"
#include "planning/ReservationTable.h"
#include "messaging/MessageBus.h"
#include "messaging/EventLogger.h"
#include "network/TcpServer.h"
#include "network/WebServer.h"
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
