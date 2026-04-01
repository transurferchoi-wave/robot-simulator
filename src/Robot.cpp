#include "Robot.h"
#include "Pathfinder.h"
#include <chrono>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;

// ── 상태 문자열 변환 ────────────────────────────────────────
std::string stateToString(RobotState s) {
    switch (s) {
        case RobotState::IDLE:     return "IDLE";
        case RobotState::PLANNING: return "PLANNING";
        case RobotState::MOVING:   return "MOVING";
        case RobotState::ARRIVED:  return "ARRIVED";
        case RobotState::ERROR:    return "ERROR";
        default:                   return "UNKNOWN";
    }
}

// ── 생성자/소멸자 ────────────────────────────────────────────
Robot::Robot(int id, Point startPos, Grid& grid)
    : id_(id)
    , name_("Robot-" + std::to_string(id))
    , grid_(grid)
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
}

void Robot::shutdown() {
    if (!running_.load()) return;
    running_.store(false);

    // 모든 대기 중인 스레드 깨우기
    cmdCV_.notify_all();
    planCV_.notify_all();
    ctrlCV_.notify_all();

    if (sensorTh_.joinable())  sensorTh_.join();
    if (plannerTh_.joinable()) plannerTh_.join();
    if (controlTh_.joinable()) controlTh_.join();
}

// ── 외부 명령 ────────────────────────────────────────────────
void Robot::moveTo(Point target) {
    {
        std::lock_guard<std::mutex> lk(cmdMutex_);
        // 기존 명령 큐 비우기
        while (!cmdQueue_.empty()) cmdQueue_.pop();
        cmdQueue_.push({RobotCommand::Type::MOVE_TO, target});
    }
    cmdCV_.notify_one();
}

void Robot::stop() {
    {
        std::lock_guard<std::mutex> lk(cmdMutex_);
        while (!cmdQueue_.empty()) cmdQueue_.pop();
        cmdQueue_.push({RobotCommand::Type::STOP, {}});
    }
    cmdCV_.notify_one();
}

