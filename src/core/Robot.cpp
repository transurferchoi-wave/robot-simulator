#include "core/Robot.h"
#include "planning/Pathfinder.h"
#include "third_party/json.hpp"
#include <chrono>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
using json = nlohmann::json;

// ── 상태 문자열 ──────────────────────────────────────────────
std::string stateToString(RobotState s) {
    switch (s) {
        case RobotState::IDLE:            return "IDLE";
        case RobotState::PLANNING:        return "PLANNING";
        case RobotState::MOVING:          return "MOVING";
        case RobotState::ARRIVED:         return "ARRIVED";
        case RobotState::CHARGING_TRAVEL: return "CHARGING_TRAVEL";
        case RobotState::CHARGING:        return "CHARGING";
        case RobotState::ERROR:           return "ERROR";
        default:                          return "UNKNOWN";
    }
}

// ── 생성자/소멸자 ────────────────────────────────────────────
Robot::Robot(int id, Point startPos, Grid& grid,
             ReservationTable& rt, MessageBus& bus, EventLogger& logger)
    : id_(id)
    , name_("Robot-" + std::to_string(id))
    , grid_(grid)
    , rt_(rt)
    , bus_(bus)
    , logger_(logger)
    , position_(startPos)
    , target_(startPos)
{}

Robot::~Robot() { shutdown(); }

// ── 스레드 시작/종료 ─────────────────────────────────────────
void Robot::start() {
    running_.store(true);
    sensorTh_  = std::thread(&Robot::sensorThread,  this);
    plannerTh_ = std::thread(&Robot::plannerThread, this);
    controlTh_ = std::thread(&Robot::controlThread, this);

    // 위치 변경 알림 구독 (다른 로봇 감시)
    bus_.subscribe("robot.position", id_, [this](const MessageBus::Message& msg){
        if (msg.senderId == id_) return;
        // 다른 로봇의 위치가 내 현재 경로 위에 있으면 재계획
        std::lock_guard<std::mutex> lk(posMutex_);
        if (state_ == RobotState::MOVING && !currentPath_.empty()) {
            try {
                auto j = json::parse(msg.payload);
                Point other{j["x"].get<int>(), j["y"].get<int>()};
                // 경로 앞 3칸 내에 있으면 경고 발행
                for (int i = 0; i < std::min(3, (int)currentPath_.size()); ++i) {
                    if (currentPath_[i] == other) {
                        // 충돌 경고
                        json warn = json::object();
                        warn["robot_a"]  = id_;
                        warn["robot_b"]  = msg.senderId;
                        warn["x"]        = other.x;
                        warn["y"]        = other.y;
                        bus_.publish("collision.warn", warn.dump(), id_);
                        break;
                    }
                }
            } catch (...) {}
        }
    });

    logEvent(EventLogger::EventType::SYSTEM,
             "{\"msg\":\"Robot started\",\"pos\":{\"x\":" +
             std::to_string(position_.x) + ",\"y\":" + std::to_string(position_.y) + "}}");
}

void Robot::shutdown() {
    if (!running_.load()) return;
    running_.store(false);
    bus_.unsubscribe("robot.position", id_);
    planReqCV_.notify_all();
    pathCV_.notify_all();
    arrivalCV_.notify_all();
    if (sensorTh_.joinable())  sensorTh_.join();
    if (plannerTh_.joinable()) plannerTh_.join();
    if (controlTh_.joinable()) controlTh_.join();
}

// ── 외부 명령 ────────────────────────────────────────────────
void Robot::moveTo(Point target) {
    Mission m;
    m.id       = nextMissionId_++;
    m.priority = 5;
    m.waypoints.push_back({target, "GOTO"});
    addMission(std::move(m));
}

void Robot::addMission(Mission mission) {
    {
        std::lock_guard<std::mutex> lk(missionMutex_);
        // 우선순위가 높은 미션은 앞에 삽입
        auto it = missionQueue_.begin();
        while (it != missionQueue_.end() && it->priority >= mission.priority)
            ++it;
        missionQueue_.insert(it, std::move(mission));
    }
    requestPlan(PlanRequest::PLAN_NEXT);
}

