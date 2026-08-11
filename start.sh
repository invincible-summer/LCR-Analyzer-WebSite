#!/usr/bin/env bash
# start.sh — 一键启动 ESP32 LCR 分析平台（FastAPI 后端 + Vite 前端）
#
# 用法:
#   ./start.sh            # 启动前后端（默认）
#   ./start.sh backend    # 仅后端
#   ./start.sh frontend   # 仅前端（自动读取 /tmp/lcr_backend_port 作为后端地址）
#   ./start.sh stop       # 按端口清单停止（任意终端可执行）
#
# 设计要点（见 web_start_sh skill）：
#   * 端口用 socket.bind 真实探测（WSL2 镜像网络下 ss/fuser 探测不可靠）
#   * 后端真实端口写入 /tmp/lcr_backend_port，前端经 LCR_BACKEND 单一真相源注入
#   * 停止按端口清单 fuser -k（不依赖原终端的 PID 变量）
#   * Ctrl+C / SIGTERM → trap cleanup：先 kill 子进程，再 fuser 按端口兜底
#   * 后端监听 0.0.0.0 —— ESP32 经 WiFi 直连上传必需

ROOT="$(cd "$(dirname "$0")" && pwd)"

# ---- conda 环境 lcr（Python 3.11）----
if command -v conda &>/dev/null; then
    eval "$(conda shell.bash hook)" 2>/dev/null || true
    conda activate lcr 2>/dev/null || true
fi
PYTHON_BIN="python"
command -v python &>/dev/null || PYTHON_BIN="python3"

if ! "$PYTHON_BIN" -c "import fastapi, scipy, sqlalchemy" >/dev/null 2>&1; then
    echo "[start.sh] 后端依赖缺失。先安装："
    echo "  conda create -y -n lcr python=3.11"
    echo "  conda run -n lcr pip install -r backend/requirements.txt"
    exit 1
fi

BACK_PID=""; FRONT_PID=""; BACK_PORT=""; FRONT_PORT=""

cleanup() {
    [ -n "$BACK_PID" ]  && kill "$BACK_PID"  2>/dev/null || true
    [ -n "$FRONT_PID" ] && kill "$FRONT_PID" 2>/dev/null || true
    sleep 1
    [ -n "$BACK_PORT" ]  && fuser -k "${BACK_PORT}/tcp"  2>/dev/null || true
    [ -n "$FRONT_PORT" ] && fuser -k "${FRONT_PORT}/tcp" 2>/dev/null || true
    echo "[start.sh] 已停止"
}
trap cleanup INT TERM

# ---- §1 bind 探测端口 ----
pick_port() {
    local preferred="$1"; shift
    local candidates=("$preferred" "$@")
    for port in "${candidates[@]}"; do
        if "$PYTHON_BIN" -c "
import socket, sys
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try: s.bind(('0.0.0.0', $port)); s.close(); sys.exit(0)
except OSError: sys.exit(1)" 2>/dev/null; then
            echo "$port"; return 0
        fi
    done
    echo "$preferred"
}

lan_ip() {
    "$PYTHON_BIN" -c "
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
try:
    s.connect(('8.8.8.8',80)); print(s.getsockname()[0])
except Exception:
    print('127.0.0.1')
finally:
    s.close()"
}

start_backend() {
    local port; port="$(pick_port 8000 8001 8002 8003)"
    BACK_PORT="$port"
    cd "$ROOT/backend"
    # 不用 --reload：保持单进程，kill $BACK_PID 即可可靠停止（fuser 兜底）
    "$PYTHON_BIN" -m uvicorn app.main:app --host 0.0.0.0 --port "$port" --proxy-headers &
    BACK_PID=$!
    echo "$port" > /tmp/lcr_backend_port
    echo "[start.sh] 后端    : http://localhost:$port  (0.0.0.0:$port)"
    echo "[start.sh] ESP32上传: http://$(lan_ip):$port/api/scan/start"
}

start_frontend() {
    local bport; bport="$(cat /tmp/lcr_backend_port 2>/dev/null || echo 8000)"
    local port; port="$(pick_port 5173 5174 5175 5176)"
    FRONT_PORT="$port"
    cd "$ROOT/frontend"
    if [ ! -x "./node_modules/.bin/vite" ]; then
        echo "[start.sh] 前端依赖缺失，执行: (cd frontend && pnpm install)"
        exit 1
    fi
    # Vite 无 Next.js 的 .next dev/build 中毒问题（dev 直接跑源码、不读 dist）。
    # 真正的对等风险：依赖优化器缓存 node_modules/.vite/deps 在改依赖后变陈旧，
    # 运行时出现 "Cannot find module" / chunk 版本错配 —— lockfile 比缓存新时清掉。
    if [ -d "$ROOT/frontend/node_modules/.vite" ] && \
       [ "$ROOT/frontend/pnpm-lock.yaml" -nt "$ROOT/frontend/node_modules/.vite" ]; then
        echo "[start.sh] pnpm-lock.yaml 比 vite 缓存新，清理 node_modules/.vite 防陈旧依赖"
        rm -rf "$ROOT/frontend/node_modules/.vite"
    fi
    # 直接调 vite 二进制（不经 pnpm/npx 壳），kill $FRONT_PID 更干净；端口仍 fuser 兜底
    # LCR_BACKEND 是前端 API 地址的单一真相源 → vite.config.ts 的 /api 与 /ws 代理据此指向真实后端
    LCR_BACKEND="http://localhost:$bport" \
        ./node_modules/.bin/vite --port "$port" --host &
    FRONT_PID=$!
    echo "$port" > /tmp/lcr_frontend_port
    echo "[start.sh] 前端    : http://localhost:$port  (/api /ws 代理 → 后端 :$bport)"
}

stop_all() {
    local bp fp
    bp="$(cat /tmp/lcr_backend_port 2>/dev/null || true)"
    fp="$(cat /tmp/lcr_frontend_port 2>/dev/null || true)"
    # 优先按 start 记录的端口精准杀——不影响他人，任意终端都可执行（/tmp 跨终端共享）
    for p in "$bp" "$fp"; do [ -n "$p" ] && fuser -k "$p/tcp" 2>/dev/null || true; done
    # /tmp 记录丢失（如重启）时的兜底：只清回退端口，避开可能被他人占用的首选 8000
    if [ -z "$bp" ]; then for p in 8001 8002 8003; do fuser -k "$p/tcp" 2>/dev/null || true; done; fi
    if [ -z "$fp" ]; then for p in 5173 5174 5175 5176; do fuser -k "$p/tcp" 2>/dev/null || true; done; fi
    rm -f /tmp/lcr_backend_port /tmp/lcr_frontend_port 2>/dev/null || true
    echo "[start.sh] 已停止（按记录端口精准清理）"
}

case "${1:-all}" in
    backend)  start_backend; wait ;;
    frontend) start_frontend; wait ;;
    all)      start_backend; sleep 2; start_frontend; wait ;;
    stop)     stop_all ;;
    clean)    rm -rf "$ROOT/frontend/node_modules/.vite" "$ROOT/frontend/dist"
              echo "[start.sh] 已清理 vite 依赖缓存与 dist（下次 dev 会重建）" ;;
    *)        echo "Usage: $0 [all|backend|frontend|stop|clean]"; exit 1 ;;
esac
