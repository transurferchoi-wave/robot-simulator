#pragma once
#include "core/Mission.h"
#include <string>
#include <vector>

namespace proto {

/**
 * 확장된 명령 프로토콜
 *
 * 기존:
 *   {"cmd":"move",  "robot_id":0, "x":5, "y":7}
 *   {"cmd":"stop",  "robot_id":1}
 *   {"cmd":"reset", "robot_id":2}
 *   {"cmd":"status"}
 *
 * 신규:
 *   {"cmd":"mission", "robot_id":0, "priority":5,
 *    "waypoints":[{"x":3,"y":4,"label":"PICKUP"},
 *                 {"x":8,"y":1,"label":"DELIVERY"},
 *                 {"x":0,"y":0,"label":"RETURN"}]}
 *
 *   {"cmd":"obstacle_add",    "x":5, "y":5}
 *   {"cmd":"obstacle_remove", "x":5, "y":5}
 *   {"cmd":"replay"}
 *   {"cmd":"replay_range", "from_ms":0, "to_ms":5000}
 *   {"cmd":"clear_log"}
 */

struct Command {
    std::string type;
    int         robotId  = -1;
    int         x = 0, y = 0;
    int         priority = 5;
    std::vector<Waypoint> waypoints;
    uint64_t    fromMs = 0;
    uint64_t    toMs   = UINT64_MAX;
};

Command     parseCommand(const std::string& json);
std::string successResponse(const std::string& msg);
std::string errorResponse(const std::string& err);

} // namespace proto