void Robot::clearMissions() {
    {
        std::lock_guard<std::mutex> lk(missionMutex_);
        missionQueue_.clear();
        currentMission_.reset();
    }
    requestPlan(PlanRequest::STOP);
}

void Robot::stop() {
    {
        std::lock_guard<std::mutex> lk(missionMutex_);
        missionQueue_.clear();
        currentMission_.reset();
        chargeMissionActive_ = false;
    }
    requestPlan(PlanRequest::STOP);
}

void Robot::reset() {
    stop();
    battery_.store(CHARGE_FULL);
    {
        std::lock_guard<std::mutex> lk(posMutex_);
        timeStep_.store(0);
    }
    rt_.clearReservations(id_);
    requestPlan(PlanRequest::RESET);
}

void Robot::notifyObstacleChanged() {
    RobotState cur = getState();
    if (cur == RobotState::MOVING || cur == RobotState::CHARGING_TRAVEL) {
        requestPlan(PlanRequest::REPLAN);
    }
}

// ── 상태 조회 ────────────────────────────────────────────────
Point Robot::getPosition() const {
    std::lock_guard<std::mutex> lk(posMutex_);
    return position_;
}
Point Robot::getTarget() const {
    std::lock_guard<std::mutex> lk(posMutex_);
    return target_;
}
RobotState Robot::getState() const {
    std::lock_guard<std::mutex> lk(posMutex_);
    return state_;
}
std::vector<Point> Robot::getPath() const {
    std::lock_guard<std::mutex> lk(posMutex_);
    return currentPath_;
}
int Robot::getMissionCount() const {
    std::lock_guard<std::mutex> lk(missionMutex_);
    return (int)missionQueue_.size() + (currentMission_.has_value() ? 1 : 0);
}
std::optional<Mission> Robot::getCurrentMission() const {
    std::lock_guard<std::mutex> lk(missionMutex_);
    return currentMission_;
}

// ── 내부 헬퍼 ────────────────────────────────────────────────
void Robot::setState(RobotState s) {
    {
        std::lock_guard<std::mutex> lk(posMutex_);
        state_ = s;
    }
    publishState(s);
    if (stateCallback_) stateCallback_(id_, s);
}

void Robot::setPath(std::vector<Point> path) {
    std::lock_guard<std::mutex> lk(posMutex_);
    currentPath_ = std::move(path);
}

void Robot::publishPosition() {
    auto pos = getPosition();
    json j = json::object();
    j["robot_id"] = id_;
    j["x"]        = pos.x;
    j["y"]        = pos.y;
    j["t"]        = timeStep_.load();
    bus_.publish("robot.position", j.dump(), id_);
}

void Robot::publishState(RobotState s) {
    json j = json::object();
    j["robot_id"] = id_;
    j["state"]    = stateToString(s);
    j["battery"]  = battery_.load();
    bus_.publish("robot.state", j.dump(), id_);
}

void Robot::logEvent(EventLogger::EventType type, const std::string& data) {
    logger_.log(type, id_, data);
}

void Robot::requestPlan(PlanRequest req) {
    {
        std::lock_guard<std::mutex> lk(planReqMutex_);
        planReq_ = req;
    }
    planReqCV_.notify_one();
}

PlanRequest Robot::waitForPlanRequest() {
    std::unique_lock<std::mutex> lk(planReqMutex_);
    planReqCV_.wait(lk, [this]{
        return planReq_ != PlanRequest::NONE || !running_.load();
    });
    PlanRequest r = planReq_;
    planReq_ = PlanRequest::NONE;
    return r;
}

