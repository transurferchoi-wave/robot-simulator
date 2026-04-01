#include "WebServer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
        throw std::runtime_error("WebServer: bind() 실패");

    ::listen(serverFd_, 10);
    running_.store(true);

    std::cout << "[WebServer] http://localhost:" << port_ << "\n";
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
        socklen_t len = sizeof(caddr);
        int fd = ::accept(serverFd_, reinterpret_cast<sockaddr*>(&caddr), &len);
        if (fd < 0) break;
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
    std::string status = (code == 200) ? "200 OK" : "404 Not Found";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << ct << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Connection: close\r\n\r\n" << body;
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
        response = buildResponse(200, "application/json",
                                  stateFn_ ? stateFn_() : "{}");
    } else if (path == "/replay" && method == "GET") {
        std::string replayCmd = R"({"cmd":"replay"})";
        response = buildResponse(200, "application/json",
                                  cmdFn_ ? cmdFn_(replayCmd) : "[]");
    } else if (path == "/command" && method == "POST") {
        response = buildResponse(200, "application/json",
                                  cmdFn_ ? cmdFn_(body) : R"({"ok":true})");
    } else if (method == "OPTIONS") {
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

// ══════════════════════════════════════════════════════════════
// 확장 웹 대시보드 HTML
// ══════════════════════════════════════════════════════════════
std::string WebServer::getDashboardHtml() {
    return R"HTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<title>🤖 물류 로봇 시뮬레이터 v2</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;display:flex;flex-direction:column;height:100vh;overflow:hidden}
header{background:#1e293b;padding:12px 20px;display:flex;align-items:center;gap:10px;border-bottom:1px solid #334155;flex-shrink:0}
header h1{font-size:1.2rem;font-weight:700}
.conn{width:9px;height:9px;border-radius:50%;background:#22c55e;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.main{display:flex;flex:1;overflow:hidden;gap:0}

/* 왼쪽: 그리드 + 컨트롤 */
.left{display:flex;flex-direction:column;padding:12px;gap:10px;width:460px;flex-shrink:0;overflow-y:auto}
canvas{display:block;border-radius:8px;cursor:crosshair}
.legend{display:flex;gap:8px;flex-wrap:wrap;font-size:.72rem;padding:4px 0}
.legend-item{display:flex;align-items:center;gap:3px}
.ld{width:10px;height:10px;border-radius:50%}

/* 가운데: 로봇 상태 */
.center{flex:1;display:flex;flex-direction:column;gap:10px;padding:12px;overflow-y:auto;border-left:1px solid #334155}

/* 오른쪽: 이벤트 + 미션 */
.right{width:280px;flex-shrink:0;display:flex;flex-direction:column;border-left:1px solid #334155;overflow:hidden}

.panel{background:#1e293b;border-radius:10px;padding:12px;border:1px solid #334155}
.panel-title{font-size:.75rem;text-transform:uppercase;letter-spacing:.05em;color:#64748b;margin-bottom:8px}

/* 로봇 카드 */
.robot-card{background:#0f172a;border-radius:8px;padding:10px;border-left:4px solid #3b82f6;margin-bottom:8px}
.robot-card.MOVING{border-color:#22c55e}
.robot-card.PLANNING,.robot-card.CHARGING_TRAVEL{border-color:#eab308}
.robot-card.ARRIVED{border-color:#a855f7}
.robot-card.CHARGING{border-color:#06b6d4}
.robot-card.ERROR{border-color:#ef4444}
.robot-name{font-weight:700;font-size:.9rem;display:flex;align-items:center;gap:6px;margin-bottom:6px}
.badge{display:inline-block;padding:1px 7px;border-radius:999px;font-size:.67rem;font-weight:700}
.badge.IDLE{background:#1e40af;color:#93c5fd}
.badge.MOVING{background:#14532d;color:#86efac}
.badge.PLANNING,.badge.CHARGING_TRAVEL{background:#713f12;color:#fde68a}
.badge.ARRIVED{background:#581c87;color:#d8b4fe}
.badge.CHARGING{background:#164e63;color:#67e8f9}
.badge.ERROR{background:#7f1d1d;color:#fca5a5}
.rinfo{font-size:.78rem;color:#94a3b8;line-height:1.8}
.bat-bar{height:5px;background:#334155;border-radius:3px;margin-top:3px}
.bat-fill{height:100%;border-radius:3px;transition:width .5s}

/* 미션 뷰 */
.wp-list{margin-top:4px;display:flex;flex-wrap:wrap;gap:4px}
.wp-chip{font-size:.66rem;padding:2px 6px;border-radius:4px;background:#334155;color:#94a3b8}
.wp-chip.done{background:#14532d;color:#86efac;text-decoration:line-through}
.wp-chip.active{background:#1e40af;color:#93c5fd;font-weight:700}

/* 명령 패널 */
.cmd-row{display:flex;gap:6px;margin-bottom:6px}
select,input[type=number],input[type=text]{background:#0f172a;border:1px solid #475569;color:#e2e8f0;border-radius:5px;padding:6px 8px;font-size:.82rem;width:100%}
button{padding:7px 12px;border-radius:6px;border:none;cursor:pointer;font-weight:600;font-size:.8rem;transition:opacity .2s}
button:hover{opacity:.8}
.btn-blue{background:#2563eb;color:#fff;width:100%}
.btn-red{background:#dc2626;color:#fff;width:100%}
.btn-cyan{background:#0891b2;color:#fff;width:100%}
.btn-purple{background:#7c3aed;color:#fff;width:100%}
.btn-green{background:#16a34a;color:#fff;width:100%}
.btn-orange{background:#d97706;color:#fff;width:100%;margin-bottom:4px}

/* 오른쪽 패널 */
.tab-bar{display:flex;border-bottom:1px solid #334155;flex-shrink:0}
.tab{flex:1;padding:8px;text-align:center;font-size:.75rem;cursor:pointer;color:#64748b;border-bottom:2px solid transparent}
.tab.active{color:#e2e8f0;border-bottom-color:#3b82f6}
.tab-content{flex:1;overflow-y:auto;padding:8px;font-size:.74rem;font-family:monospace}
.ev-line{padding:2px 0;border-bottom:1px solid #1e293b;color:#64748b;line-height:1.5}
.ev-line.ROBOT_STATE{color:#a78bfa}
.ev-line.ROBOT_MOVE{color:#475569}
.ev-line.MISSION_START{color:#22c55e}
.ev-line.MISSION_END{color:#34d399}
.ev-line.OBSTACLE_ADD{color:#f59e0b}
.ev-line.OBSTACLE_REMOVE{color:#06b6d4}
.ev-line.COLLISION_WARN{color:#ef4444;font-weight:700}
.ev-line.CHARGE_START{color:#67e8f9}
.ev-line.CHARGE_END{color:#38bdf8}

/* 재생 */
.replay-ctrl{padding:8px;border-bottom:1px solid #334155;display:flex;gap:6px;flex-shrink:0}
.replay-ctrl button{flex:1;padding:5px;font-size:.72rem}
#replayProgress{height:3px;background:#334155;flex-shrink:0}
#replayFill{height:100%;background:#3b82f6;width:0;transition:width .3s}

/* 모드 표시 */
.mode-badge{font-size:.7rem;padding:2px 8px;border-radius:4px;margin-left:auto}
.mode-badge.live{background:#14532d;color:#86efac}
.mode-badge.replay{background:#581c87;color:#d8b4fe}

/* 미션 빌더 */
.wp-builder{border:1px dashed #475569;border-radius:6px;padding:8px;margin-top:6px}
.wp-entry{display:flex;gap:4px;align-items:center;margin-bottom:4px}
.wp-entry input{flex:1}
.wp-remove{background:#7f1d1d;color:#fca5a5;border:none;border-radius:3px;padding:2px 6px;cursor:pointer;font-size:.7rem}
</style>
</head>
<body>
<header>
  <div class="conn" id="connDot"></div>
  <h1>🤖 물류 로봇 시뮬레이터 v2</h1>
  <span id="modeLabel" class="mode-badge live">LIVE</span>
  <span id="elapsedLabel" style="font-size:.75rem;color:#64748b;margin-left:8px">-</span>
</header>

<div class="main">
  <!-- 왼쪽: 그리드 -->
  <div class="left">
    <div class="panel">
      <div class="panel-title">그리드 맵 (클릭: 장애물 토글)</div>
      <canvas id="grid" width="420" height="420"></canvas>
      <div class="legend">
        <div class="legend-item"><div class="ld" style="background:#1e293b;border:1px solid #475569"></div>빈 셀</div>
        <div class="legend-item"><div class="ld" style="background:#dc2626"></div>장애물</div>
        <div class="legend-item"><div class="ld" style="background:#16a34a"></div>스테이션</div>
        <div class="legend-item"><div class="ld" style="background:#0891b2"></div>충전소</div>
        <div class="legend-item"><div class="ld" style="background:#f59e0b;border:2px solid #fff"></div>로봇</div>
      </div>
    </div>

    <!-- 명령 패널 -->
    <div class="panel">
      <div class="panel-title">빠른 명령</div>
      <div class="cmd-row">
        <select id="selRobot" style="flex:1">
          <option value="0">Robot-0</option>
          <option value="1">Robot-1</option>
          <option value="2">Robot-2</option>
        </select>
        <input type="number" id="tx" min="0" max="9" value="9" placeholder="X" style="width:55px">
        <input type="number" id="ty" min="0" max="9" value="9" placeholder="Y" style="width:55px">
      </div>
      <div class="cmd-row">
        <button class="btn-blue" onclick="sendMove()" style="flex:2">▶ 이동</button>
        <button class="btn-red"  onclick="sendStop()" style="flex:1">⏹</button>
        <button class="btn-cyan" onclick="sendReset()" style="flex:1">↺</button>
      </div>
    </div>

    <!-- 미션 빌더 (NEW) -->
    <div class="panel">
      <div class="panel-title">미션 빌더 (다중 웨이포인트)</div>
      <div class="cmd-row">
        <select id="missionRobot" style="flex:1">
          <option value="0">Robot-0</option>
          <option value="1">Robot-1</option>
          <option value="2">Robot-2</option>
        </select>
        <input type="number" id="missionPriority" value="5" min="1" max="10" placeholder="우선순위" style="width:80px">
      </div>
      <div id="wpList" class="wp-builder"></div>
      <div class="cmd-row" style="margin-top:6px">
        <button onclick="addWpRow()" style="background:#334155;color:#94a3b8;flex:1">+ 웨이포인트 추가</button>
        <button class="btn-green" onclick="sendMission()" style="flex:1">🚀 미션 전송</button>
      </div>
      <button class="btn-orange" onclick="runLogisticsDemo()">📦 물류 데모 (픽업→배송→복귀)</button>
    </div>
  </div>

  <!-- 가운데: 로봇 상태 -->
  <div class="center">
    <div class="panel-title" style="padding:0 4px">로봇 상태</div>
    <div id="robotCards"></div>

    <!-- MessageBus 실시간 피드 (NEW) -->
    <div class="panel" style="margin-top:auto">
      <div class="panel-title">MessageBus 실시간 피드</div>
      <div id="busLog" style="font-family:monospace;font-size:.72rem;max-height:120px;overflow-y:auto"></div>
    </div>
  </div>

  <!-- 오른쪽: 이벤트 로그 + 재생 -->
  <div class="right">
    <div class="tab-bar">
      <div class="tab active" id="tab-log"     onclick="switchTab('log')">이벤트 로그</div>
      <div class="tab"        id="tab-replay"  onclick="switchTab('replay')">⏯ 재생</div>
    </div>

    <!-- 이벤트 로그 탭 -->
    <div id="pane-log" class="tab-content"></div>

    <!-- 재생 탭 -->
    <div id="pane-replay" class="tab-content" style="display:none">
      <div style="color:#64748b;font-size:.75rem;margin-bottom:8px">
        녹화된 이벤트를 시간순으로 재생합니다.
      </div>
      <div style="display:flex;gap:4px;margin-bottom:6px">
        <button onclick="loadReplay()" style="background:#334155;color:#e2e8f0;flex:1;font-size:.75rem">📥 로드</button>
        <button onclick="clearLog()"  style="background:#7f1d1d;color:#fca5a5;flex:1;font-size:.75rem">🗑 초기화</button>
      </div>
      <div id="replayInfo" style="color:#64748b;font-size:.72rem;margin-bottom:6px">이벤트 없음</div>
      <div class="replay-ctrl">
        <button id="btnPlay"  onclick="replayPlay()"  style="background:#16a34a;color:#fff">▶ 재생</button>
        <button id="btnPause" onclick="replayPause()" style="background:#d97706;color:#fff">⏸</button>
        <button onclick="replayStop()"               style="background:#dc2626;color:#fff">⏹</button>
      </div>
      <div id="replayProgress"><div id="replayFill"></div></div>
      <div id="replayEvLog" style="font-family:monospace;font-size:.7rem;max-height:300px;overflow-y:auto;padding:4px"></div>
    </div>
  </div>
</div>

<script>
// ══ 상수 ═══════════════════════════════════════════════════════
const CELL = 42, COLS = 10, ROWS = 10;
const ROBOT_COLORS = ['#f59e0b','#38bdf8','#f472b6'];
const canvas = document.getElementById('grid');
const ctx = canvas.getContext('2d');

// ══ 상태 ═══════════════════════════════════════════════════════
let state = null;
let tick  = 0;
let replayData   = [];
let replayIdx    = 0;
let replayTimer  = null;
let replayActive = false;

// 미션 빌더 웨이포인트 목록
let wpRows = [];

// ══ 폴링 ═══════════════════════════════════════════════════════
async function fetchState() {
  try {
    const r = await fetch('/state');
    state = await r.json();
    tick++;
    document.getElementById('connDot').style.background = '#22c55e';
    document.getElementById('elapsedLabel').textContent =
      '⏱ ' + (state.elapsed/1000).toFixed(1) + 's';
    if (!replayActive) {
      render();
      updateBusLog();
    }
    updateEventLog();
  } catch(e) {
    document.getElementById('connDot').style.background = '#ef4444';
  }
}

setInterval(fetchState, 300);
fetchState();

// ══ 명령 전송 ══════════════════════════════════════════════════
async function sendCmd(obj) {
  const r = await fetch('/command', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify(obj)
  });
  return r.json();
}

async function sendMove() {
  const id = +document.getElementById('selRobot').value;
  const x  = +document.getElementById('tx').value;
  const y  = +document.getElementById('ty').value;
  const res = await sendCmd({cmd:'move', robot_id:id, x, y});
  addLog(res.ok ? `✅ ${res.message}` : `❌ ${res.error}`, res.ok?'MISSION_START':'ERROR');
}
function sendStop() {
  const id = +document.getElementById('selRobot').value;
  sendCmd({cmd:'stop', robot_id:id});
  addLog(`⏹ Robot-${id} 정지`, 'ROBOT_STATE');
}
function sendReset() {
  const id = +document.getElementById('selRobot').value;
  sendCmd({cmd:'reset', robot_id:id});
  addLog(`↺ Robot-${id} 리셋`, 'ROBOT_STATE');
}

// ══ 미션 빌더 ══════════════════════════════════════════════════
function addWpRow(x='', y='', label='DELIVERY') {
  const idx = wpRows.length;
  wpRows.push({x, y, label});
  renderWpRows();
}
function removeWpRow(idx) {
  wpRows.splice(idx, 1);
  renderWpRows();
}
function renderWpRows() {
  const cont = document.getElementById('wpList');
  cont.innerHTML = '';
  if (wpRows.length === 0) {
    cont.innerHTML = '<div style="color:#475569;font-size:.75rem;text-align:center;padding:6px">웨이포인트 없음 (아래 + 버튼으로 추가)</div>';
    return;
  }
  wpRows.forEach((wp, i) => {
    const d = document.createElement('div');
    d.className = 'wp-entry';
    d.innerHTML = `
      <span style="color:#64748b;font-size:.7rem;width:16px">${i+1}.</span>
      <input type="number" min="0" max="9" value="${wp.x}" placeholder="X"
        onchange="wpRows[${i}].x=+this.value" style="width:45px">
      <input type="number" min="0" max="9" value="${wp.y}" placeholder="Y"
        onchange="wpRows[${i}].y=+this.value" style="width:45px">
      <select onchange="wpRows[${i}].label=this.value" style="flex:1;font-size:.72rem">
        ${['PICKUP','DELIVERY','RETURN','GOTO'].map(l =>
          `<option ${l===wp.label?'selected':''}>${l}</option>`).join('')}
      </select>
      <button class="wp-remove" onclick="removeWpRow(${i})">✕</button>`;
    cont.appendChild(d);
  });
}
renderWpRows();

async function sendMission() {
  if (wpRows.length === 0) { alert('웨이포인트를 추가하세요'); return; }
  const id  = +document.getElementById('missionRobot').value;
  const pri = +document.getElementById('missionPriority').value;
  const res = await sendCmd({
    cmd: 'mission', robot_id: id, priority: pri,
    waypoints: wpRows.map(w => ({x:+w.x, y:+w.y, label:w.label}))
  });
  addLog(res.ok ? `🚀 ${res.message}` : `❌ ${res.error}`, res.ok?'MISSION_START':'ERROR');
  if (res.ok) { wpRows = []; renderWpRows(); }
}

async function runLogisticsDemo() {
  addLog('📦 물류 데모 시작', 'MISSION_START');
  // Robot-0: 픽업(1,1) → 배송(8,8) → 복귀(0,0)
  await sendCmd({cmd:'mission', robot_id:0, priority:7,
    waypoints:[{x:1,y:1,label:'PICKUP'},{x:8,y:8,label:'DELIVERY'},{x:0,y:0,label:'RETURN'}]});
  // Robot-1: 픽업(8,1) → 배송(0,8)
  await new Promise(r=>setTimeout(r,200));
  await sendCmd({cmd:'mission', robot_id:1, priority:6,
    waypoints:[{x:8,y:1,label:'PICKUP'},{x:0,y:8,label:'DELIVERY'}]});
  // Robot-2: 픽업(4,9) → 배송(5,0)
  await new Promise(r=>setTimeout(r,200));
  await sendCmd({cmd:'mission', robot_id:2, priority:5,
    waypoints:[{x:4,y:9,label:'PICKUP'},{x:5,y:0,label:'DELIVERY'}]});
}

// ══ 그리드 클릭: 동적 장애물 ══════════════════════════════════
canvas.addEventListener('click', async (e) => {
  const rect = canvas.getBoundingClientRect();
  const cx = Math.floor((e.clientX - rect.left) / CELL);
  const cy = Math.floor((e.clientY - rect.top)  / CELL);
  if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) return;

  // 현재 장애물이면 제거, 아니면 추가
  const isObs = state && state.obstacles && state.obstacles.some(o => o.x===cx && o.y===cy);
  const cmdType = isObs ? 'obstacle_remove' : 'obstacle_add';
  const res = await sendCmd({cmd: cmdType, x: cx, y: cy});
  addLog(res.ok ? `🧱 ${res.message}` : `❌ ${res.error}`, isObs?'OBSTACLE_REMOVE':'OBSTACLE_ADD');
});

// ══ 렌더링 ════════════════════════════════════════════════════
function render(overrideRobots) {
  if (!state) return;
  const robots = overrideRobots || state.robots;
  drawGrid(robots);
  updateRobotCards(robots);
}

function drawGrid(robots) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  for (let y=0; y<ROWS; y++) {
    for (let x=0; x<COLS; x++) {
      ctx.fillStyle = '#1e293b';
      ctx.fillRect(x*CELL, y*CELL, CELL, CELL);
      ctx.strokeStyle = '#334155';
      ctx.lineWidth = .5;
      ctx.strokeRect(x*CELL, y*CELL, CELL, CELL);
    }
  }

  // 장애물
  (state.obstacles||[]).forEach(o => {
    ctx.fillStyle = '#dc2626';
    ctx.fillRect(o.x*CELL+2, o.y*CELL+2, CELL-4, CELL-4);
  });

  // 스테이션
  (state.stations||[]).forEach(s => {
    ctx.fillStyle = '#16a34a22';
    ctx.fillRect(s.x*CELL+1, s.y*CELL+1, CELL-2, CELL-2);
    ctx.strokeStyle = '#16a34a';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(s.x*CELL+2, s.y*CELL+2, CELL-4, CELL-4);
  });

  // 충전소 (NEW)
  (state.chargers||[]).forEach(c => {
    ctx.fillStyle = '#0891b222';
    ctx.fillRect(c.x*CELL+1, c.y*CELL+1, CELL-2, CELL-2);
    ctx.strokeStyle = '#0891b2';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(c.x*CELL+2, c.y*CELL+2, CELL-4, CELL-4);
    // 번개 아이콘
    ctx.fillStyle = '#0891b2';
    ctx.font = '12px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('⚡', c.x*CELL+CELL/2, c.y*CELL+CELL/2+4);
    ctx.textAlign = 'left';
  });

  // 좌표
  ctx.fillStyle = '#334155';
  ctx.font = '9px monospace';
  for (let i=0; i<10; i++) {
    ctx.fillText(i, i*CELL+3, 11);
    ctx.fillText(i, 2, i*CELL+12);
  }

  if (!robots) return;

  // 경로
  robots.forEach((robot, ri) => {
    if (!robot.path || robot.path.length < 2) return;
    ctx.strokeStyle = ROBOT_COLORS[ri] + '55';
    ctx.lineWidth = 2.5;
    ctx.setLineDash([3,3]);
    ctx.beginPath();
    robot.path.forEach((p, i) => {
      const cx = p.x*CELL+CELL/2, cy = p.y*CELL+CELL/2;
      i===0 ? ctx.moveTo(cx,cy) : ctx.lineTo(cx,cy);
    });
    ctx.stroke();
    ctx.setLineDash([]);

    // 미션 웨이포인트 표시
    if (robot.mission && robot.mission.waypoints) {
      robot.mission.waypoints.forEach((wp, wi) => {
        const isDone = wi < robot.mission.wp_idx;
        ctx.strokeStyle = isDone ? '#334155' : ROBOT_COLORS[ri];
        ctx.lineWidth = isDone ? 1 : 2;
        ctx.beginPath();
        ctx.arc(wp.x*CELL+CELL/2, wp.y*CELL+CELL/2, 8, 0, Math.PI*2);
        ctx.stroke();
        ctx.fillStyle = isDone ? '#334155' : ROBOT_COLORS[ri] + '33';
        ctx.fill();
        // 레이블
        ctx.fillStyle = isDone ? '#475569' : ROBOT_COLORS[ri];
        ctx.font = '7px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText(wp.label ? wp.label[0] : '?',
                     wp.x*CELL+CELL/2, wp.y*CELL+CELL/2+3);
        ctx.textAlign = 'left';
      });
    }
  });

  // 로봇 (충전 상태면 번개 효과)
  robots.forEach((robot, ri) => {
    const px = robot.x*CELL+CELL/2;
    const py = robot.y*CELL+CELL/2;
    const color = ROBOT_COLORS[ri];
    const isCharging = robot.state === 'CHARGING';
    const isChargingTravel = robot.state === 'CHARGING_TRAVEL';

    // 그림자
    ctx.fillStyle = '#00000044';
    ctx.beginPath();
    ctx.ellipse(px, py+13, 9, 3, 0, 0, Math.PI*2);
    ctx.fill();

    // 충전 이동 중: 주황색 펄스
    if (isChargingTravel) {
      ctx.strokeStyle = '#f59e0b88';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(px, py, 16, 0, Math.PI*2);
      ctx.stroke();
    }

    // 로봇 몸체
    ctx.fillStyle = isCharging ? '#06b6d4' : color;
    ctx.beginPath();
    ctx.roundRect(px-11, py-11, 22, 22, 4);
    ctx.fill();

    // ID
    ctx.fillStyle = '#000';
    ctx.font = 'bold 10px sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(ri, px, py);
    ctx.textAlign = 'left';
    ctx.textBaseline = 'alphabetic';

    // 충전 중 아이콘
    if (isCharging) {
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillStyle = '#fff';
      ctx.fillText('⚡', px, py-16);
      ctx.textAlign = 'left';
    }
  });
  ctx.lineWidth = 1;
}

function updateRobotCards(robots) {
  if (!state || !robots) return;
  const cont = document.getElementById('robotCards');
  cont.innerHTML = '';

  robots.forEach((robot, i) => {
    const bat     = robot.battery || 0;
    const batColor = bat > 50 ? '#22c55e' : bat > 25 ? '#eab308' : '#ef4444';
    const card = document.createElement('div');
    card.className = `robot-card ${robot.state}`;

    // 미션 웨이포인트 칩
    let missionHtml = '<span style="color:#475569">미션 없음</span>';
    if (robot.mission && robot.mission.waypoints) {
      const chips = robot.mission.waypoints.map((wp, wi) => {
        const cls = wi < robot.mission.wp_idx ? 'done'
                  : wi === robot.mission.wp_idx ? 'active' : '';
        return `<span class="wp-chip ${cls}">${wp.label||'?'}(${wp.x},${wp.y})</span>`;
      }).join('');
      missionHtml = `<div class="wp-list">${chips}</div>`;
    }

    card.innerHTML = `
      <div class="robot-name" style="color:${ROBOT_COLORS[i]}">
        ${robot.name} <span class="badge ${robot.state}">${robot.state}</span>
        <span style="font-size:.68rem;color:#475569;margin-left:auto">미션 ${robot.mission_count}개</span>
      </div>
      <div class="rinfo">
        위치:(${robot.x},${robot.y}) 목표:(${robot.target_x},${robot.target_y})
        &nbsp;|&nbsp; 경로:${(robot.path||[]).length}스텝 &nbsp;|&nbsp; T:${robot.time_step}
        <br>배터리: ${bat}%
        <div class="bat-bar"><div class="bat-fill" style="width:${bat}%;background:${batColor}"></div></div>
        <div style="margin-top:4px">${missionHtml}</div>
      </div>`;
    cont.appendChild(card);
  });
}

// ══ MessageBus 피드 ════════════════════════════════════════════
function updateBusLog() {
  if (!state || !state.bus_log) return;
  const cont = document.getElementById('busLog');
  const entries = state.bus_log.slice(-8);
  cont.innerHTML = entries.reverse().map(m =>
    `<div style="color:#475569;border-bottom:1px solid #1e293b;padding:1px 0">
      <span style="color:#3b82f6">[${m.topic}]</span>
      <span style="color:${m.from>=0?ROBOT_COLORS[m.from%3]:'#64748b'}">
        ${m.from>=0?'R'+m.from:'sys'}</span>
      ${truncate(m.payload, 60)}
    </div>`
  ).join('');
}
function truncate(s, n) { return s.length > n ? s.slice(0,n)+'…' : s; }

// ══ 이벤트 로그 탭 ════════════════════════════════════════════
const eventLogPane = document.getElementById('pane-log');
const TYPE_COLORS = {
  ROBOT_STATE:'#a78bfa', ROBOT_MOVE:'#475569',
  MISSION_START:'#22c55e', MISSION_END:'#34d399',
  OBSTACLE_ADD:'#f59e0b', OBSTACLE_REMOVE:'#06b6d4',
  COLLISION_WARN:'#ef4444', CHARGE_START:'#67e8f9',
  CHARGE_END:'#38bdf8', SYSTEM:'#64748b'
};

async function updateEventLog() {
  if (document.getElementById('pane-log').style.display === 'none') return;
  try {
    const r = await fetch('/replay');
    const events = await r.json();
    const pane = document.getElementById('pane-log');
    const recent = events.slice(-60).reverse();
    pane.innerHTML = recent.map(e => {
      const color = TYPE_COLORS[e.type] || '#64748b';
      const ms = e.t;
      const s  = (ms/1000).toFixed(1);
      let dataStr = '';
      try { dataStr = JSON.stringify(JSON.parse(e.data)).slice(0,50); } catch { dataStr = ''; }
      return `<div class="ev-line ${e.type}">
        <span style="color:#334155">[${s}s]</span>
        <span style="color:${color}">${e.type}</span>
        ${e.robot>=0?`<span style="color:${ROBOT_COLORS[e.robot%3]}"> R${e.robot}</span>`:''}
        <span style="color:#334155"> ${dataStr}</span>
      </div>`;
    }).join('');
  } catch(e) {}
}

// ══ 재생 기능 ══════════════════════════════════════════════════
async function loadReplay() {
  const r = await fetch('/replay');
  replayData = await r.json();
  replayIdx  = 0;
  const info = document.getElementById('replayInfo');
  if (replayData.length === 0) {
    info.textContent = '이벤트 없음';
    return;
  }
  const dur = replayData[replayData.length-1].t;
  info.textContent = `${replayData.length}개 이벤트, ${(dur/1000).toFixed(1)}초 분량`;
  document.getElementById('replayEvLog').innerHTML = '';
  document.getElementById('replayFill').style.width = '0';
  addLog(`📥 재생 로드: ${replayData.length}개`, 'SYSTEM');
}

function replayPlay() {
  if (replayData.length === 0) { loadReplay().then(replayPlay); return; }
  replayActive = true;
  document.getElementById('modeLabel').className = 'mode-badge replay';
  document.getElementById('modeLabel').textContent = 'REPLAY';
  playNextEvent();
}

function playNextEvent() {
  if (!replayActive || replayIdx >= replayData.length) {
    replayStop();
    return;
  }
  const ev = replayData[replayIdx];
  const next = replayData[replayIdx+1];
  const delay = next ? Math.min(next.t - ev.t, 500) : 0;

  // 이벤트 표시
  showReplayEvent(ev);
  // 그리드에 이동 이벤트 반영
  if (ev.type === 'ROBOT_MOVE' && state) {
    try {
      const d = JSON.parse(ev.data);
      if (d.x !== undefined && ev.robot >= 0 && state.robots) {
        // 임시 로봇 위치 업데이트
        const r = state.robots[ev.robot];
        if (r) { r.x = d.x; r.y = d.y; }
        drawGrid(state.robots);
      }
    } catch(e) {}
  }
  const pct = (replayIdx / replayData.length * 100).toFixed(1);
  document.getElementById('replayFill').style.width = pct + '%';
  replayIdx++;
  replayTimer = setTimeout(playNextEvent, Math.max(50, delay));
}

function showReplayEvent(ev) {
  const color = TYPE_COLORS[ev.type] || '#64748b';
  const pane  = document.getElementById('replayEvLog');
  const div   = document.createElement('div');
  div.style.cssText = `color:${color};padding:1px 0;font-size:.7rem`;
  let d = '';
  try { d = JSON.stringify(JSON.parse(ev.data)).slice(0,40); } catch {}
  div.textContent = `[${(ev.t/1000).toFixed(2)}s] ${ev.type} R${ev.robot} ${d}`;
  pane.insertBefore(div, pane.firstChild);
  while (pane.children.length > 80) pane.removeChild(pane.lastChild);
}

function replayPause() {
  if (replayTimer) clearTimeout(replayTimer);
  replayTimer = null;
}

function replayStop() {
  if (replayTimer) clearTimeout(replayTimer);
  replayActive = false;
  replayIdx = 0;
  document.getElementById('replayFill').style.width = '0';
  document.getElementById('modeLabel').className = 'mode-badge live';
  document.getElementById('modeLabel').textContent = 'LIVE';
}

async function clearLog() {
  await sendCmd({cmd:'clear_log'});
  replayData = [];
  document.getElementById('replayEvLog').innerHTML = '';
  document.getElementById('replayInfo').textContent = '이벤트 초기화됨';
  document.getElementById('pane-log').innerHTML = '';
}

// ══ 탭 전환 ═══════════════════════════════════════════════════
function switchTab(name) {
  ['log','replay'].forEach(t => {
    document.getElementById('tab-'+t).className  = 'tab' + (t===name?' active':'');
    document.getElementById('pane-'+t).style.display = t===name?'':'none';
  });
  if (name === 'log') updateEventLog();
}

// ══ 로그 헬퍼 ═════════════════════════════════════════════════
function addLog(msg, type) {
  const pane = document.getElementById('pane-log');
  const d    = document.createElement('div');
  d.className = 'ev-line ' + (type||'');
  d.textContent = '[UI] ' + msg;
  pane.insertBefore(d, pane.firstChild);
  while (pane.children.length > 100) pane.removeChild(pane.lastChild);
}
</script>
</body>
</html>)HTML";
}
