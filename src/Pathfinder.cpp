#include "Pathfinder.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>

Pathfinder::Pathfinder(const Grid& grid) : grid_(grid) {}

float Pathfinder::heuristic(Point a, Point b) const {
    // 옥타일 거리 (대각선 이동 허용)
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    return static_cast<float>(dx + dy) + (std::sqrt(2.f) - 2.f) * std::min(dx, dy);
}

std::vector<Point> Pathfinder::findPath(Point start, Point goal) {
    if (!grid_.isWalkable(goal.x, goal.y))  return {};
    if (!grid_.isWalkable(start.x, start.y)) return {};
    if (start == goal) return {start};

    // 노드 배열 초기화
    std::vector<std::vector<Node>> nodes(
        Grid::HEIGHT, std::vector<Node>(Grid::WIDTH));
    for (int y = 0; y < Grid::HEIGHT; ++y)
        for (int x = 0; x < Grid::WIDTH; ++x)
            nodes[y][x].pos = {x, y};

    // 우선순위 큐 (f 기준 최소 힙)
    using QElem = std::pair<float, Point>;
    std::priority_queue<QElem, std::vector<QElem>, std::greater<QElem>> open;

    auto& startNode = nodes[start.y][start.x];
    startNode.g      = 0;
    startNode.h      = heuristic(start, goal);
    startNode.inOpen = true;
    open.push({startNode.f(), start});

    // 8방향 이웃
    static const int dx[] = {-1,0,1,-1,1,-1,0,1};
    static const int dy[] = {-1,-1,-1,0,0,1,1,1};
    static const float cost[] = {
        1.414f,1.f,1.414f,1.f,1.f,1.414f,1.f,1.414f
    };

    while (!open.empty()) {
        auto [f, cur] = open.top(); open.pop();

        auto& cn = nodes[cur.y][cur.x];
        if (cn.inClosed) continue;
        cn.inClosed = true;

        if (cur == goal) return reconstruct(nodes, goal);

        for (int i = 0; i < 8; ++i) {
            int nx = cur.x + dx[i];
            int ny = cur.y + dy[i];
            if (!grid_.isWalkable(nx, ny)) continue;

            // 대각선 이동: 인접 두 셀 모두 walkable이어야 함
            bool diagonal = (dx[i] != 0 && dy[i] != 0);
            if (diagonal) {
                if (!grid_.isWalkable(cur.x + dx[i], cur.y) ||
                    !grid_.isWalkable(cur.x, cur.y + dy[i]))
                    continue;
            }

            auto& nn = nodes[ny][nx];
            if (nn.inClosed) continue;

            float ng = cn.g + cost[i];
            if (!nn.inOpen || ng < nn.g) {
                nn.g      = ng;
                nn.h      = heuristic({nx,ny}, goal);
                nn.parent = cur;
                nn.inOpen = true;
                open.push({nn.f(), {nx,ny}});
            }
        }
    }
    return {}; // 경로 없음
}

std::vector<Point> Pathfinder::reconstruct(
    const std::vector<std::vector<Node>>& nodes, Point goal) const
{
    std::vector<Point> path;
    Point cur = goal;
    while (cur.x != -1) {
        path.push_back(cur);
        cur = nodes[cur.y][cur.x].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