// ══════════════════════════════════════════════════════════════
// 스레드 1: 센서 ─ 배터리 관리 + 자동 충전 트리거
// ══════════════════════════════════════════════════════════════
void Robot::sensorThread() {
    while (running_.load()) {
        std::this_thread::sleep_for(400ms);

        RobotState cur = getState();

        // 배터리 소모 (이동 중)
        if (cur == RobotState::MOVING || cur == RobotState::CHARGING_TRAVEL) {
            int b = battery_.fetch_sub(1);
            if (b <= 1) {
                setState(RobotState::ERROR);
                logEvent(EventLogger::EventType::SYSTEM,
                         "{\"msg\":\"Battery depleted\"}");
                continue;
            }
            // 배터리 상태 발행
            json j = json::object();
            j["robot_id"] = id_;
            j["battery"]  = battery_.load();
            bus_.publish("robot.battery", j.dump(), id_);
        }

        // 충전 중
        if (cur == RobotState::CHARGING) {
            int b = battery_.load();
            if (b < CHARGE_FULL) {
                battery_.store(std::min(CHARGE_FULL, b + 4)); // 4%/tick 충전
                json j = json::object();
                j["robot_id"] = id_;
                j["battery"]  = battery_.load();
                bus_.publish("robot.battery", j.dump(), id_);
            } else {
                // 충전 완료
                chargeMissionActive_ = false;
                logEvent(EventLogger::EventType::CHARGE_END,
                         "{\"battery\":100}");
                setState(RobotState::IDLE);
                // 이전 미션 재개 가능
                {
                    std::lock_guard<std::mutex> lk(missionMutex_);
                    if (!missionQueue_.empty())
                        requestPlan(PlanRequest::PLAN_NEXT);
                }
            }
            continue;
        }

        // 자동 충전 트리거
        if (!chargeMissionActive_ &&
            battery_.load() <= CHARGE_THRESHOLD &&
            (cur == RobotState::IDLE || cur == RobotState::MOVING ||
             cur == RobotState::ARRIVED))
        {
            chargeMissionActive_ = true;
            Point charger = grid_.nearestCharger(getPosition());
            std::cout << "[" << name_ << "] 배터리 부족 (" << battery_.load()
                      << "%) → 충전소(" << charger.x << "," << charger.y << ")로 이동\n";

            Mission chargeMission;
            chargeMission.id             = nextMissionId_++;
            chargeMission.priority       = 10; // 가장 높은 우선순위
            chargeMission.isChargeMission = true;
            chargeMission.waypoints.push_back({charger, "CHARGE"});

            // 현재 큐에 충전 미션 최우선 삽입
            {
                std::lock_guard<std::mutex> lk(missionMutex_);
                missionQueue_.push_front(chargeMission);
            }
            logEvent(EventLogger::EventType::CHARGE_START,
                     "{\"battery\":" + std::to_string(battery_.load()) +
                     ",\"charger_x\":" + std::to_string(charger.x) +
                     ",\"charger_y\":" + std::to_string(charger.y) + "}");
            requestPlan(PlanRequest::PLAN_NEXT);
        }

        // IDLE 시 배터리 소량 회복
        if (cur == RobotState::IDLE || cur == RobotState::ARRIVED) {
            int b = battery_.load();
            if (b < CHARGE_FULL && b > CHARGE_THRESHOLD)
                battery_.store(std::min(CHARGE_FULL, b + 1));
        }
    }
}

