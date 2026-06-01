#!/usr/bin/env bash
#
# run_throughput.sh — серия А/В/Г: пропускная способность T, syscalls S,
#                     память M, загрузка CPU U при разных размерах буфера.
#
# Запуск (на двух узлах):
#   Сервер:  ./run_throughput.sh server
#   Клиент:  ./run_throughput.sh client <SERVER_IP>
#
# Результаты пишутся в results/throughput_*.csv
#
set -euo pipefail

ROLE="${1:?usage: run_throughput.sh <server|client> [server_ip]}"
SERVER_IP="${2:-127.0.0.1}"
PORT=5556

BIN_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
RESULTS_DIR="$(cd "$(dirname "$0")/.." && pwd)/results"
mkdir -p "$RESULTS_DIR"

# Размеры буфера (Серия А: 64КБ … 64МБ) — соответствует плану эксперимента
BUF_SIZES_KB=(64 256 1024 4096 16384 65536)
NUM_BUFFERS=64
DURATION=10
WARMUP=2

run_server() {
    for kb in "${BUF_SIZES_KB[@]}"; do
        bytes=$((kb * 1024))
        echo "=== [SERVER] buffer=${kb}KB blocking ==="
        "$BIN_DIR/tcp_bench_server" \
            --port "$PORT" --buf-size "$bytes" --num-buf "$NUM_BUFFERS" \
            --duration "$DURATION" --warmup "$WARMUP" --csv \
            >> "$RESULTS_DIR/throughput_server.csv"

        echo "=== [SERVER] buffer=${kb}KB zero-copy ==="
        "$BIN_DIR/tcp_bench_server" \
            --port "$PORT" --buf-size "$bytes" --num-buf "$NUM_BUFFERS" \
            --duration "$DURATION" --warmup "$WARMUP" --zero-copy --csv \
            >> "$RESULTS_DIR/throughput_server.csv"
    done
    echo "Server results -> $RESULTS_DIR/throughput_server.csv"
}

run_client() {
    echo "mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct" \
        > "$RESULTS_DIR/throughput_client.csv"

    for kb in "${BUF_SIZES_KB[@]}"; do
        bytes=$((kb * 1024))
        echo "=== [CLIENT] buffer=${kb}KB blocking ==="
        "$BIN_DIR/tcp_bench_client" \
            --addr "$SERVER_IP" --port "$PORT" --buf-size "$bytes" \
            --num-buf "$NUM_BUFFERS" --duration "$DURATION" --warmup "$WARMUP" --csv \
            >> "$RESULTS_DIR/throughput_client.csv"
        sleep 1

        echo "=== [CLIENT] buffer=${kb}KB zero-copy ==="
        "$BIN_DIR/tcp_bench_client" \
            --addr "$SERVER_IP" --port "$PORT" --buf-size "$bytes" \
            --num-buf "$NUM_BUFFERS" --duration "$DURATION" --warmup "$WARMUP" \
            --zero-copy --csv \
            >> "$RESULTS_DIR/throughput_client.csv"
        sleep 1
    done
    echo "Client results -> $RESULTS_DIR/throughput_client.csv"
}

case "$ROLE" in
    server) run_server ;;
    client) run_client ;;
    *) echo "Unknown role: $ROLE"; exit 1 ;;
esac
