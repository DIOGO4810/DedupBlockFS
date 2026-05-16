import json
import pathlib
import statistics
import sys

MODE = sys.argv[1] if len(sys.argv) > 1 else "writes"
RESULTS_DIR = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path("./results")
PREFIX = "write" if MODE == "writes" else "read"


def extract_metrics(prefix):
    iops_values = []
    bw_values = []

    for file in sorted(RESULTS_DIR.glob(f"{prefix}_*.json")):
        with open(file) as f:
            data = json.load(f)

        job = data["jobs"][0]
        stats = job["read"] if prefix == "read" else job["write"]

        iops_values.append(stats["iops"])
        bw_values.append(stats["bw_bytes"] / (1024 * 1024))

    return iops_values, bw_values


def print_stats(prefix):
    iops, bw = extract_metrics(prefix)
    if not iops:
        return False

    print(f"=== {prefix.upper()} ===")
    print(f"IOPS mean   : {statistics.mean(iops):.2f}")
    print(f"IOPS stddev : {statistics.stdev(iops):.2f}" if len(iops) > 1 else "IOPS stddev : 0.00")
    print(f"BW mean     : {statistics.mean(bw):.2f} MiB/s")
    print(f"BW stddev   : {statistics.stdev(bw):.2f} MiB/s" if len(bw) > 1 else "BW stddev   : 0.00 MiB/s")
    print()
    return True


if not print_stats(PREFIX):
    raise SystemExit(f"No fio result files found for mode={MODE}")
