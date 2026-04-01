# 🤖 멀티스레드 물류 로봇 시뮬레이터

C++17 기반의 멀티스레드 물류 로봇 시뮬레이터입니다.
10×10 그리드에서 3대의 로봇이 A* 알고리즘으로 경로를 찾고, TCP 명령과 웹 대시보드로 실시간 제어됩니다.

## 아키텍처

```
┌─────────────────────────────────────────────────────┐
│                    Simulator                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │  Robot 0 │  │  Robot 1 │  │     Robot 2      │   │
│  │ ┌──────┐ │  │ ┌──────┐ │  │  ┌──────────┐   │   │
│  │ │Sensor│ │  │ │Sensor│ │  │  │  Sensor  │   │   │
│  │ │Thread│ │  │ │Thread│ │  │  │  Thread  │   │   │
│  │ ├──────┤ │  │ ├──────┤ │  │  ├──────────┤   │   │
│  │ │Plan- │ │  │ │Plan- │ │  │  │ Planner  │   │   │
│  │ │ner   │ │  │ │ner   │ │  │  │ (A* 경로) │  │   │
│  │ ├──────┤ │  │ ├──────┤ │  │  ├──────────┤   │   │
│  │ │Ctrl  │ │  │ │Ctrl  │ │  │  │ Control  │   │   │
│  │ │Thread│ │  │ │Thread│ │  │  │  Thread  │   │   │
│  └──────────┘  └──────────┘  └──────────────────┘   │
│                                                      │
│  ┌──────────────┐    ┌──────────────────────────┐    │
│  │  TCP Server  │    │       Web Server         │    │
│  │  port 8765   │    │  port 8080 (대시보드)    │    │
│  └──────────────┘    └──────────────────────────┘    │
└─────────────────────────────────────────────────────┘
```

## 스레드 구조 (로봇 1대 기준)

| 스레드 | 역할 | 동기화 |
|--------|------|--------|
| **Sensor** | 배터리 소모/충전 시뮬레이션, 위치 감지 | atomic + mutex |
| **Planner** | 명령 수신 → A* 경로 계획 | condition_variable (cmdCV) |
| **Control** | 계획된 경로 실행, 이동 시뮬레이션 | condition_variable (planCV) |

## 상태 머신

```
IDLE ──[move 명령]──▶ PLANNING ──[경로 완성]──▶ MOVING ──[도착]──▶ ARRIVED
  ▲                                                 │                   │
  │                      ◀──────[stop 명령]─────────┘                   │
  └──────────────────────────────────────────────── (2초 후 자동)──────┘

ERROR: 배터리 0% 또는 경로 없음
```

## 빌드 방법

### 사전 요구사항 (macOS)
```bash
# Homebrew로 CMake 설치
brew install cmake
```

### 빌드
```bash
git clone <repo-url>
cd robot_simulator
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 실행
```bash
./robot_sim
```

실행 후:
- **웹 대시보드**: http://localhost:8080
- **TCP 서버**: localhost:8765

## TCP 명령 프로토콜

```bash
# nc 또는 telnet으로 연결
nc localhost 8765
```

### 명령 형식 (JSON)

```json
// 로봇 이동
{"cmd": "move", "robot_id": 0, "x": 9, "y": 9}

// 로봇 정지
{"cmd": "stop", "robot_id": 1}

// 로봇 리셋 (배터리 복구)
{"cmd": "reset", "robot_id": 2}

// 전체 상태 조회
{"cmd": "status"}
```

### 응답 형식

```json
// 성공
{"ok": true, "message": "Robot-0 이동 명령: (9,9)"}

// 실패
{"ok": false, "error": "목표 위치가 장애물임"}
```

## HTTP API (웹 대시보드 연동)

| 메서드 | 경로 | 설명 |
|--------|------|------|
| GET | `/` | 웹 대시보드 HTML |
| GET | `/state` | 전체 시뮬레이터 상태 JSON |
| POST | `/command` | 로봇 명령 전송 |

## 그리드 맵

```
  0123456789
0 S...#..#..
1 ....#..#..
2 ....#.....
3 ..........
4 .#........
5 .#...S#...
6 .#...##...
7 .##.......
8 ..........
9 S.........S
```

- `S` = 스테이션 (출발/도착 지점)
- `#` = 장애물
- `.` = 이동 가능 셀

## 핵심 기술

- **C++17**: `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`
- **POSIX Socket**: TCP 서버 (비동기 accept + 클라이언트 스레드)
- **A* 알고리즘**: 8방향 이동 + 옥타일 거리 휴리스틱
- **경량 JSON**: 외부 의존성 없는 자체 구현 JSON 직렬화/파싱
- **CMake**: 크로스플랫폼 빌드 시스템

## 파일 구조

```
robot_simulator/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── Grid.h          # 10×10 그리드
│   ├── Robot.h         # 로봇 클래스 (3-스레드)
│   ├── Pathfinder.h    # A* 경로 탐색
│   ├── RobotController.h  # JSON 명령 파서
│   ├── TcpServer.h     # TCP 서버
│   ├── WebServer.h     # HTTP 서버 + 대시보드
│   └── Simulator.h     # 최상위 시뮬레이터
├── src/
│   ├── main.cpp
│   ├── Grid.cpp
│   ├── Robot.cpp
│   ├── Pathfinder.cpp
│   ├── RobotController.cpp
│   ├── TcpServer.cpp
│   ├── WebServer.cpp
│   └── Simulator.cpp
└── third_party/
    └── json.hpp        # 경량 JSON 구현
```