// ══════════════════════════════════════════════════════════════
// 스레드 2: 플래너 ─ Space-Time A*, 미션 큐 관리
// ══════════════════════════════════════════════════════════════
void Robot::plannerThread() {
    Pathfinder pf(grid_);

    while (running_.load()) {
        PlanRequest req = waitForPlanRequest();
        if (!running_.load()) break;

        if (req == PlanRequest::STOP) {
            rt_.clearReservations(id_);
            setPath({});
            setState(RobotState::IDLE);
            // 제어 스레드 깨우기 (경로 없음으로 종료)
            {
                std::lock_guard<std::mutex> lk(pathMutex_);
                pendingPath_.clear();
                pathReady_ = true;
            }
            pathCV_.notify_one();
            continue;
        }

        if (req == PlanRequest::RESET) {
            rt_.clearReservations(id_);
            setPath({});
            setState(RobotState::IDLE);
            {
                std::lock_guard<std::mutex> lk(pathMutex_);
                pendingPath_.clear();
                pathReady_ = true;
            }
            pathCV_.notify_one();
            continue;
        }

        // PLAN_NEXT 또는 REPLAN
        // 다음 미션/웨이포인트 가져오기
        Mission mission;
        bool hasMission = false;
        {
            std::lock_guard<std::mutex> lk(missionMutex_);
            // 현재 미션이 있고 완료 안됐으면 계속
            if (currentMission_.has_value() && !currentMission_->isComplete()
                && req != PlanRequest::REPLAN)
            {
                mission    = *currentMission_;
                hasMission = true;
            } else {
                // 큐에서 다음 미션
                if (!missionQueue_.empty()) {
                    mission    = missionQueue_.front();
                    missionQueue_.pop_front();
                    mission.status = MissionStatus::ACTIVE;
                    currentMission_ = mission;
                    hasMission = true;

                    // REPLAN 시에도 현재 미션 유지
                } else if (req == PlanRequest::REPLAN && currentMission_.has_value()) {
                    mission    = *currentMission_;
                    hasMission = true;
                }
            }
        }

        if (!hasMission) {
            setState(RobotState::IDLE);
            continue;
        }

        const Waypoint* wp = mission.currentWaypoint();
        if (!wp) continue;

        Point from = getPosition();
        Point to   = wp->pos;

        if (from == to) {
            // 이미 도착
            onArrivalInternal(mission, pf);
            continue;
        }

        setState(wp->label == "CHARGE" ?
                 RobotState::CHARGING_TRAVEL : RobotState::PLANNING);

        {
            std::lock_guard<std::mutex> lk(posMutex_);
            target_ = to;
        }

        std::cout << "[" << name_ << "] 경로 계획: ("
                  << from.x << "," << from.y << ") → ("
                  << to.x << "," << to.y << ")"
                  << " [" << wp->label << "]\n";

        logEvent(EventLogger::EventType::MISSION_START,
                 "{\"from\":{\"x\":" + std::to_string(from.x) +
                 ",\"y\":" + std::to_string(from.y) + "}" +
                 ",\"to\":{\"x\":" + std::to_string(to.x) +
                 ",\"y\":" + std::to_string(to.y) + "}" +
                 ",\"label\":\"" + wp->label + "\"}");

        // 기존 예약 삭제 후 Space-Time A*
        rt_.clearReservations(id_);
        int curT = timeStep_.load();
        auto path = pf.findPath(from, to, id_, &rt_, curT);

        if (path.empty()) {
            std::cerr << "[" << name_ << "] 경로 없음! 재시도 대기...\n";
            setState(RobotState::ERROR);
            logEvent(EventLogger::EventType::MISSION_END,
                     "{\"result\":\"FAILED\",\"reason\":\"no_path\"}");
            // 2초 후 재시도
            std::this_thread::sleep_for(2s);
            if (running_.load()) requestPlan(PlanRequest::PLAN_NEXT);
            continue;
        }

        // 경로 예약
        rt_.reservePath(path, id_, curT);
        std::cout << "[" << name_ << "] 경로 발견: " << path.size()
                  << " 스텝 (대기 포함)\n";

        // 제어 스레드에 경로 전달
        {
            std::lock_guard<std::mutex> lk(pathMutex_);
            pendingPath_ = std::move(path);
            pathReady_   = true;
        }
        pathCV_.notify_one();

        // 제어 스레드가 이 웨이포인트에 도착할 때까지 대기
        {
            std::unique_lock<std::mutex> lk(arrivalMutex_);
            arrivalCV_.wait(lk, [this]{
                return waypointArrived_ || !running_.load();
            });
            waypointArrived_ = false;
        }

        if (!running_.load()) break;

        // 웨이포인트 도착 처리
        onArrivalInternal(mission, pf);
    }
}

