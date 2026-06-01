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
│   ├── tcp_client.cpp        # клиент: throughput + ping-pong + zero-copy
│   ├── tcp_server_multi.cpp  # многоканальный сервер (серия Д, аналог chain)
│   └── tcp_client_multi.cpp  # многоканальный клиент (N параллельных соединений)
└── scripts/
    ├── build.sh              # сборка через CMake + Ninja
    ├── run_throughput.sh     # серии А/В/Г: T, S, M, U при разных буферах
    ├── run_latency.sh        # серия Б: задержка RTT
    ├── run_scaling.sh        # серия Д: масштабируемость 1/2/4/8 каналов
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

### Масштабируемость — серия Д (многоканальный режим)

Аналог цепочки серверов Inflare (`RdmaStreamChain`): N параллельных соединений,
каждое в своём потоке, совокупная пропускная способность агрегируется.
Скрипт прогоняет 1, 2, 4, 8 каналов.

```bash
# сервер:
./scripts/run_scaling.sh server
# клиент:
./scripts/run_scaling.sh client 192.168.1.2
```

Одиночный прогон с заданным числом каналов:

```bash
# сервер (4 канала):
./build/tcp_bench_server_multi --port 5558 --channels 4 \
    --buf-size 1048576 --duration 10

# клиент (4 канала):
./build/tcp_bench_client_multi --addr 192.168.1.2 --port 5558 --channels 4 \
    --buf-size 1048576 --duration 10
```

В колонке `num_buf` итогового CSV для multi-channel хранится число каналов N —
это позволяет `compare.py` построить график масштабируемости.

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
| `--channels, -c` | число каналов (только multi) | 4 |
| `--csv` | вывод в формате CSV | off |

## Сравнение с Inflare

Сторона Inflare сама печатает CSV в идентичном формате:
- `sample_rdma_server` — строка `CSV: rdma,server,…` с 11-й колонкой `eta_pct`
  (метрика 6, КПД конвейера — берётся через публичный `RdmaStreamServer::PipelineEfficiency()`);
- `sample_rdma_latency` — строка `CSV: rdma,client,…` с `p50_us`/`p99_us`
  (метрика 2, через `RdmaStreamClient::LatencySamples()`).

`compare.py` понимает оба формата: голые CSV-строки TCP-бенчмарка и строки
RDMA-сэмплов с префиксом `CSV:` (лишние лог-строки игнорируются).

После прогона обоих стендов сведите результаты:

```bash
# пропускная способность, CPU, η (серии А/В/Г):
python3 scripts/compare.py \
    --tcp  results/throughput_client.csv \
    --rdma results/rdma_server.csv \
    --out  results/comparison

# полное сведение (+ задержка метрика 2, + масштабируемость серия Д):
python3 scripts/compare.py \
    --tcp           results/throughput_client.csv \
    --rdma          results/rdma_server.csv \
    --tcp-latency   results/latency_client.csv \
    --rdma-latency  results/rdma_latency.csv \
    --scaling       results/scaling_client.csv \
    --rdma-scaling  results/rdma_chain.csv \
    --out           results/comparison
```

Где взять RDMA-CSV (на стенде с DOCA):

```bash
# метрики 1,3,4,5,6 — сервер печатает CSV-строку в конце измерения:
./sample_rdma_server | grep '^CSV:' > results/rdma_server.csv

# метрика 2 — отдельный сэмпл латентности (p50/p99/p999):
./sample_rdma_latency | grep '^CSV:' > results/rdma_latency.csv
```

Скрипт печатает сравнительную таблицу (с колонкой `eta_R`), таблицу задержек
и строит графики:
- `comparison_throughput.png` — пропускная способность от размера буфера
- `comparison_cpu.png` — загрузка CPU
- `comparison_scaling.png` — масштабируемость T от числа каналов (серия Д)

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
