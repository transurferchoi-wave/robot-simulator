#pragma once
#include "Grid.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <functional>

// ── 상태 머신 ──────────────────────────────────────────────
enum class RobotState {
    IDLE,       // 대기 중
    PLANNING,   // 경로 계산 중
    MOVING,     // 이동 중
    ARRIVED,    // 목적지 도착
    ERROR       // 오류 상태
};

std::string stateToString(RobotState s);

// ── 로봇 명령 ──────────────────────────────────────────────
struct RobotCommand {
    enum class Type { MOVE_TO, STOP, RESET } type;
    Point target; // MOVE_TO 전용
};

// ── 로봇 클래스 ────────────────────────────────────────────
class Robot {
public:
    Robot(int id, Point startPos, Grid& grid);
    ~Robot();

    // 외부 명령 (스레드 안전)
    void moveTo(Point target);
    void stop();
    void reset();

    // 상태 조회 (스레드 안전)
    int        getId()       const { return id_; }
    Point      getPosition() const;
    Point      getTarget()   const;
    RobotState getState()    const;
    std::vector<Point> getPath() const;
    int        getBattery()  const { return battery_.load(); }
    std::string getName()    const { return name_; }

    // 스레드 시작/종료
    void start();
    void shutdown();

    // 콜백: 상태 변경 시 호출
    using StateCallback = std::function<void(int robotId, RobotState)>;
    void setStateCallback(StateCallback cb) { stateCallback_ = cb; }

private:
    // ── 3개 스레드 ─────────────────────────────────────────
    void sensorThread();    // 센서: 위치 업데이트, 배터리 시뮬
    void plannerThread();   // 경로 계획: A* 실행
    void controlThread();   // 제어: 실제 이동 실행

    // 명령 큐 처리
    bool popCommand(RobotCommand& cmd);

    void setState(RobotState s);
    void setPath(std::vector<Point> path);

    // ── 멤버 변수 ──────────────────────────────────────────
    int              id_;
    std::string      name_;
    Grid&            grid_;

    // 위치/상태 (뮤텍스 보호)
    mutable std::mutex posMutex_;
    Point              position_;
    Point              target_;
    std::vector<Point> currentPath_;
    RobotState         state_{RobotState::IDLE};

    // 배터리 (원자적)
    std::atomic<int>   battery_{100};

    // 명령 큐
    std::mutex              cmdMutex_;
    std::condition_variable cmdCV_;
    std::queue<RobotCommand> cmdQueue_;

    // 경로 계획 동기화
    std::mutex              planMutex_;
    std::condition_variable planCV_;
    bool                    planRequested_{false};
    bool                    planReady_{false};
    std::vector<Point>      plannedPath_;

    // 제어 동기화
    std::mutex              ctrlMutex_;
    std::condition_variable ctrlCV_;

    // 스레드
    std::thread sensorTh_;
    std::thread plannerTh_;
    std::thread controlTh_;
    std::atomic<bool> running_{false};

    StateCallback stateCallback_;
};
