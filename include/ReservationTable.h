#pragma once
#include "Grid.h"
#include <map>
#include <set>
#include <mutex>
#include <tuple>

/**
 * 예약 테이블 (Space-Time Collision Avoidance)
 *
 * 로봇이 경로를 계획할 때 (위치, 시간스텝) 쌍을 예약한다.
 * 다른 로봇의 A* 계획 시 예약된 셀을 회피한다.
 *
 * 시간스텝: 로봇 이동 1칸 = 1 time_step
 * time_step은 절대 시각이 아닌 계획 기준 상대값 (0, 1, 2, ...)
 * 각 로봇은 자신의 예약을 지우고 재등록할 수 있다.
 */
class ReservationTable {
public:
    static constexpr int MAX_TIME = 80; // 최대 예약 시간 스텝

    // (x, y, time) 예약 시도 → 성공 여부 반환
    bool reserve(Point p, int timeStep, int robotId);

    // 특정 (x, y, time)이 다른 로봇에 의해 예약되었는지 확인
    bool isReserved(Point p, int timeStep, int queryingRobotId) const;

    // 로봇의 모든 예약 삭제 (재계획 시 호출)
    void clearReservations(int robotId);

    // 경로 전체를 한 번에 예약
    void reservePath(const std::vector<Point>& path, int robotId, int startTimeStep = 0);

    // 특정 시간에 점유된 셀들 조회 (다른 로봇 제외)
    std::set<Point> getOccupiedAt(int timeStep, int excludeRobotId) const;

    // 모든 예약 초기화
    void clear();

private:
    using Key = std::tuple<int, int, int>; // (x, y, time)

    mutable std::mutex mutex_;
    std::map<Key, int> table_; // key → robot_id
};
