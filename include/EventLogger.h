#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

/**
 * 이벤트 로거
 * 모든 상태 변화를 타임스탬프와 함께 기록
 * /replay HTTP 엔드포인트에서 전체 로그 조회
 * 재생 기능: 특정 구간의 이벤트를 시간순으로 반환
 */
class EventLogger {
public:
    enum class EventType {
        ROBOT_STATE,    // 로봇 상태 변경
        ROBOT_MOVE,     // 로봇 이동 (매 스텝)
        MISSION_START,  // 미션 시작
        MISSION_END,    // 미션 완료/실패
        OBSTACLE_ADD,   // 장애물 추가
        OBSTACLE_REMOVE,// 장애물 제거
        COLLISION_WARN, // 충돌 경고
        CHARGE_START,   // 충전 시작
        CHARGE_END,     // 충전 완료
        SYSTEM,         // 시스템 메시지
    };

    static std::string typeToString(EventType t);

    struct Event {
        uint64_t    timestampMs; // 시작 시각 기준 ms
        EventType   type;
        int         robotId;     // -1 = 시스템 이벤트
        std::string data;        // JSON 문자열
    };

    EventLogger();

    // 이벤트 기록
    void log(EventType type, int robotId, const std::string& dataJson);

    // 전체 이벤트 JSON 배열 반환
    std::string getLogJson() const;

    // 재생용: 특정 구간 이벤트 반환 (ms 기준)
    std::string getReplayJson(uint64_t fromMs = 0, uint64_t toMs = UINT64_MAX) const;

    // 경과 시간 (ms)
    uint64_t elapsedMs() const;

    // 로그 초기화
    void clear();

    // 이벤트 개수
    size_t count() const;

private:
    mutable std::mutex mutex_;
    std::vector<Event> events_;
    uint64_t startTimeMs_;

    static constexpr size_t MAX_EVENTS = 2000;

    uint64_t nowMs() const;
};
