#pragma once
#include "Robot.h"
#include <string>

// JSON 파싱 유틸리티 (경량 버전)
namespace proto {

/**
 * TCP/HTTP 명령 프로토콜
 *
 * 명령 예시:
 *   {"cmd":"move","robot_id":0,"x":5,"y":7}
 *   {"cmd":"stop","robot_id":1}
 *   {"cmd":"reset","robot_id":2}
 *   {"cmd":"status"}
 *
 * 응답:
 *   {"ok":true,"message":"..."}
 *   {"ok":false,"error":"..."}
 */

struct Command {
    std::string type;    // "move", "stop", "reset", "status"
    int         robotId = -1;
    int         x = 0, y = 0;
};

Command  parseCommand(const std::string& json);
std::string successResponse(const std::string& msg);
std::string errorResponse(const std::string& err);

} // namespace proto