void Robot::reset() {
    {
        std::lock_guard<std::mutex> lk(cmdMutex_);
        while (!cmdQueue_.empty()) cmdQueue_.pop();
        cmdQueue_.push({RobotCommand::Type::RESET, {}});
    }
    cmdCV_.notify_one();
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

// ── 내부 헬퍼 ────────────────────────────────────────────────
void Robot::setState(RobotState s) {
    {
        std::lock_guard<std::mutex> lk(posMutex_);
        state_ = s;
    }
    if (stateCallback_) stateCallback_(id_, s);
}

void Robot::setPath(std::vector<Point> path) {
    std::lock_guard<std::mutex> lk(posMutex_);
    currentPath_ = std::move(path);
}

bool Robot::popCommand(RobotCommand& cmd) {
    std::unique_lock<std::mutex> lk(cmdMutex_);
    cmdCV_.wait(lk, [this]{
        return !cmdQueue_.empty() || !running_.load();
    });
    if (!running_.load()) return false;
    cmd = cmdQueue_.front();
    cmdQueue_.pop();
    return true;
}

// ── 스레드 1: 센서 ───────────────────────────────────────────
// 실제 센서 시뮬레이션: 주기적으로 위치를 확인하고 배터리 소모
void Robot::sensorThread() {
    while (running_.load()) {
        std::this_thread::sleep_for(500ms);

        // 배터리 소모 (MOVING 상태일 때만)
        RobotState cur;
        {
            std::lock_guard<std::mutex> lk(posMutex_);
            cur = state_;
        }
        if (cur == RobotState::MOVING) {
            int b = battery_.load();
            if (b > 0) battery_.store(b - 1);
            if (battery_.load() == 0) {
                setState(RobotState::ERROR);
                std::cerr << "[" << name_ << "] 배터리 부족!\n";
            }
        }
        // 충전 (IDLE 상태)
        if (cur == RobotState::IDLE || cur == RobotState::ARRIVED) {
            int b = battery_.load();
            if (b < 100) battery_.store(std::min(100, b + 2));
        }
    }
}

// ── 스레드 2: 경로 계획 ──────────────────────────────────────
// 명령 수신 → A* 실행 → 제어 스레드에 경로 전달
void Robot::plannerThread() {
    Pathfinder pf(grid_);

    while (running_.load()) {
        RobotCommand cmd;
        if (!popCommand(cmd)) break;

        if (cmd.type == RobotCommand::Type::STOP) {
            setState(RobotState::IDLE);
            setPath({});
            ctrlCV_.notify_all();
            continue;
        }
        if (cmd.type == RobotCommand::Type::RESET) {
            setState(RobotState::IDLE);
            {
                std::lock_guard<std::mutex> lk(posMutex_);
                battery_.store(100);
                currentPath_.clear();
            }
            ctrlCV_.notify_all();
            continue;
        }

        // MOVE_TO
        setState(RobotState::PLANNING);
        {
            std::lock_guard<std::mutex> lk(posMutex_);
            target_ = cmd.target;
        }

        Point from = getPosition();
        Point to   = cmd.target;

        std::cout << "[" << name_ << "] 경로 계산: ("
                  << from.x << "," << from.y << ") → ("
                  << to.x << "," << to.y << ")\n";

        auto path = pf.findPath(from, to);

        if (path.empty()) {
            std::cerr << "[" << name_ << "] 경로 없음!\n";
            setState(RobotState::ERROR);
            continue;
        }

        std::cout << "[" << name_ << "] 경로 발견: " << path.size() << " 스텝\n";

        // 경로를 제어 스레드로 전달
        {
            std::lock_guard<std::mutex> lk(planMutex_);
            plannedPath_  = std::move(path);
            planReady_    = true;
        }
        setState(RobotState::MOVING);
        planCV_.notify_one();
    }
}

// ── 스레드 3: 제어 ───────────────────────────────────────────
// 계획된 경로를 따라 실제로 이동 실행
void Robot::controlThread() {
    while (running_.load()) {
        // 경로 준비 대기
        std::vector<Point> path;
        {
            std::unique_lock<std::mutex> lk(planMutex_);
            planCV_.wait(lk, [this]{
                return planReady_ || !running_.load();
            });
            if (!running_.load()) break;
            path       = std::move(plannedPath_);
            planReady_ = false;
        }

        setPath(path);

        // 경로를 따라 이동 (첫 번째 노드는 현재 위치이므로 skip)
        for (size_t i = 1; i < path.size(); ++i) {
            if (!running_.load()) break;

            // MOVING 상태 아니면 중단
            {
                std::lock_guard<std::mutex> lk(posMutex_);
                if (state_ != RobotState::MOVING) break;
            }

            // 이동 시뮬레이션 (200ms/칸)
            std::this_thread::sleep_for(200ms);

            {
                std::lock_guard<std::mutex> lk(posMutex_);
                position_ = path[i];
                // 경로에서 이미 지나간 점 제거
                if (i < currentPath_.size())
                    currentPath_.erase(currentPath_.begin(),
                                       currentPath_.begin() + i);
            }
        }

        // 도착 처리
        {
            std::lock_guard<std::mutex> lk(posMutex_);
            if (state_ == RobotState::MOVING) {
                state_ = RobotState::ARRIVED;
                currentPath_.clear();
                std::cout << "[" << name_ << "] 도착: ("
                          << position_.x << "," << position_.y << ")\n";
                if (stateCallback_) stateCallback_(id_, RobotState::ARRIVED);
                // ARRIVED → IDLE (2초 후)
            }
        }

        // 2초 대기 후 IDLE로 복귀
        for (int i = 0; i < 20 && running_.load(); ++i) {
            std::this_thread::sleep_for(100ms);
            std::lock_guard<std::mutex> lk(posMutex_);
            if (state_ != RobotState::ARRIVED) break; // 새 명령 수신 시
        }
        {
            std::lock_guard<std::mutex> lk(posMutex_);
            if (state_ == RobotState::ARRIVED) {
                state_ = RobotState::IDLE;
                if (stateCallback_) stateCallback_(id_, RobotState::IDLE);
            }
        }
    }
}
