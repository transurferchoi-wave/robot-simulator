#include "core/Grid.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <limits>

Grid::Grid()
    : cells_(HEIGHT, std::vector<CellType>(WIDTH, CellType::FREE))
{
    auto obs = [&](int x, int y){ cells_[y][x] = CellType::OBSTACLE; };
    auto sta = [&](int x, int y){ cells_[y][x] = CellType::STATION;  };
    auto chr = [&](int x, int y){ cells_[y][x] = CellType::CHARGER;  };

    // 장애물
    obs(4,2); obs(4,3); obs(4,4); obs(4,5);
    obs(7,5); obs(7,6); obs(7,7);
    obs(1,7); obs(2,7);
    obs(2,1); obs(3,1); obs(6,1); obs(7,1);

    // 스테이션 (출발/도착)
    sta(0,0); sta(9,0); sta(0,9); sta(9,9);
    sta(5,5);

    // 충전소 (NEW) - 그리드 4곳
    chr(0,4); chr(9,4); chr(4,0); chr(4,9);
}

CellType Grid::get(int x, int y) const {
    if (!inBounds(x,y)) throw std::out_of_range("Grid::get out of bounds");
    std::lock_guard<std::mutex> lk(mutex_);
    return cells_[y][x];
}

void Grid::set(int x, int y, CellType type) {
    if (!inBounds(x,y)) return;
    std::lock_guard<std::mutex> lk(mutex_);
    cells_[y][x] = type;
}

bool Grid::isWalkable(int x, int y) const {
    if (!inBounds(x,y)) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    return cells_[y][x] != CellType::OBSTACLE;
}

bool Grid::inBounds(int x, int y) const {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

// ── 동적 장애물 ──────────────────────────────────────────────
bool Grid::addObstacle(int x, int y) {
    if (!inBounds(x, y)) return false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (cells_[y][x] == CellType::OBSTACLE) return false;
        if (cells_[y][x] == CellType::CHARGER)  return false; // 충전소는 불가
        cells_[y][x] = CellType::OBSTACLE;
    }
    if (obstacleCallback_) obstacleCallback_({x, y}, true);
    return true;
}

bool Grid::removeObstacle(int x, int y) {
    if (!inBounds(x, y)) return false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (cells_[y][x] != CellType::OBSTACLE) return false;
        cells_[y][x] = CellType::FREE;
    }
    if (obstacleCallback_) obstacleCallback_({x, y}, false);
    return true;
}

std::string Grid::toString() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream oss;
    oss << "  0123456789\n";
    for (int y = 0; y < HEIGHT; ++y) {
        oss << y << ' ';
        for (int x = 0; x < WIDTH; ++x) {
            switch (cells_[y][x]) {
                case CellType::FREE:     oss << '.'; break;
                case CellType::OBSTACLE: oss << '#'; break;
                case CellType::STATION:  oss << 'S'; break;
                case CellType::CHARGER:  oss << 'C'; break;
            }
        }
        oss << '\n';
    }
    return oss.str();
}

std::vector<Point> Grid::getObstacles() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Point> result;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            if (cells_[y][x] == CellType::OBSTACLE)
                result.push_back({x, y});
    return result;
}

std::vector<Point> Grid::getStations() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Point> result;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            if (cells_[y][x] == CellType::STATION)
                result.push_back({x, y});
    return result;
}

std::vector<Point> Grid::getChargers() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Point> result;
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            if (cells_[y][x] == CellType::CHARGER)
                result.push_back({x, y});
    return result;
}

Point Grid::nearestCharger(Point from) const {
    auto chargers = getChargers();
    if (chargers.empty()) return {0, 0};
    Point best = chargers[0];
    float bestDist = std::numeric_limits<float>::max();
    for (auto& c : chargers) {
        float d = std::hypot(float(c.x - from.x), float(c.y - from.y));
        if (d < bestDist) { bestDist = d; best = c; }
    }
    return best;
}
