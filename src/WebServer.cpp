#include "WebServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>

WebServer::WebServer(int port) : port_(port) {}
WebServer::~WebServer() { stop(); }

void WebServer::start(StateProvider stateFn, CommandHandler cmdFn) {
    stateFn_ = std::move(stateFn);
    cmdFn_   = std::move(cmdFn);

    serverFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) throw std::runtime_error("WebServer: socket() 실패");

    int opt = 1;
    ::setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("WebServer: bind() 실패 (포트 " + std::to_string(port_) + ")");

    ::listen(serverFd_, 10);
    running_.store(true);

    std::cout << "[WebServer] http://localhost:" << port_ << " 에서 대시보드 제공 중...\n";
    acceptThread_ = std::thread(&WebServer::acceptLoop, this);
}

void WebServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (serverFd_ >= 0) { ::close(serverFd_); serverFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
}

void WebServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in caddr{};
        socklen_t   len = sizeof(caddr);
        int fd = ::accept(serverFd_, reinterpret_cast<sockaddr*>(&caddr), &len);
        if (fd < 0) break;
        // 동기적으로 처리 (간단한 HTTP 서버)
        handleClient(fd);
    }
}

std::string WebServer::parseMethod(const std::string& req) {
    return req.substr(0, req.find(' '));
}

std::string WebServer::parsePath(const std::string& req) {
    size_t s = req.find(' ');
    if (s == std::string::npos) return "/";
    size_t e = req.find(' ', s+1);
    return req.substr(s+1, e-s-1);
}

std::string WebServer::parseBody(const std::string& req) {
    size_t sep = req.find("\r\n\r\n");
    if (sep == std::string::npos) sep = req.find("\n\n");
    if (sep == std::string::npos) return "";
    return req.substr(sep + 4);
}

