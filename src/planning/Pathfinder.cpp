#include "planning/Pathfinder.h"
#include <cmath>
#include <algorithm>
#include <queue>

Pathfinder::Pathfinder(const Grid& grid) : grid_(grid) {}

float Pathfinder::heuristic(Point a, Point b) const {
    int dx = std::abs(a.x - b.x);
    int dy = std::abs(a.y - b.y);
    return static_cast<float>(dx + dy) + (std::sqrt(2.f) - 2.f) * std::min(dx, dy);
}

std::vector<Point> Pathfinder::findPath(Point start, Point goal,
                                         int robotId,
                                         ReservationTable* rt,
                                         int startTimeStep)
{
    if (!grid_.isWalkable(goal.x, goal.y))   return {};
    if (!grid_.isWalkable(start.x, start.y)) return {};
    if (start == goal) return {start};

    // Space-Time 노드 배열: [time][y][x]
    // time 차원: 최대 MAX_TIME 스텝
    const int T = ReservationTable::MAX_TIME;
    // nodes[t][y][x]
    std::vector<std::vector<std::vector<STNode>>> nodes(
        T, std::vector<std::vector<STNode>>(
            Grid::HEIGHT, std::vector<STNode>(Grid::WIDTH)));

    for (int t = 0; t < T; ++t)
        for (int y = 0; y < Grid::HEIGHT; ++y)
            for (int x = 0; x < Grid::WIDTH; ++x) {
                nodes[t][y][x].pos  = {x, y};
                nodes[t][y][x].time = t;
            }

    // 우선순위 큐: (f, time, Point)
    using QElem = std::tuple<float, int, Point>;
    std::priority_queue<QElem, std::vector<QElem>, std::greater<QElem>> open;

    auto& s = nodes[0][start.y][start.x];
    s.g      = 0;
    s.h      = heuristic(start, goal);
    s.inOpen = true;
    open.push({s.f(), 0, start});

    // 8방향 이동
    static const int dx[]   = {-1,0,1,-1,1,-1,0,1};
    static const int dy[]   = {-1,-1,-1,0,0,1,1,1};
    static const float cost[]= {1.414f,1.f,1.414f,1.f,1.f,1.414f,1.f,1.414f};
    static const float WAIT_COST = 1.2f; // 대기 비용 (이동보다 약간 비쌈)

    while (!open.empty()) {
        auto [f, t, cur] = open.top(); open.pop();

        auto& cn = nodes[t][cur.y][cur.x];
        if (cn.inClosed) continue;
        cn.inClosed = true;

        if (cur == goal) return reconstruct(nodes, goal, t);

        if (t + 1 >= T) continue; // 시간 초과

        // ── 이동 액션 ────────────────────────────────────────
        for (int i = 0; i < 8; ++i) {
            int nx = cur.x + dx[i];
            int ny = cur.y + dy[i];
            if (!grid_.isWalkable(nx, ny)) continue;

            bool diagonal = (dx[i] != 0 && dy[i] != 0);
            if (diagonal) {
                if (!grid_.isWalkable(cur.x + dx[i], cur.y) ||
                    !grid_.isWalkable(cur.x, cur.y + dy[i]))
                    continue;
            }

            int nt = t + 1;
            // 예약 테이블 충돌 확인
            if (rt && rt->isReserved({nx, ny}, startTimeStep + nt, robotId))
                continue;

            auto& nn = nodes[nt][ny][nx];
            if (nn.inClosed) continue;

            float ng = cn.g + cost[i];
            if (!nn.inOpen || ng < nn.g) {
                nn.g          = ng;
                nn.h          = heuristic({nx, ny}, goal);
                nn.parentPos  = cur;
                nn.parentTime = t;
                nn.inOpen     = true;
                open.push({nn.f(), nt, {nx, ny}});
            }
        }

        // ── 대기 액션 (제자리 머물기) ─────────────────────────
        // 충돌 가능성이 있을 때 잠시 기다리는 전략
        {
            int nt = t + 1;
            if (rt && rt->isReserved(cur, startTimeStep + nt, robotId)) {
                // 이 위치에서도 대기 불가 → skip
            } else {
                auto& nn = nodes[nt][cur.y][cur.x];
                if (!nn.inClosed) {
                    float ng = cn.g + WAIT_COST;
                    if (!nn.inOpen || ng < nn.g) {
                        nn.g          = ng;
                        nn.h          = heuristic(cur, goal);
                        nn.parentPos  = cur;
                        nn.parentTime = t;
                        nn.inOpen     = true;
                        open.push({nn.f(), nt, cur});
                    }
                }
            }
        }
    }
    return {}; // 경로 없음
}

std::vector<Point> Pathfinder::reconstruct(
    const std::vector<std::vector<std::vector<STNode>>>& nodes,
    Point goal, int goalTime) const
{
    std::vector<Point> path;
    Point cur = goal;
    int t = goalTime;

    while (t >= 0 && cur.x != -1) {
        path.push_back(cur);
        const auto& node = nodes[t][cur.y][cur.x];
        Point nextCur = node.parentPos;
        int   nextT   = node.parentTime;
        cur = nextCur;
        t   = nextT;
    }

    std::reverse(path.begin(), path.end());

    // 연속된 같은 위치(대기) 중 마지막 것만 남기기는 하지 않음 —
    // 대기 스텝도 경로에 포함시켜 시간 예약이 정확하게 됨
    return path;
}
