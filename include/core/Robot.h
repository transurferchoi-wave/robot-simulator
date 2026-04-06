#pragma once
#include "core/Grid.h"
#include "core/Mission.h"
#include "planning/ReservationTable.h"
#include "messaging/MessageBus.h"
#include "messaging/EventLogger.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <deque>
#include <optional>
#include <functional>

// ── 로봇 상태 머신 ─────────────────────────────────────────
enum class RobotState {
    IDLE,            // 대기 중
    PLANNING,        // 경로 계산 중
    MOVING,          // 이동 중
    ARRIVED,         // 웨이포인트 도착
    CHARGING_TRAVEL, // 충전소로 이동 중 (NEW)
    CHARGING,        // 충전 중 (NEW)
    ERROR
};

std::string stateToString(RobotState s);

// ── 플래너 요청 타입 ───────────────────────────────────────
enum class PlanRequest {
    NONE,
    PLAN_NEXT,   // 다음 웨이포인트 계획
    REPLAN,      // 장애물 변경으로 인한 재계획
    STOP,
    RESET
};

// ── 로봇 클래스 ────────────────────────────────────────────
class Robot {
public:
    Robot(int id, Point startPos, Grid& grid,
          ReservationTable& rt, MessageBus& bus, EventLogger& logger);
    ~Robot();

    // ── 외부 명령 (스레드 안전) ────────────────────────────
    void moveTo(Point target);          // 단일 목표 이동
    void addMission(Mission mission);   // 미션 큐에 추가
    void clearMissions();               // 미션 큐 비우기
    void stop();
    void reset();
    void notifyObstacleChanged();       // 경로 재계산 트리거

    // ── 상태 조회 ──────────────────────────────────────────
    int        getId()      const { return id_; }
    std::string getName()   const { return name_; }
    Point      getPosition() const;
    Point      getTarget()   const;
    RobotState getState()    const;
    std::vector<Point> getPath() const;
    int        getBattery()  const { return battery_.load(); }
    int        getMissionCount() const;
    std::optional<Mission> getCurrentMission() const;
    int        getTimeStep() const { return timeStep_.load(); }

    // ── 스레드 생명주기 ────────────────────────────────────
    void start();
    void shutdown();

    using StateCallback = std::function<void(int, RobotState)>;
    void setStateCallback(StateCallback cb) { stateCallback_ = cb; }

private:
    // ── 3개 스레드 ─────────────────────────────────────────
    void sensorThread();   // 센서: 배터리, 자동충전 판단
    void plannerThread();  // 계획: Space-Time A*, 미션 관리
    void controlThread();  // 제어: 이동 실행

    // 내부 헬퍼
    void setState(RobotState s);
    void setPath(std::vector<Point> path);
    void publishPosition();
    void publishState(RobotState s);
    void logEvent(EventLogger::EventType type, const std::string& data);
    void onArrivalInternal(Mission& mission, class Pathfinder& pf);

    // 플래너 신호
    void requestPlan(PlanRequest req);
    PlanRequest waitForPlanRequest();

    // ── 멤버 변수 ──────────────────────────────────────────
    int             id_;
    std::string     name_;
    Grid&           grid_;
    ReservationTable& rt_;
    MessageBus&     bus_;
    EventLogger&    logger_;

    // 위치/상태
    mutable std::mutex posMutex_;
    Point              position_;
    Point              target_;
    std::vector<Point> currentPath_;
    RobotState         state_{RobotState::IDLE};

    // 배터리
    std::atomic<int>   battery_{100};
    static constexpr int CHARGE_THRESHOLD  = 25; // 25% 이하면 자동 충전
    static constexpr int CHARGE_FULL       = 100;
    bool               chargeMissionActive_{false};

    // 시간 스텝 (예약 테이블 동기화용)
    std::atomic<int>   timeStep_{0};

    // 미션 큐
    mutable std::mutex missionMutex_;
    std::deque<Mission> missionQueue_;
    std::optional<Mission> currentMission_;
    int  nextMissionId_{0};

    // 플래너 ↔ 다른 스레드 동기화
    std::mutex              planReqMutex_;
    std::condition_variable planReqCV_;
    PlanRequest             planReq_{PlanRequest::NONE};

    // 플래너 → 제어 스레드
    std::mutex              pathMutex_;
    std::condition_variable pathCV_;
    std::vector<Point>      pendingPath_;
    bool                    pathReady_{false};

    // 제어 → 플래너 (웨이포인트 도착 신호)
    std::mutex              arrivalMutex_;
    std::condition_variable arrivalCV_;
    bool                    waypointArrived_{false};

    // 스레드
    std::thread sensorTh_, plannerTh_, controlTh_;
    std::atomic<bool> running_{false};

    StateCallback stateCallback_;
};
