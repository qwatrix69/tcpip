# TCP/IP Benchmark — базовая линия для сравнения с Inflare (RDMA)

Эталонная реализация клиент-сервер на Berkeley Sockets (POSIX) для
количественного сравнения с RDMA-библиотекой Inflare по метрикам раздела 4.3 ВКР.

Параметры (`buffer_size`, `num_buffers`) намеренно совпадают со структурой
`RdmaStreamConfig`, чтобы измерения были напрямую сопоставимы.

## Измеряемые метрики (по `Метрики_4_3.docx`)

| № | Метрика | Обозн. | Как считается в TCP |
|---|---------|--------|---------------------|
| 1 | Пропускная способность | T, Гбит/с | `T = (ΔBytes × 8) / (Δt × 10⁹)`, счётчик байт по `send()`/`recv()` |
| 2 | Задержка RTT (p50/p99/p999) | L, мкс | ping-pong: `RTT/2`, 10 000 итераций |
| 3 | Загрузка CPU | U, % | `/proc/stat`, разность busy/total за интервал |
| 4 | Системные вызовы | S, 1/с | счётчик вызовов `send()`/`recv()` / Δt |
| 5 | Пропускная способность памяти | M, ГБ/с | TCP: `≈ 2×T/8` (две копии); RDMA: `≈ T/8` |
| 6 | КПД конвейера η | % | неприменимо к TCP (нет тройной буферизации) |

## Структура

```
tcpip/
├── CMakeLists.txt
├── include/
│   ├── bench_config.hpp      # парсинг аргументов, конфигурация
│   ├── socket_utils.hpp      # обёртки сокетов, send_all/recv_all, MSG_ZEROCOPY
│   ├── cpu_monitor.hpp       # чтение /proc/stat, расчёт загрузки CPU
│   ├── latency_tracker.hpp   # сбор задержек, перцентили p50/p99/p999
│   └── metrics.hpp           # структуры результатов, вывод (текст и CSV)
├── src/
│   ├── tcp_server.cpp        # сервер: throughput + ping-pong echo
│   └── tcp_client.cpp        # клиент: throughput + ping-pong + zero-copy
└── scripts/
    ├── build.sh              # сборка через CMake + Ninja
    ├── run_throughput.sh     # серии А/В/Г: T, S, M, U при разных буферах
    ├── run_latency.sh        # серия Б: задержка RTT
    └── compare.py            # сведение RDMA vs TCP, графики для ВКР
```

## Сборка

```bash
cd tcpip
./scripts/build.sh
# или вручную:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Требуется: Linux, GCC 14+ (C++23, `std::print`), CMake 3.20+.

## Запуск

### Пропускная способность (две машины)

```bash
# на узле-сервере:
./scripts/run_throughput.sh server

# на узле-клиенте:
./scripts/run_throughput.sh client 192.168.1.2
```

### Задержка (ping-pong)

```bash
# сервер:
./scripts/run_latency.sh server
# клиент:
./scripts/run_latency.sh client 192.168.1.2
```

### Одиночный прогон вручную

```bash
# сервер:
./build/tcp_bench_server --port 5556 --buf-size 1048576 --num-buf 64 --duration 10

# клиент (blocking):
./build/tcp_bench_client --addr 192.168.1.2 --port 5556 \
    --buf-size 1048576 --num-buf 64 --duration 10

# клиент (zero-copy):
./build/tcp_bench_client --addr 192.168.1.2 --buf-size 1048576 --zero-copy

# задержка:
./build/tcp_bench_client --addr 192.168.1.2 --ping-pong --iters 10000
```

## Опции

| Флаг | Описание | По умолчанию |
|------|----------|--------------|
| `--addr, -a` | адрес сервера (клиент) | 127.0.0.1 |
| `--port, -p` | TCP-порт | 5556 |
| `--buf-size, -b` | размер буфера (байт) | 1048576 (1 МБ) |
| `--num-buf, -n` | число буферов | 64 |
| `--duration, -d` | длительность измерения (с) | 10 |
| `--warmup, -w` | прогрев (с) | 2 |
| `--iters, -i` | итераций ping-pong | 10000 |
| `--zero-copy, -z` | режим MSG_ZEROCOPY | off |
| `--ping-pong, -l` | режим измерения задержки | off |
| `--csv` | вывод в формате CSV | off |

## Сравнение с Inflare

После прогона обоих стендов сведите результаты:

```bash
python3 scripts/compare.py \
    --tcp  results/throughput_client.csv \
    --rdma results/rdma_throughput.csv \
    --out  results/comparison
```

Скрипт печатает сравнительную таблицу и строит графики
`comparison_throughput.png` и `comparison_cpu.png` для вставки в главу 4.

## Замечания по методологии

- **NUMA-affinity**: для честного сравнения запускайте через
  `numactl --cpunodebind=0 --membind=0`, привязывая к тому же NUMA-узлу,
  что и сетевой адаптер (как в RDMA-стенде).
- **Governor**: зафиксируйте частоту CPU в `performance`
  (`cpupower frequency-set -g performance`) для исключения turbo boost.
- **Прогрев**: первые 2 с не учитываются — конвейер выходит на режим.
- **Метрика M**: для TCP измеряется косвенно (расчёт `2×T/8`); для точного
  значения используйте `perf stat -e LLC-load-misses,cache-references` или
  Intel VTune Memory Access, как указано в `Метрики_4_3.docx`.
