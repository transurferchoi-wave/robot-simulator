#include "Simulator.h"
#include "RobotController.h"
#include "../third_party/json.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

using namespace std::chrono_literals;
using json = nlohmann::json;

Simulator::Simulator() : tcpServer_(8765), webServer_(8080) {
    initGrid();
    initRobots();
}

Simulator::~Simulator() { shutdown(); }

void Simulator::initGrid() {
    // 장애물 변경 시 모든 로봇에게 재계획 알림
    grid_.setObstacleCallback([this](Point p, bool added) {
        std::cout << "[Grid] 장애물 " << (added ? "추가" : "제거")
                  << ": (" << p.x << "," << p.y << ")\n";

        // 이벤트 기록
        logger_.log(added ? EventLogger::EventType::OBSTACLE_ADD
                           : EventLogger::EventType::OBSTACLE_REMOVE,
                    -1,
                    "{\"x\":" + std::to_string(p.x) +
                    ",\"y\":" + std::to_string(p.y) + "}");

        // MessageBus로 알림
        json j = json::object();
        j["x"]     = p.x;
        j["y"]     = p.y;
        j["added"] = added;
        bus_.publish(added ? "obstacle.added" : "obstacle.removed",
                     j.dump(), -1);

        // 모든 로봇 재계획
        for (auto& robot : robots_) {
            robot->notifyObstacleChanged();
        }
    });

    std::cout << "[Simulator] 그리드 초기화 완료\n";
    std::cout << grid_.toString() << "\n";
    std::cout << "범례: S=스테이션, C=충전소, #=장애물, .=빈 셀\n\n";
}

void Simulator::initRobots() {
    // 로봇 3대
    std::vector<Point> starts = {{0,0}, {9,0}, {0,9}};

    for (int i = 0; i < 3; ++i) {
        auto robot = std::make_unique<Robot>(i, starts[i], grid_, rt_, bus_, logger_);
        robot->setStateCallback([](int id, RobotState s){
            std::cout << "[Robot-" << id << "] → " << stateToString(s) << "\n";
        });
        robot->start();
        robots_.push_back(std::move(robot));
    }

    // 충돌 경고 구독 (로그용)
    bus_.subscribe("collision.warn", -1, [this](const MessageBus::Message& msg){
        std::cerr << "[!] 충돌 경고: " << msg.payload << "\n";
        logger_.log(EventLogger::EventType::COLLISION_WARN, -1, msg.payload);
    });

    // 미션 완료 구독
    bus_.subscribe("mission.complete", -1, [](const MessageBus::Message& msg){
        std::cout << "[✓] 미션 완료: " << msg.payload << "\n";
    });

    std::cout << "[Simulator] 로봇 " << robots_.size() << "대 초기화 완료\n";
}

// ── JSON 상태 생성 ─────────────────────────────────────────────
std::string Simulator::getStateJson() const {
    json root = json::object();
    root["grid_w"]  = Grid::WIDTH;
    root["grid_h"]  = Grid::HEIGHT;
    root["elapsed"] = (int64_t)logger_.elapsedMs();

    // 장애물
    json obs = json::array();
    for (auto& p : grid_.getObstacles()) {
        json o = json::object(); o["x"] = p.x; o["y"] = p.y;
        obs.push_back(o);
    }
    root["obstacles"] = obs;

    // 스테이션
    json sta = json::array();
    for (auto& p : grid_.getStations()) {
        json s = json::object(); s["x"] = p.x; s["y"] = p.y;
        sta.push_back(s);
    }
    root["stations"] = sta;

    // 충전소 (NEW)
    json chg = json::array();
    for (auto& p : grid_.getChargers()) {
        json c = json::object(); c["x"] = p.x; c["y"] = p.y;
        chg.push_back(c);
    }
    root["chargers"] = chg;

    // 로봇 상태
    json robotsArr = json::array();
    for (const auto& robot : robots_) {
        json r = json::object();
        auto pos  = robot->getPosition();
        auto tgt  = robot->getTarget();
        auto path = robot->getPath();

        r["id"]            = robot->getId();
        r["name"]          = robot->getName();
        r["x"]             = pos.x;
        r["y"]             = pos.y;
        r["target_x"]      = tgt.x;
        r["target_y"]      = tgt.y;
        r["state"]         = stateToString(robot->getState());
        r["battery"]       = robot->getBattery();
        r["mission_count"] = robot->getMissionCount();
        r["time_step"]     = robot->getTimeStep();

        // 현재 미션 정보
        auto curMission = robot->getCurrentMission();
        if (curMission.has_value()) {
            json m = json::object();
            m["id"]     = curMission->id;
            m["status"] = missionStatusToString(curMission->status);
            m["wp_idx"] = curMission->currentWpIdx;
            m["total_wp"] = (int)curMission->waypoints.size();
            // 웨이포인트 목록
            json wps = json::array();
            for (auto& wp : curMission->waypoints) {
                json w = json::object();
                w["x"]     = wp.pos.x;
                w["y"]     = wp.pos.y;
                w["label"] = wp.label;
                wps.push_back(w);
            }
            m["waypoints"] = wps;
            r["mission"] = m;
        } else {
            r["mission"] = nullptr;
        }

        // 경로
        json pathArr = json::array();
        for (auto& p : path) {
            json pt = json::object(); pt["x"] = p.x; pt["y"] = p.y;
            pathArr.push_back(pt);
        }
        r["path"] = pathArr;

        robotsArr.push_back(r);
    }
    root["robots"] = robotsArr;

    // 최근 메시지 버스 메시지 (NEW)
    json msgs = json::array();
    for (auto& entry : bus_.getRecentMessages(10)) {
        json m = json::object();
        m["t"]       = (int64_t)entry.timestampMs;
        m["topic"]   = entry.topic;
        m["payload"] = entry.payload;
        m["from"]    = entry.senderId;
        msgs.push_back(m);
    }
    root["bus_log"] = msgs;

    return root.dump();
}

