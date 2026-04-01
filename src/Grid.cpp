#include "Grid.h"
#include <sstream>
#include <stdexcept>

Grid::Grid()
    : cells_(HEIGHT, std::vector<CellType>(WIDTH, CellType::FREE))
{
    // ── 장애물 배치 (10×10 그리드) ─────────────────────────
    auto obs = [&](int x, int y){ cells_[y][x] = CellType::OBSTACLE; };

    // 중앙 벽
    obs(4,2); obs(4,3); obs(4,4); obs(4,5);
    // 오른쪽 벽
    obs(7,5); obs(7,6); obs(7,7);
    // 왼쪽 섬
    obs(1,7); obs(2,7);
    // 상단 칸막이
    obs(2,1); obs(3,1); obs(6,1); obs(7,1);

    // ── 스테이션 배치 ──────────────────────────────────────
    auto sta = [&](int x, int y){ cells_[y][x] = CellType::STATION; };
    sta(0,0); sta(9,0); sta(0,9); sta(9,9);
    sta(5,5);
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
