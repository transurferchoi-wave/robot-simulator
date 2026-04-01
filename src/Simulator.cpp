#include "Simulator.h"
#include "RobotController.h"
#include "../third_party/json.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <signal.h>

using namespace std::chrono_literals;
using json = nlohmann::json;

// ── 생성자 ───────────────────────────────────────────────────
Simulator::Simulator()
    : tcpServer_(8765)
    , webServer_(8080)
{
    initGrid();
    initRobots();
}

Simulator::~Simulator() { shutdown(); }

void Simulator::initGrid() {
    // Grid 생성자에서 이미 장애물/스테이션 초기화됨
    std::cout << "[Simulator] 그리드 초기화 완료 ("
              << Grid::WIDTH << "x" << Grid::HEIGHT << ")\n";
    std::cout << grid_.toString() << "\n";
}

void Simulator::initRobots() {
    // 로봇 3대: 각 코너에서 시작
    std::vector<Point> startPositions = {{0,0}, {9,0}, {0,9}};

    for (int i = 0; i < 3; ++i) {
        auto robot = std::make_unique<Robot>(i, startPositions[i], grid_);
        robot->setStateCallback([](int id, RobotState s){
            std::cout << "[Robot-" << id << "] 상태: " << stateToString(s) << "\n";
        });
        robot->start();
        robots_.push_back(std::move(robot));
    }
    std::cout << "[Simulator] 로봇 " << robots_.size() << "대 초기화 완료\n";
}

// ── JSON 상태 생성 ────────────────────────────────────────────
std::string Simulator::getStateJson() const {
    json root = json::object();

    // 그리드 정보
    root["grid_w"] = Grid::WIDTH;
    root["grid_h"] = Grid::HEIGHT;

    // 장애물
    json obstacles = json::array();
    for (auto& p : grid_.getObstacles()) {
        json o = json::object();
        o["x"] = p.x; o["y"] = p.y;
        obstacles.push_back(o);
    }
    root["obstacles"] = obstacles;

    // 스테이션
    json stations = json::array();
    for (auto& p : grid_.getStations()) {
        json s = json::object();
        s["x"] = p.x; s["y"] = p.y;
        stations.push_back(s);
    }
    root["stations"] = stations;

    // 로봇 상태
    json robotsArr = json::array();
    for (const auto& robot : robots_) {
        json r = json::object();
        auto pos  = robot->getPosition();
        auto tgt  = robot->getTarget();
        auto path = robot->getPath();

        r["id"]       = robot->getId();
        r["name"]     = robot->getName();
        r["x"]        = pos.x;
        r["y"]        = pos.y;
        r["target_x"] = tgt.x;
        r["target_y"] = tgt.y;
        r["state"]    = stateToString(robot->getState());
        r["battery"]  = robot->getBattery();

        json pathArr = json::array();
        for (auto& p : path) {
            json pt = json::object();
            pt["x"] = p.x; pt["y"] = p.y;
            pathArr.push_back(pt);
        }
        r["path"] = pathArr;

        robotsArr.push_back(r);
    }
    root["robots"] = robotsArr;

    return root.dump();
}

// ── 명령 처리 ─────────────────────────────────────────────────
std::string Simulator::handleCommand(const std::string& jsonStr) {
    auto cmd = proto::parseCommand(jsonStr);

    if (cmd.type == "status") {
        return getStateJson();
    }

    if (cmd.type == "move") {
        if (cmd.robotId < 0 || cmd.robotId >= (int)robots_.size())
            return proto::errorResponse("유효하지 않은 robot_id");
        if (!grid_.inBounds(cmd.x, cmd.y))
            return proto::errorResponse("목표 좌표가 그리드 범위를 벗어남");
        if (!grid_.isWalkable(cmd.x, cmd.y))
            return proto::errorResponse("목표 위치가 장애물임");

        robots_[cmd.robotId]->moveTo({cmd.x, cmd.y});
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) +
            " 이동 명령: (" + std::to_string(cmd.x) + "," + std::to_string(cmd.y) + ")");
    }

    if (cmd.type == "stop") {
        if (cmd.robotId < 0 || cmd.robotId >= (int)robots_.size())
            return proto::errorResponse("유효하지 않은 robot_id");
        robots_[cmd.robotId]->stop();
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) + " 정지");
    }

    if (cmd.type == "reset") {
        if (cmd.robotId < 0 || cmd.robotId >= (int)robots_.size())
            return proto::errorResponse("유효하지 않은 robot_id");
        robots_[cmd.robotId]->reset();
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) + " 리셋");
    }

    return proto::errorResponse("알 수 없는 명령: " + cmd.type);
}

// ── 메인 루프 ─────────────────────────────────────────────────
void Simulator::run() {
    running_.store(true);

    // TCP 서버 시작
    tcpServer_.start([this](const std::string& msg) {
        return handleCommand(msg);
    });

    // 웹 서버 시작
    webServer_.start(
        [this](){ return getStateJson(); },
        [this](const std::string& body){ return handleCommand(body); }
    );

    std::cout << "\n========================================\n";
    std::cout << "  물류 로봇 시뮬레이터 실행 중\n";
    std::cout << "  웹 대시보드: http://localhost:8080\n";
    std::cout << "  TCP 제어:    localhost:8765\n";
    std::cout << "  Ctrl+C 로 종료\n";
    std::cout << "========================================\n\n";

    // 상태 출력 루프 (5초마다)
    int printCnt = 0;
    while (running_.load()) {
        std::this_thread::sleep_for(5s);
        if (++printCnt % 1 == 0) {
            printStatus();
        }
    }
}

void Simulator::shutdown() {
    if (!running_.load()) return;
    running_.store(false);
    std::cout << "\n[Simulator] 종료 중...\n";
    tcpServer_.stop();
    webServer_.stop();
    for (auto& r : robots_) r->shutdown();
    std::cout << "[Simulator] 정상 종료\n";
}

void Simulator::printStatus() const {
    std::cout << "\n─────────── 로봇 상태 ───────────\n";
    for (const auto& robot : robots_) {
        auto pos = robot->getPosition();
        auto tgt = robot->getTarget();
        std::cout << "  " << robot->getName()
                  << " | 위치:(" << pos.x << "," << pos.y << ")"
                  << " | 목표:(" << tgt.x << "," << tgt.y << ")"
                  << " | " << stateToString(robot->getState())
                  << " | 배터리:" << robot->getBattery() << "%\n";
    }
    std::cout << "──────────────────────────────────\n";
}
