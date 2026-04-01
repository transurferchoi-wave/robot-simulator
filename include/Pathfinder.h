#pragma once
#include "Grid.h"
#include "ReservationTable.h"
#include <vector>
#include <optional>

/**
 * Space-Time A* 경로 탐색기
 *
 * 일반 A*에서 시간 차원을 추가:
 * - 노드: (x, y, time_step)
 * - 액션: 8방향 이동(cost 1 or 1.414) + 대기(cost 1.2)
 * - 예약 테이블: 다른 로봇이 점유할 (x,y,t) 회피
 *
 * 이를 통해 충돌 없는 협력적 경로 계획이 가능하다.
 */
class Pathfinder {
public:
    explicit Pathfinder(const Grid& grid);

    /**
     * start → goal 경로를 찾는다.
     * @param robotId       요청 로봇 ID (예약 테이블에서 자신의 예약은 무시)
     * @param rt            예약 테이블 (nullptr 허용 → 기본 A*)
     * @param startTimeStep 현재 절대 시간 스텝 (예약 체크에 사용)
     */
    std::vector<Point> findPath(Point start, Point goal,
                                int robotId = -1,
                                ReservationTable* rt = nullptr,
                                int startTimeStep = 0);

private:
    const Grid& grid_;

    struct STNode {
        Point  pos;
        int    time = 0;
        float  g = 0;
        float  h = 0;
        float  f() const { return g + h; }
        Point  parentPos{-1, -1};
        int    parentTime = -1;
        bool   inOpen   = false;
        bool   inClosed = false;
    };

    float heuristic(Point a, Point b) const;

    std::vector<Point> reconstruct(
        const std::vector<std::vector<std::vector<STNode>>>& nodes,
        Point goal, int goalTime) const;
};
