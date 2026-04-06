#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <functional>

struct Point {
    int x = 0, y = 0;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point& o) const { return !(*this == o); }
    bool operator<(const Point& o)  const { return x < o.x || (x == o.x && y < o.y); }
};

enum class CellType : uint8_t {
    FREE     = 0,
    OBSTACLE = 1,
    STATION  = 2,
    CHARGER  = 3,  // 충전소 (NEW)
};

class Grid {
public:
    static constexpr int WIDTH  = 10;
    static constexpr int HEIGHT = 10;

    // 장애물 변경 콜백 (동적 장애물 지원)
    using ObstacleCallback = std::function<void(Point, bool /*added*/)>;

    Grid();

    CellType get(int x, int y) const;
    void     set(int x, int y, CellType type);
    bool     isWalkable(int x, int y) const;
    bool     inBounds(int x, int y)   const;

    // 동적 장애물 (NEW)
    bool addObstacle(int x, int y);    // true = 성공
    bool removeObstacle(int x, int y); // true = 성공

    // 장애물 변경 콜백 등록 (NEW)
    void setObstacleCallback(ObstacleCallback cb) { obstacleCallback_ = std::move(cb); }

    std::string toString() const;
    std::vector<Point> getObstacles() const;
    std::vector<Point> getStations()  const;
    std::vector<Point> getChargers()  const; // NEW

    // 가장 가까운 충전소 찾기 (NEW)
    Point nearestCharger(Point from) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::vector<CellType>> cells_;
    ObstacleCallback obstacleCallback_;
};
