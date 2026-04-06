#pragma once
#include "core/Grid.h"
#include <string>
#include <vector>

// ── 웨이포인트 ─────────────────────────────────────────────
struct Waypoint {
    Point       pos;
    std::string label; // "PICKUP", "DELIVERY", "RETURN", "CHARGE", ""
};

// ── 미션 상태 ──────────────────────────────────────────────
enum class MissionStatus {
    QUEUED,
    ACTIVE,
    COMPLETED,
    FAILED,
    CANCELLED
};

std::string missionStatusToString(MissionStatus s);

// ── 미션 ───────────────────────────────────────────────────
struct Mission {
    int                  id       = 0;
    int                  priority = 0;   // 높을수록 먼저 처리
    MissionStatus        status   = MissionStatus::QUEUED;
    std::vector<Waypoint> waypoints;
    int                  currentWpIdx = 0;
    bool                 isChargeMission = false;  // 자동 충전 미션

    // 현재 목표 웨이포인트
    const Waypoint* currentWaypoint() const {
        if (currentWpIdx < (int)waypoints.size())
            return &waypoints[currentWpIdx];
        return nullptr;
    }
    bool isComplete() const {
        return currentWpIdx >= (int)waypoints.size();
    }
    void advanceWaypoint() { ++currentWpIdx; }

    // 비교 (우선순위 큐용: priority 높은 것이 먼저)
    bool operator<(const Mission& o) const {
        return priority < o.priority;
    }
};

inline std::string missionStatusToString(MissionStatus s) {
    switch (s) {
        case MissionStatus::QUEUED:    return "QUEUED";
        case MissionStatus::ACTIVE:    return "ACTIVE";
        case MissionStatus::COMPLETED: return "COMPLETED";
        case MissionStatus::FAILED:    return "FAILED";
        case MissionStatus::CANCELLED: return "CANCELLED";
        default:                       return "UNKNOWN";
    }
}