// ── 명령 처리 ──────────────────────────────────────────────────
std::string Simulator::handleCommand(const std::string& jsonStr) {
    auto cmd = proto::parseCommand(jsonStr);

    // 상태 조회
    if (cmd.type == "status") return getStateJson();

    // 이벤트 로그 조회 (replay)
    if (cmd.type == "replay") {
        return logger_.getLogJson();
    }
    if (cmd.type == "replay_range") {
        return logger_.getReplayJson(cmd.fromMs, cmd.toMs);
    }
    if (cmd.type == "clear_log") {
        logger_.clear();
        return proto::successResponse("이벤트 로그 초기화");
    }

    // 동적 장애물 (NEW)
    if (cmd.type == "obstacle_add") {
        if (!grid_.inBounds(cmd.x, cmd.y))
            return proto::errorResponse("범위 밖 좌표");
        bool ok = grid_.addObstacle(cmd.x, cmd.y);
        return ok ? proto::successResponse("장애물 추가: (" +
                    std::to_string(cmd.x) + "," + std::to_string(cmd.y) + ")")
                  : proto::errorResponse("장애물 추가 실패");
    }
    if (cmd.type == "obstacle_remove") {
        bool ok = grid_.removeObstacle(cmd.x, cmd.y);
        return ok ? proto::successResponse("장애물 제거: (" +
                    std::to_string(cmd.x) + "," + std::to_string(cmd.y) + ")")
                  : proto::errorResponse("장애물 없음");
    }

    // 로봇 ID 유효성 검사
    auto validRobotId = [&]() -> bool {
        return cmd.robotId >= 0 && cmd.robotId < (int)robots_.size();
    };

    // 이동 명령
    if (cmd.type == "move") {
        if (!validRobotId()) return proto::errorResponse("유효하지 않은 robot_id");
        if (!grid_.inBounds(cmd.x, cmd.y)) return proto::errorResponse("범위 밖 좌표");
        if (!grid_.isWalkable(cmd.x, cmd.y)) return proto::errorResponse("장애물 위치");
        robots_[cmd.robotId]->moveTo({cmd.x, cmd.y});
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) +
            " 이동: (" + std::to_string(cmd.x) + "," + std::to_string(cmd.y) + ")");
    }

    // 미션 명령 (NEW)
    if (cmd.type == "mission") {
        if (!validRobotId()) return proto::errorResponse("유효하지 않은 robot_id");
        if (cmd.waypoints.empty()) return proto::errorResponse("웨이포인트 없음");

        Mission m;
        m.priority  = cmd.priority;
        m.waypoints = cmd.waypoints;

        robots_[cmd.robotId]->addMission(std::move(m));
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) +
            " 미션 추가: " + std::to_string(cmd.waypoints.size()) + " 웨이포인트");
    }

    if (cmd.type == "stop") {
        if (!validRobotId()) return proto::errorResponse("유효하지 않은 robot_id");
        robots_[cmd.robotId]->stop();
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) + " 정지");
    }

    if (cmd.type == "reset") {
        if (!validRobotId()) return proto::errorResponse("유효하지 않은 robot_id");
        robots_[cmd.robotId]->reset();
        return proto::successResponse("Robot-" + std::to_string(cmd.robotId) + " 리셋");
    }

    return proto::errorResponse("알 수 없는 명령: " + cmd.type);
}

// ── 메인 루프 ──────────────────────────────────────────────────
void Simulator::run() {
    running_.store(true);

    tcpServer_.start([this](const std::string& msg) {
        return handleCommand(msg);
    });
    webServer_.start(
        [this](){ return getStateJson(); },
        [this](const std::string& body){ return handleCommand(body); }
    );

    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout <<   "║   물류 로봇 시뮬레이터 v2.0 실행 중   ║\n";
    std::cout <<   "║  웹 대시보드: http://localhost:8080   ║\n";
    std::cout <<   "║  TCP 제어:    localhost:8765          ║\n";
    std::cout <<   "║  Ctrl+C 로 종료                       ║\n";
    std::cout <<   "╚════════════════════════════════════════╝\n\n";

    while (running_.load()) {
        std::this_thread::sleep_for(5s);
        printStatus();
    }
}

void Simulator::shutdown() {
    if (!running_.load()) return;
    running_.store(false);
    std::cout << "\n[Simulator] 종료 중...\n";
    tcpServer_.stop();
    webServer_.stop();
    for (auto& r : robots_) r->shutdown();
    rt_.clear();
    std::cout << "[Simulator] 정상 종료. 이벤트 수: " << logger_.count() << "\n";
}

void Simulator::printStatus() const {
    std::cout << "\n── 로봇 현황 ──────────────────────────────\n";
    for (const auto& robot : robots_) {
        auto pos = robot->getPosition();
        auto tgt = robot->getTarget();
        std::cout << "  " << robot->getName()
                  << " | (" << pos.x << "," << pos.y << ")"
                  << " → (" << tgt.x << "," << tgt.y << ")"
                  << " | " << stateToString(robot->getState())
                  << " | 배터리:" << robot->getBattery() << "%"
                  << " | 미션:" << robot->getMissionCount() << "\n";
    }
    std::cout << "  이벤트 누적: " << logger_.count()
              << " | 경과: " << logger_.elapsedMs()/1000 << "s\n";
    std::cout << "───────────────────────────────────────────\n";
}
