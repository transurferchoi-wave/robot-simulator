#include "messaging/EventLogger.h"
#include <chrono>
#include <sstream>
#include <algorithm>

using namespace std::chrono;

std::string EventLogger::typeToString(EventType t) {
    switch(t) {
        case EventType::ROBOT_STATE:    return "ROBOT_STATE";
        case EventType::ROBOT_MOVE:     return "ROBOT_MOVE";
        case EventType::MISSION_START:  return "MISSION_START";
        case EventType::MISSION_END:    return "MISSION_END";
        case EventType::OBSTACLE_ADD:   return "OBSTACLE_ADD";
        case EventType::OBSTACLE_REMOVE:return "OBSTACLE_REMOVE";
        case EventType::COLLISION_WARN: return "COLLISION_WARN";
        case EventType::CHARGE_START:   return "CHARGE_START";
        case EventType::CHARGE_END:     return "CHARGE_END";
        case EventType::SYSTEM:         return "SYSTEM";
        default:                        return "UNKNOWN";
    }
}

EventLogger::EventLogger() : startTimeMs_(nowMs()) {}

uint64_t EventLogger::nowMs() const {
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void EventLogger::log(EventType type, int robotId, const std::string& dataJson) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (events_.size() >= MAX_EVENTS)
        events_.erase(events_.begin()); // 오래된 것부터 제거
    events_.push_back({nowMs() - startTimeMs_, type, robotId, dataJson});
}

std::string EventLogger::getLogJson() const {
    return getReplayJson(0, UINT64_MAX);
}

std::string EventLogger::getReplayJson(uint64_t fromMs, uint64_t toMs) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream oss;
    oss << '[';
    bool first = true;
    for (const auto& e : events_) {
        if (e.timestampMs < fromMs || e.timestampMs > toMs) continue;
        if (!first) oss << ',';
        oss << "{\"t\":" << e.timestampMs
            << ",\"type\":\"" << typeToString(e.type) << "\""
            << ",\"robot\":" << e.robotId
            << ",\"data\":" << e.data
            << '}';
        first = false;
    }
    oss << ']';
    return oss.str();
}

uint64_t EventLogger::elapsedMs() const {
    return nowMs() - startTimeMs_;
}

void EventLogger::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    events_.clear();
    startTimeMs_ = nowMs();
}

size_t EventLogger::count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return events_.size();
}