// 웨이포인트 도착 후 미션 진행
void Robot::onArrivalInternal(Mission& mission, Pathfinder& /*pf*/) {
    Point arrPos = getPosition();
    const Waypoint* wp = mission.currentWaypoint();
    if (!wp) return;

    std::string label = wp->label;
    mission.advanceWaypoint();
    {
        std::lock_guard<std::mutex> lk(missionMutex_);
        currentMission_ = mission;
    }

    logEvent(EventLogger::EventType::ROBOT_MOVE,
             "{\"x\":" + std::to_string(arrPos.x) +
             ",\"y\":" + std::to_string(arrPos.y) +
             ",\"label\":\"" + label + "\"}");

    // 충전 웨이포인트 도착
    if (label == "CHARGE") {
        std::cout << "[" << name_ << "] 충전소 도착, 충전 시작\n";
        setState(RobotState::CHARGING);
        return; // 센서 스레드가 충전 관리
    }

    // 미션 완료 확인
    if (mission.isComplete()) {
        setState(RobotState::ARRIVED);
        logEvent(EventLogger::EventType::MISSION_END,
                 "{\"result\":\"COMPLETED\"}");

        json j = json::object();
        j["robot_id"]   = id_;
        j["mission_id"] = mission.id;
        bus_.publish("mission.complete", j.dump(), id_);

        std::cout << "[" << name_ << "] 미션 완료!\n";

        // 잠시 ARRIVED 후 IDLE
        std::this_thread::sleep_for(1500ms);
        if (running_.load()) {
            setState(RobotState::IDLE);
            // 대기 중인 미션 있으면 즉시 처리
            std::lock_guard<std::mutex> lk(missionMutex_);
            if (!missionQueue_.empty())
                requestPlan(PlanRequest::PLAN_NEXT);
        }
    } else {
        // 다음 웨이포인트로
        requestPlan(PlanRequest::PLAN_NEXT);
    }
}

// ══════════════════════════════════════════════════════════════
// 스레드 3: 제어 ─ 실제 이동 실행
// ══════════════════════════════════════════════════════════════
void Robot::controlThread() {
    while (running_.load()) {
        // 경로 대기
        std::vector<Point> path;
        {
            std::unique_lock<std::mutex> lk(pathMutex_);
            pathCV_.wait(lk, [this]{
                return pathReady_ || !running_.load();
            });
            if (!running_.load()) break;
            path      = std::move(pendingPath_);
            pathReady_ = false;
        }

        if (path.empty()) continue;

        setState(RobotState::MOVING);
        setPath(path);

        // 경로를 따라 이동 (첫 번째 노드 = 현재 위치 skip)
        for (size_t i = 1; i < path.size(); ++i) {
            if (!running_.load()) break;

            // 이동 중이 아니면 중단 (STOP 명령 등)
            {
                std::lock_guard<std::mutex> lk(posMutex_);
                if (state_ != RobotState::MOVING &&
                    state_ != RobotState::CHARGING_TRAVEL) break;
            }

            // 이동 간격 (대기 스텝이면 실제 이동 없이 tick만)
            bool isSamePos = (path[i] == path[i > 0 ? i-1 : 0]);
            std::this_thread::sleep_for(200ms);

            {
                std::lock_guard<std::mutex> lk(posMutex_);
                if (!isSamePos) position_ = path[i];
                // 지나간 경로 앞부분 제거
                if (currentPath_.size() > 1)
                    currentPath_.erase(currentPath_.begin());
            }

            timeStep_.fetch_add(1);
            publishPosition();

            logEvent(EventLogger::EventType::ROBOT_MOVE,
                     "{\"x\":" + std::to_string(path[i].x) +
                     ",\"y\":" + std::to_string(path[i].y) +
                     ",\"t\":" + std::to_string(timeStep_.load()) + "}");
        }

        // 도착 신호 → 플래너 스레드 깨우기
        {
            std::lock_guard<std::mutex> lk(arrivalMutex_);
            waypointArrived_ = true;
        }
        arrivalCV_.notify_one();
        setPath({});
    }
}
