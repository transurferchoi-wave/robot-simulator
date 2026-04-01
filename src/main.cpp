#include "Simulator.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>

static std::atomic<bool> g_shutdown{false};
static Simulator* g_sim = nullptr;

void sigHandler(int /*sig*/) {
    std::cout << "\n[main] SIGINT 수신, 종료합니다...\n";
    g_shutdown.store(true);
    if (g_sim) g_sim->shutdown();
    std::exit(0);
}

int main() {
    std::cout << R"(
 ____       _           _     ____  _
|  _ \ ___ | |__   ___ | |_  / ___|(_)_ __ ___
| |_) / _ \| '_ \ / _ \| __| \___ \| | '_ ` _ \
|  _ < (_) | |_) | (_) | |_   ___) | | | | | | |
|_| \_\___/|_.__/ \___/ \__| |____/|_|_| |_| |_|

  멀티스레드 물류 로봇 시뮬레이터 v1.0
  C++17 | POSIX Socket | A* Pathfinding
)" << "\n";

    std::signal(SIGINT, sigHandler);

    try {
        Simulator sim;
        g_sim = &sim;
        sim.run();
    } catch (const std::exception& e) {
        std::cerr << "[오류] " << e.what() << "\n";
        return 1;
    }
    return 0;
}
