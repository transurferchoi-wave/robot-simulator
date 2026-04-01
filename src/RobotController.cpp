#include "RobotController.h"
#include "../third_party/json.hpp"
#include <stdexcept>

namespace proto {

Command parseCommand(const std::string& jsonStr) {
    Command cmd;
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("cmd"))
            cmd.type = j["cmd"].get<std::string>();
        if (j.contains("robot_id"))
            cmd.robotId = j["robot_id"].get<int>();
        if (j.contains("x"))
            cmd.x = j["x"].get<int>();
        if (j.contains("y"))
            cmd.y = j["y"].get<int>();
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
