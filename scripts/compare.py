#!/usr/bin/env python3
"""
compare.py — сведение результатов RDMA (Inflare) и TCP/IP в одну таблицу
             и построение графиков для главы 4.3 ВКР.

Использование:
    python3 compare.py --tcp results/throughput_client.csv \
                       --rdma results/rdma_throughput.csv \
                       --out results/comparison

CSV-формат (одинаковый для обоих источников):
    mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct
"""
import argparse
import csv
import os
import sys


def read_csv(path):
    if not os.path.exists(path):
        print(f"WARN: файл не найден: {path}", file=sys.stderr)
        return []
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            # приводим числовые поля
            for k in ("buf_kb", "num_buf"):
                row[k] = int(float(row[k]))
            for k in ("T_gbps", "S_calls_s", "M_gbps", "p50_us", "p99_us", "cpu_pct"):
                row[k] = float(row[k])
            rows.append(row)
    return rows


def print_comparison_table(tcp_rows, rdma_rows):
    """Печатает сравнительную таблицу по метрикам из Метрики_4_3.docx"""
    print("\n" + "=" * 92)
    print("СРАВНЕНИЕ RDMA (Inflare) и TCP/IP — метрики 4.3")
    print("=" * 92)

    # Индексируем по (mode, buf_kb)
    def index(rows):
        return {(r["mode"], r["buf_kb"]): r for r in rows}

    tcp = index(tcp_rows)
    rdma = index(rdma_rows)

    buf_sizes = sorted({r["buf_kb"] for r in tcp_rows + rdma_rows})

    header = f"{'buf(KB)':>8} | {'T_RDMA':>8} {'T_TCP':>8} {'×':>5} | " \
             f"{'CPU_R':>6} {'CPU_T':>6} | {'S_RDMA':>7} {'S_TCP':>8} | " \
             f"{'M_RDMA':>7} {'M_TCP':>7}"
    print(header)
    print("-" * 92)

    for kb in buf_sizes:
        r = next((rdma[k] for k in rdma if k[1] == kb), None)
        t = tcp.get(("tcp-blocking", kb))
        if not t:
            t = next((tcp[k] for k in tcp if k[1] == kb), None)

        T_r = r["T_gbps"] if r else 0.0
        T_t = t["T_gbps"] if t else 0.0
        ratio = (T_r / T_t) if T_t > 0 else 0.0
        cpu_r = r["cpu_pct"] if r else 0.0
        cpu_t = t["cpu_pct"] if t else 0.0
        s_r = r["S_calls_s"] if r else 0.0
        s_t = t["S_calls_s"] if t else 0.0
        m_r = r["M_gbps"] if r else 0.0
        m_t = t["M_gbps"] if t else 0.0

        print(f"{kb:>8} | {T_r:>8.2f} {T_t:>8.2f} {ratio:>5.1f} | "
              f"{cpu_r:>6.1f} {cpu_t:>6.1f} | {s_r:>7.0f} {s_t:>8.0f} | "
              f"{m_r:>7.2f} {m_t:>7.2f}")

    print("=" * 92)
    print("T — пропускная способность (Гбит/с); × — превышение RDMA над TCP")
    print("CPU — загрузка процессора (%); S — системные вызовы/с; M — память (ГБ/с)")


def write_merged_csv(tcp_rows, rdma_rows, out_path):
    fields = ["mode", "side", "buf_kb", "num_buf",
              "T_gbps", "S_calls_s", "M_gbps", "p50_us", "p99_us", "cpu_pct"]
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rdma_rows + tcp_rows:
            w.writerow({k: r.get(k, "") for k in fields})
    print(f"\nОбъединённый CSV: {out_path}")