std::string WebServer::buildResponse(int code, const std::string& ct,
                                      const std::string& body) {
    std::string status = (code == 200) ? "200 OK" :
                         (code == 404) ? "404 Not Found" : "400 Bad Request";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << ct << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

void WebServer::handleClient(int fd) {
    char buf[8192];
    ssize_t n = ::recv(fd, buf, sizeof(buf)-1, 0);
    if (n <= 0) { ::close(fd); return; }
    buf[n] = '\0';
    std::string request(buf);

    std::string method = parseMethod(request);
    std::string path   = parsePath(request);
    std::string body   = parseBody(request);

    std::string response;

    if (path == "/" || path == "/index.html") {
        response = buildResponse(200, "text/html", getDashboardHtml());
    } else if (path == "/state" && method == "GET") {
        std::string json = stateFn_ ? stateFn_() : "{}";
        response = buildResponse(200, "application/json", json);
    } else if (path == "/command" && method == "POST") {
        std::string result = cmdFn_ ? cmdFn_(body) : R"({"ok":true})";
        response = buildResponse(200, "application/json", result);
    } else if (method == "OPTIONS") {
        // CORS preflight
        response = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n"
                   "Access-Control-Allow-Methods: GET, POST\r\n"
                   "Access-Control-Allow-Headers: Content-Type\r\n"
                   "Content-Length: 0\r\n\r\n";
    } else {
        response = buildResponse(404, "text/plain", "Not Found");
    }

    ::send(fd, response.c_str(), response.size(), 0);
    ::close(fd);
}

// ── 웹 대시보드 HTML ────────────────────────────────────────
std::string WebServer::getDashboardHtml() {
    return R"HTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>🤖 물류 로봇 시뮬레이터</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Segoe UI', sans-serif; background: #0f172a; color: #e2e8f0; }
  header { background: #1e293b; padding: 16px 24px; display: flex; align-items: center; gap: 12px; border-bottom: 1px solid #334155; }
  header h1 { font-size: 1.4rem; font-weight: 700; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; background: #22c55e; animation: pulse 2s infinite; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.4} }
  .main { display: flex; gap: 20px; padding: 20px; }
  .panel { background: #1e293b; border-radius: 12px; padding: 16px; border: 1px solid #334155; }
  canvas { display: block; image-rendering: pixelated; border-radius: 8px; }
  .robots-panel { flex: 1; min-width: 280px; }
  .robot-card { background: #0f172a; border-radius: 8px; padding: 12px; margin-bottom: 10px; border-left: 4px solid #3b82f6; }
  .robot-card.MOVING  { border-color: #22c55e; }
  .robot-card.PLANNING{ border-color: #eab308; }
  .robot-card.ARRIVED { border-color: #a855f7; }
  .robot-card.ERROR   { border-color: #ef4444; }
  .robot-name { font-weight: 700; font-size: 1rem; margin-bottom: 6px; }
  .robot-info { font-size: 0.82rem; color: #94a3b8; line-height: 1.7; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 0.72rem; font-weight: 700; }
  .badge.IDLE     { background:#1e40af; color:#93c5fd; }
  .badge.MOVING   { background:#14532d; color:#86efac; }
  .badge.PLANNING { background:#713f12; color:#fde68a; }
  .badge.ARRIVED  { background:#581c87; color:#d8b4fe; }
  .badge.ERROR    { background:#7f1d1d; color:#fca5a5; }
  .battery-bar { height: 6px; background: #334155; border-radius: 3px; margin-top: 4px; }
  .battery-fill { height: 100%; border-radius: 3px; transition: width 0.5s; }
  .cmd-panel { min-width: 240px; }
  .cmd-panel h3 { font-size: 0.9rem; color: #94a3b8; margin-bottom: 12px; text-transform: uppercase; letter-spacing: 0.05em; }
  select, input[type=number] {
    background: #0f172a; border: 1px solid #475569; color: #e2e8f0;
    border-radius: 6px; padding: 8px; width: 100%; margin-bottom: 8px; font-size: 0.9rem;
  }
  .coord-row { display: flex; gap: 8px; }
  .coord-row input { flex: 1; }
  button { width: 100%; padding: 10px; border-radius: 8px; border: none; cursor: pointer; font-weight: 700; font-size: 0.9rem; margin-bottom: 6px; transition: opacity 0.2s; }
  button:hover { opacity: 0.85; }
  .btn-move  { background: #2563eb; color: white; }
  .btn-stop  { background: #dc2626; color: white; }
  .btn-reset { background: #0891b2; color: white; }
  .log-panel { position: fixed; bottom: 0; left: 0; right: 0; background: #0f172a; border-top: 1px solid #334155; padding: 8px 20px; height: 100px; overflow-y: auto; font-family: monospace; font-size: 0.78rem; }
  .log-line { color: #64748b; }
  .log-line.info { color: #22c55e; }
  .log-line.warn { color: #eab308; }
  .log-line.err  { color: #ef4444; }
  .legend { display: flex; gap: 12px; flex-wrap: wrap; margin-top: 10px; font-size: 0.78rem; }
  .legend-item { display: flex; align-items: center; gap: 4px; }
  .legend-dot { width: 12px; height: 12px; border-radius: 50%; }
</style>
</head>
<body>
<header>
  <div class="status-dot" id="connDot"></div>
  <h1>🤖 물류 로봇 시뮬레이터</h1>
  <span id="tickLabel" style="margin-left:auto;font-size:0.8rem;color:#64748b;">연결 중...</span>
</header>

<div class="main">
  <!-- 그리드 캔버스 -->
  <div class="panel">
    <canvas id="grid" width="400" height="400"></canvas>
    <div class="legend">
      <div class="legend-item"><div class="legend-dot" style="background:#334155"></div>빈 셀</div>
      <div class="legend-item"><div class="legend-dot" style="background:#dc2626"></div>장애물</div>
      <div class="legend-item"><div class="legend-dot" style="background:#16a34a"></div>스테이션</div>
      <div class="legend-item"><div class="legend-dot" style="background:#f59e0b;border:2px solid white"></div>로봇</div>
    </div>
  </div>

  <!-- 로봇 상태 패널 -->
  <div class="panel robots-panel" id="robotsPanel">
    <h3 style="font-size:0.9rem;color:#94a3b8;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:12px;">로봇 상태</h3>
    <div id="robotCards">로딩 중...</div>
  </div>

  <!-- 명령 패널 -->
  <div class="panel cmd-panel">
    <h3>명령 전송</h3>
    <select id="selRobot">
      <option value="0">Robot-0</option>
      <option value="1">Robot-1</option>
      <option value="2">Robot-2</option>
    </select>
    <div class="coord-row">
      <input type="number" id="tx" min="0" max="9" value="9" placeholder="X">
      <input type="number" id="ty" min="0" max="9" value="9" placeholder="Y">
    </div>
    <button class="btn-move"  onclick="sendMove()">▶ 이동</button>
    <button class="btn-stop"  onclick="sendStop()">⏹ 정지</button>
    <button class="btn-reset" onclick="sendReset()">↺ 리셋</button>

    <div style="margin-top:16px;border-top:1px solid #334155;padding-top:12px;">
      <h3>빠른 시나리오</h3>
      <button style="background:#7c3aed;color:white;margin-top:8px;" onclick="runScenario()">🚀 데모 시나리오</button>
    </div>
  </div>
</div>

<!-- 로그 -->
<div class="log-panel" id="logPanel"></div>

<script>
const CELL = 40;
const COLS = 10, ROWS = 10;
const ROBOT_COLORS = ['#f59e0b','#38bdf8','#f472b6'];
let state = null;
let tick  = 0;

const canvas = document.getElementById('grid');
const ctx    = canvas.getContext('2d');

function log(msg, cls='info') {
  const p = document.getElementById('logPanel');
  const now = new Date().toLocaleTimeString('ko',{hour12:false});
  const d = document.createElement('div');
  d.className = 'log-line ' + cls;
  d.textContent = `[${now}] ${msg}`;
  p.insertBefore(d, p.firstChild);
  while (p.children.length > 50) p.removeChild(p.lastChild);
}

async function fetchState() {
  try {
    const r = await fetch('/state');
    state = await r.json();
    tick++;
    document.getElementById('tickLabel').textContent = `Tick #${tick}`;
    document.getElementById('connDot').style.background = '#22c55e';
    render();
  } catch(e) {
    document.getElementById('connDot').style.background = '#ef4444';
    document.getElementById('tickLabel').textContent = '연결 끊김';
  }
}

async function sendCommand(obj) {
  try {
    const r = await fetch('/command', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify(obj)
    });
    const res = await r.json();
    if (res.ok) log(`✅ ${res.message || JSON.stringify(obj)}`);
    else        log(`❌ ${res.error}`, 'err');
  } catch(e) { log('명령 전송 실패', 'err'); }
}

function sendMove() {
  const id = +document.getElementById('selRobot').value;
  const x  = +document.getElementById('tx').value;
  const y  = +document.getElementById('ty').value;
  log(`Robot-${id} → (${x},${y}) 이동 명령`);
  sendCommand({cmd:'move', robot_id:id, x, y});
}
function sendStop()  {
  const id = +document.getElementById('selRobot').value;
  log(`Robot-${id} 정지 명령`, 'warn');
  sendCommand({cmd:'stop',  robot_id:id});
}
function sendReset() {
  const id = +document.getElementById('selRobot').value;
  log(`Robot-${id} 리셋 명령`, 'warn');
  sendCommand({cmd:'reset', robot_id:id});
}

async function runScenario() {
  log('🚀 데모 시나리오 시작');
  await sendCommand({cmd:'move', robot_id:0, x:9, y:0});
  await new Promise(r=>setTimeout(r,300));
  await sendCommand({cmd:'move', robot_id:1, x:0, y:9});
  await new Promise(r=>setTimeout(r,300));
  await sendCommand({cmd:'move', robot_id:2, x:5, y:5});
}

// ── 렌더링 ─────────────────────────────────────────────────
function render() {
  if (!state) return;
  drawGrid();
  updateRobotCards();
}

function drawGrid() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // 셀 배경
  for (let y=0; y<ROWS; y++) {
    for (let x=0; x<COLS; x++) {
      ctx.fillStyle = '#1e293b';
      ctx.fillRect(x*CELL, y*CELL, CELL, CELL);
      ctx.strokeStyle = '#334155';
      ctx.strokeRect(x*CELL, y*CELL, CELL, CELL);
    }
  }

  // 장애물
  if (state.obstacles) {
    state.obstacles.forEach(o => {
      ctx.fillStyle = '#dc2626';
      ctx.fillRect(o.x*CELL+2, o.y*CELL+2, CELL-4, CELL-4);
    });
  }

  // 스테이션
  if (state.stations) {
    state.stations.forEach(s => {
      ctx.fillStyle = '#16a34a44';
      ctx.fillRect(s.x*CELL+1, s.y*CELL+1, CELL-2, CELL-2);
      ctx.strokeStyle = '#16a34a';
      ctx.lineWidth = 2;
      ctx.strokeRect(s.x*CELL+2, s.y*CELL+2, CELL-4, CELL-4);
      ctx.lineWidth = 1;
    });
  }

  // 좌표 라벨
  ctx.fillStyle = '#475569';
  ctx.font = '9px monospace';
  for (let i=0; i<10; i++) {
    ctx.fillText(i, i*CELL+2, 10);
    ctx.fillText(i, 2, i*CELL+12);
  }

  // 경로 표시
  if (state.robots) {
    state.robots.forEach((robot, ri) => {
      if (robot.path && robot.path.length > 1) {
        ctx.strokeStyle = ROBOT_COLORS[ri] + '66';
        ctx.lineWidth = 3;
        ctx.setLineDash([4,4]);
        ctx.beginPath();
        robot.path.forEach((p, i) => {
          const cx = p.x*CELL + CELL/2;
          const cy = p.y*CELL + CELL/2;
          if (i===0) ctx.moveTo(cx,cy); else ctx.lineTo(cx,cy);
        });
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.lineWidth = 1;

        // 목표 표시
        const tgt = robot.path[robot.path.length-1];
        ctx.strokeStyle = ROBOT_COLORS[ri];
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(tgt.x*CELL+CELL/2, tgt.y*CELL+CELL/2, 10, 0, Math.PI*2);
        ctx.stroke();
        ctx.lineWidth = 1;
      }
    });

    // 로봇 그리기
    state.robots.forEach((robot, ri) => {
      const px = robot.x * CELL + CELL/2;
      const py = robot.y * CELL + CELL/2;
      const color = ROBOT_COLORS[ri];

      // 그림자
      ctx.fillStyle = '#00000055';
      ctx.beginPath();
      ctx.ellipse(px, py+14, 10, 4, 0, 0, Math.PI*2);
      ctx.fill();

      // 로봇 몸체
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.roundRect(px-12, py-12, 24, 24, 5);
      ctx.fill();

      // 로봇 ID
      ctx.fillStyle = '#000';
      ctx.font = 'bold 11px sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(ri, px, py);
      ctx.textAlign = 'left';
      ctx.textBaseline = 'alphabetic';
    });
  }
}

function updateRobotCards() {
  if (!state || !state.robots) return;
  const container = document.getElementById('robotCards');
  container.innerHTML = '';
  state.robots.forEach((robot, i) => {
    const bat = robot.battery || 0;
    const batColor = bat > 50 ? '#22c55e' : bat > 20 ? '#eab308' : '#ef4444';
    const card = document.createElement('div');
    card.className = `robot-card ${robot.state}`;
    card.innerHTML = `
      <div class="robot-name" style="color:${ROBOT_COLORS[i]}">
        ${robot.name} <span class="badge ${robot.state}">${robot.state}</span>
      </div>
      <div class="robot-info">
        위치: (${robot.x}, ${robot.y}) &nbsp;|&nbsp; 목표: (${robot.target_x}, ${robot.target_y})<br>
        경로 길이: ${(robot.path||[]).length} 스텝<br>
        배터리: ${bat}%
        <div class="battery-bar">
          <div class="battery-fill" style="width:${bat}%;background:${batColor}"></div>
        </div>
      </div>`;
    container.appendChild(card);
  });
}

// 폴링 시작 (300ms)
setInterval(fetchState, 300);
fetchState();
log('대시보드 시작됨');
</script>
</body>
</html>)HTML";
}
