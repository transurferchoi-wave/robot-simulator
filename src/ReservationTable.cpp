#include "ReservationTable.h"
#include <algorithm>

bool ReservationTable::reserve(Point p, int timeStep, int robotId) {
    if (timeStep < 0 || timeStep >= MAX_TIME) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    Key key{p.x, p.y, timeStep};
    auto it = table_.find(key);
    if (it != table_.end() && it->second != robotId) {
        return false; // 이미 다른 로봇이 예약
    }
    table_[key] = robotId;
    return true;
}

bool ReservationTable::isReserved(Point p, int timeStep, int queryingRobotId) const {
    if (timeStep < 0 || timeStep >= MAX_TIME) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    Key key{p.x, p.y, timeStep};
    auto it = table_.find(key);
    if (it == table_.end()) return false;
    return it->second != queryingRobotId;
}

void ReservationTable::clearReservations(int robotId) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto it = table_.begin(); it != table_.end(); ) {
        if (it->second == robotId)
            it = table_.erase(it);
        else
            ++it;
    }
}

void ReservationTable::reservePath(const std::vector<Point>& path,
                                    int robotId, int startTimeStep) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (int i = 0; i < (int)path.size(); ++i) {
        int t = startTimeStep + i;
        if (t >= MAX_TIME) break;
        Key key{path[i].x, path[i].y, t};
        table_[key] = robotId;
    }
    // 마지막 위치는 이후 시간도 예약 (도착 후 잠시 점유)
    if (!path.empty()) {
        Point last = path.back();
        int endT = startTimeStep + (int)path.size();
        for (int t = endT; t < std::min(endT + 5, MAX_TIME); ++t) {
            table_[Key{last.x, last.y, t}] = robotId;
        }
    }
}

std::set<Point> ReservationTable::getOccupiedAt(int timeStep, int excludeRobotId) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::set<Point> result;
    for (const auto& [key, rid] : table_) {
        if (std::get<2>(key) == timeStep && rid != excludeRobotId) {
            result.insert({std::get<0>(key), std::get<1>(key)});
        }
    }
    return result;
}

void ReservationTable::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    table_.clear();
}
