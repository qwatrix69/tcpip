#!/usr/bin/env bash
#
# run_scaling.sh — серия Д: масштабируемость многоканального TCP/IP.
#                  Аналог цепочки серверов Inflare (RdmaStreamChain).
#                  Варьируется число каналов: 1, 2, 4, 8.
#
# Запуск (на двух узлах):
#   Сервер:  ./run_scaling.sh server
#   Клиент:  ./run_scaling.sh client <SERVER_IP>
#
set -euo pipefail

ROLE="${1:?usage: run_scaling.sh <server|client> [server_ip]}"
SERVER_IP="${2:-127.0.0.1}"
PORT=5558

BIN_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
RESULTS_DIR="$(cd "$(dirname "$0")/.." && pwd)/results"
mkdir -p "$RESULTS_DIR"

CHANNELS=(1 2 4 8)
BUF_SIZE=$((1024 * 1024))   # 1 МБ (как в плане эксперимента)
DURATION=10
WARMUP=2

run_server() {
    for ch in "${CHANNELS[@]}"; do
        echo "=== [SERVER] channels=${ch} ==="
        "$BIN_DIR/tcp_bench_server_multi" \
            --port "$PORT" --channels "$ch" --buf-size "$BUF_SIZE" \
            --duration "$DURATION" --warmup "$WARMUP" --csv \
            >> "$RESULTS_DIR/scaling_server.csv"
        sleep 1
    done
    echo "Server results -> $RESULTS_DIR/scaling_server.csv"
}

run_client() {
    echo "mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct" \
        > "$RESULTS_DIR/scaling_client.csv"
    for ch in "${CHANNELS[@]}"; do
        echo "=== [CLIENT] channels=${ch} ==="
        "$BIN_DIR/tcp_bench_client_multi" \
            --addr "$SERVER_IP" --port "$PORT" --channels "$ch" \
            --buf-size "$BUF_SIZE" --duration "$DURATION" --warmup "$WARMUP" --csv \
            >> "$RESULTS_DIR/scaling_client.csv"
        sleep 2
    done
    echo "Client results -> $RESULTS_DIR/scaling_client.csv"
    echo ""
    echo "Для оценки линейности масштабирования сравните T_aggregate"
    echo "с T(1 канал) × N — RDMA даёт почти линейный рост, TCP насыщается раньше."
}

case "$ROLE" in
    server) run_server ;;
    client) run_client ;;
    *) echo "Unknown role: $ROLE"; exit 1 ;;
esac
