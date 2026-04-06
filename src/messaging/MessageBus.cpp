#include "messaging/MessageBus.h"
#include <chrono>
#include <algorithm>

void MessageBus::subscribe(const std::string& topic, int subscriberId, Handler handler) {
    std::lock_guard<std::mutex> lk(mutex_);
    subs_[topic].push_back({subscriberId, std::move(handler)});
}

void MessageBus::publish(const std::string& topic, const std::string& payload, int senderId) {
    // 로그 기록
    {
        std::lock_guard<std::mutex> lk(logMutex_);
        log_.push_back({nowMs(), topic, payload, senderId});
        if (log_.size() > MAX_LOG)
            log_.erase(log_.begin());
    }

    // 구독자 복사 후 호출 (deadlock 방지)
    std::vector<Subscription> handlers;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = subs_.find(topic);
        if (it != subs_.end()) handlers = it->second;
    }

    Message msg{topic, payload, senderId};
    for (auto& sub : handlers) {
        try { sub.handler(msg); } catch (...) {}
    }
}

void MessageBus::unsubscribe(const std::string& topic, int subscriberId) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = subs_.find(topic);
    if (it == subs_.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
        [&](const Subscription& s){ return s.subscriberId == subscriberId; }), v.end());
}

std::vector<MessageBus::LogEntry> MessageBus::getRecentMessages(size_t n) const {
    std::lock_guard<std::mutex> lk(logMutex_);
    if (log_.size() <= n) return log_;
    return std::vector<LogEntry>(log_.end() - n, log_.end());
}

uint64_t MessageBus::nowMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}
