#pragma once
#include "Grid.h"
#include <vector>
#include <optional>

/**
 * A* 경로 탐색기
 * 8방향 이동 지원 (상하좌우 + 대각선)
 * 대각선은 두 인접 셀이 모두 walkable일 때만 허용
 */
class Pathfinder {
public:
    explicit Pathfinder(const Grid& grid);

    // start → goal 경로 반환 (없으면 empty)
    std::vector<Point> findPath(Point start, Point goal);

private:
    const Grid& grid_;

    struct Node {
        Point  pos;
        float  g = 0;   // 시작점으로부터 비용
        float  h = 0;   // 휴리스틱 (목표까지 추정)
        float  f() const { return g + h; }
        Point  parent{-1, -1};
        bool   inOpen   = false;
        bool   inClosed = false;
    };

    float heuristic(Point a, Point b) const;
    std::vector<Point> reconstruct(
        const std::vector<std::vector<Node>>& nodes, Point goal) const;
};