def try_plot(tcp_rows, rdma_rows, out_prefix):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib не установлен — графики пропущены "
              "(pip install matplotlib)", file=sys.stderr)
        return

    def series(rows, mode, x, y):
        pts = sorted([(r[x], r[y]) for r in rows if r["mode"] == mode])
        return [p[0] for p in pts], [p[1] for p in pts]

    # График 1: Пропускная способность от размера буфера
    fig, ax = plt.subplots(figsize=(9, 6))
    for mode, label, color in [
        ("rdma", "RDMA (Inflare)", "#2e7d32"),
        ("tcp-blocking", "TCP blocking", "#c62828"),
        ("tcp-zerocopy", "TCP zero-copy", "#ef6c00"),
    ]:
        x, y = series(rdma_rows + tcp_rows, mode, "buf_kb", "T_gbps")
        if x:
            ax.plot(x, y, marker="o", label=label, color=color, linewidth=2)
    ax.set_xscale("log", base=2)
    ax.set_xlabel("Размер буфера (КБ)")
    ax.set_ylabel("Пропускная способность (Гбит/с)")
    ax.set_title("Пропускная способность RDMA и TCP/IP")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.savefig(f"{out_prefix}_throughput.png", dpi=200, bbox_inches="tight")
    print(f"График: {out_prefix}_throughput.png")

    # График 2: Загрузка CPU
    fig2, ax2 = plt.subplots(figsize=(9, 6))
    for mode, label, color in [
        ("rdma", "RDMA (Inflare)", "#2e7d32"),
        ("tcp-blocking", "TCP blocking", "#c62828"),
    ]:
        x, y = series(rdma_rows + tcp_rows, mode, "buf_kb", "cpu_pct")
        if x:
            ax2.plot(x, y, marker="s", label=label, color=color, linewidth=2)
    ax2.set_xscale("log", base=2)
    ax2.set_xlabel("Размер буфера (КБ)")
    ax2.set_ylabel("Загрузка CPU (%)")
    ax2.set_title("Загрузка CPU: RDMA и TCP/IP")
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    fig2.savefig(f"{out_prefix}_cpu.png", dpi=200, bbox_inches="tight")
    print(f"График: {out_prefix}_cpu.png")


def plot_scaling(scaling_rows, rdma_scaling_rows, out_prefix):
    """График масштабируемости (серия Д): T_aggregate от числа каналов."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return

    def series(rows):
        # для multi-channel число каналов лежит в num_buf
        pts = sorted([(r["num_buf"], r["T_gbps"]) for r in rows])
        return [p[0] for p in pts], [p[1] for p in pts]

    fig, ax = plt.subplots(figsize=(9, 6))

    x_tcp, y_tcp = series(scaling_rows)
    if x_tcp:
        ax.plot(x_tcp, y_tcp, marker="o", label="TCP/IP (multi-channel)",
                color="#c62828", linewidth=2)
        # идеальная линейная линия от 1 канала
        if y_tcp:
            base = y_tcp[0]
            ax.plot(x_tcp, [base * n for n in x_tcp], "--", color="#c62828",
                    alpha=0.4, label="TCP идеальное (линейное)")

    x_r, y_r = series(rdma_scaling_rows)
    if x_r:
        ax.plot(x_r, y_r, marker="s", label="RDMA (Inflare chain)",
                color="#2e7d32", linewidth=2)
        if y_r:
            base_r = y_r[0]
            ax.plot(x_r, [base_r * n for n in x_r], "--", color="#2e7d32",
                    alpha=0.4, label="RDMA идеальное (линейное)")

    ax.set_xlabel("Число каналов (N)")
    ax.set_ylabel("Суммарная пропускная способность (Гбит/с)")
    ax.set_title("Масштабируемость: TCP/IP и RDMA (серия Д)")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.savefig(f"{out_prefix}_scaling.png", dpi=200, bbox_inches="tight")
    print(f"График: {out_prefix}_scaling.png")


def main():
    ap = argparse.ArgumentParser(description="Сравнение RDMA и TCP/IP метрик")
    ap.add_argument("--tcp", required=True, help="CSV с TCP-результатами")
    ap.add_argument("--rdma", default="", help="CSV с RDMA-результатами (Inflare)")
    ap.add_argument("--scaling", default="", help="CSV TCP multi-channel (серия Д)")
    ap.add_argument("--rdma-scaling", default="", help="CSV RDMA chain (серия Д)")
    ap.add_argument("--out", default="results/comparison", help="префикс выходных файлов")
    args = ap.parse_args()

    tcp_rows = read_csv(args.tcp)
    rdma_rows = read_csv(args.rdma) if args.rdma else []

    if not tcp_rows and not rdma_rows:
        print("Нет данных для сравнения.", file=sys.stderr)
        sys.exit(1)

    print_comparison_table(tcp_rows, rdma_rows)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    write_merged_csv(tcp_rows, rdma_rows, f"{args.out}.csv")
    try_plot(tcp_rows, rdma_rows, args.out)

    # Серия Д: масштабируемость
    if args.scaling or args.rdma_scaling:
        scaling_rows = read_csv(args.scaling) if args.scaling else []
        rdma_scaling_rows = read_csv(args.rdma_scaling) if args.rdma_scaling else []
        plot_scaling(scaling_rows, rdma_scaling_rows, args.out)


if __name__ == "__main__":
    main()
