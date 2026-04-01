#pragma once
#include <string>
#include <functional>
#include <map>
#include <vector>
#include <mutex>

/**
 * 로봇 간 통신용 pub/sub 메시지 버스
 *
 * 토픽 예시:
 *   "robot.position"   - 로봇 위치 변경
 *   "robot.state"      - 로봇 상태 변경
 *   "robot.battery"    - 배터리 수준
 *   "obstacle.added"   - 장애물 추가
 *   "obstacle.removed" - 장애물 제거
 *   "mission.started"  - 미션 시작
 *   "mission.complete" - 미션 완료
 *   "collision.warn"   - 충돌 경고
 */
class MessageBus {
public:
    struct Message {
        std::string topic;
        std::string payload; // JSON 문자열
        int         senderId = -1;
    };

    using Handler = std::function<void(const Message&)>;

    // 토픽 구독 (핸들러 등록)
    void subscribe(const std::string& topic, int subscriberId, Handler handler);

    // 메시지 발행 (모든 구독자에게 동기 호출)
    void publish(const std::string& topic, const std::string& payload, int senderId = -1);

    // 구독 해제
    void unsubscribe(const std::string& topic, int subscriberId);

    // 최근 메시지 N개 조회 (대시보드용)
    struct LogEntry {
        uint64_t    timestampMs;
        std::string topic;
        std::string payload;
        int         senderId;
    };
    std::vector<LogEntry> getRecentMessages(size_t n = 50) const;

private:
    struct Subscription {
        int     subscriberId;
        Handler handler;
    };

    mutable std::mutex mutex_;
    std::map<std::string, std::vector<Subscription>> subs_;

    // 최근 메시지 로그 (순환 버퍼)
    mutable std::mutex logMutex_;
    std::vector<LogEntry> log_;
    static constexpr size_t MAX_LOG = 200;

    uint64_t nowMs() const;
};
