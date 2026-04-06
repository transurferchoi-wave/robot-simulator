#include "network/RobotController.h"
#include "third_party/json.hpp"
#include <stdexcept>
#include <limits>

namespace proto {

Command parseCommand(const std::string& jsonStr) {
    Command cmd;
    try {
        auto j = nlohmann::json::parse(jsonStr);

        if (j.contains("cmd"))      cmd.type     = j["cmd"].get<std::string>();
        if (j.contains("robot_id")) cmd.robotId  = j["robot_id"].get<int>();
        if (j.contains("x"))        cmd.x        = j["x"].get<int>();
        if (j.contains("y"))        cmd.y        = j["y"].get<int>();
        if (j.contains("priority")) cmd.priority = j["priority"].get<int>();
        if (j.contains("from_ms"))  cmd.fromMs   = j["from_ms"].get<int64_t>();
        if (j.contains("to_ms"))    cmd.toMs     = j["to_ms"].get<int64_t>();

        // 웨이포인트 파싱
        if (j.contains("waypoints")) {
            for (size_t i = 0; i < j["waypoints"].size(); ++i) {
                const auto& w = j["waypoints"][i];
                Waypoint wp;
                wp.pos.x = w["x"].get<int>();
                wp.pos.y = w["y"].get<int>();
                if (w.contains("label")) wp.label = w["label"].get<std::string>();
                cmd.waypoints.push_back(wp);
            }
        }
    } catch (...) {
        cmd.type = "invalid";
    }
    return cmd;
}

std::string successResponse(const std::string& msg) {
    nlohmann::json j = nlohmann::json::object();
    j["ok"]      = true;
    j["message"] = msg;
    return j.dump();
}

std::string errorResponse(const std::string& err) {
    nlohmann::json j = nlohmann::json::object();
    j["ok"]    = false;
    j["error"] = err;
    return j.dump();
}

} // namespace proto
