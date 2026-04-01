#pragma once
#include <vector>
#include <string>
#include <mutex>

struct Point {
    int x = 0, y = 0;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point& o) const { return !(*this == o); }
    bool operator<(const Point& o)  const { return x < o.x || (x == o.x && y < o.y); }
};

// 셀 타입
enum class CellType : uint8_t {
    FREE     = 0,
    OBSTACLE = 1,
    STATION  = 2,   // 출발/도착 스테이션
};

class Grid {
public:
    static constexpr int WIDTH  = 10;
    static constexpr int HEIGHT = 10;

    Grid();

    CellType get(int x, int y) const;
    void     set(int x, int y, CellType type);
    bool     isWalkable(int x, int y) const;
    bool     inBounds(int x, int y)   const;

    // 디버그 출력용
    std::string toString() const;

    // 장애물 좌표 반환 (웹 전송용)
    std::vector<Point> getObstacles() const;

    // 스테이션 좌표
    std::vector<Point> getStations() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::vector<CellType>> cells_;
};
