"""
Usage: uv run python scripts/beir_bench.py scifact

Exports BEIR -> C++ benchmark -> computes metrics.
"""
import sys, os, subprocess, struct, json
from pathlib import Path
from collections import defaultdict

DATASET = sys.argv[1] if len(sys.argv) > 1 else "scifact"
OUT_DIR = Path(f"beir_bench/{DATASET}")
BENCH_EXE = Path("build/src/beir_bench.exe")

# Step 1: Export BEIR
print("Step 1: Exporting BEIR data...")
os.system(f"{sys.executable} scripts/export_beir.py {DATASET}")

# Step 2: Build benchmark
print("\nStep 2: Building C++ benchmark...")
os.chdir(Path(__file__).parent.parent)
result = subprocess.run(["mingw32-make", f"build/src/beir_bench.exe"], capture_output=True, text=True)
if result.returncode != 0:
    print("Build failed:", result.stderr[-500:])
    sys.exit(1)

# Step 3: Run C++ benchmark
print("\nStep 3: Running C++ benchmark...")
import subprocess, tempfile
env = os.environ.copy()
env["PATH"] = f"build/src;vcpkg_installed/x64-mingw-dynamic/bin;{env.get('PATH','')}"

out_file = tempfile.mktemp(suffix=".txt")
err_file = tempfile.mktemp(suffix=".txt")

proc = subprocess.run(
    [str(BENCH_EXE), str(OUT_DIR)],
    capture_output=True, text=True, timeout=600, env=env
)
print(proc.stderr[:1000])  # logs
results_text = proc.stdout

# Step 4: Parse results and compute metrics
print("\nStep 4: Computing metrics...")
from pytrec_eval import RelevanceEvaluator

qrels = defaultdict(dict)
with open(OUT_DIR / "qrels.tsv") as f:
    for line in f:
        qid, did, score = line.strip().split("\t")
        qrels[qid][did] = int(score)

# Parse C++ output: qid\tdid\tscore
results = defaultdict(dict)
for line in results_text.strip().split("\n"):
    if not line.strip(): continue
    parts = line.strip().split("\t")
    if len(parts) == 3:
        qid, did, score = parts
        results[qid][did] = float(score)

evaluator = RelevanceEvaluator(qrels, {"ndcg_cut.1", "ndcg_cut.5", "ndcg_cut.10",
                                        "recall.5", "recall.10", "recall.100",
                                        "P.5", "P.10"})
metrics = evaluator.evaluate(results)

# Aggregate
agg = defaultdict(list)
for qid, m in metrics.items():
    for k, v in m.items():
        agg[k].append(v)

print("\n=== BEIR Results (C++ Pipeline) ===")
for k in sorted(agg.keys()):
    vals = agg[k]
    mean_v = sum(vals) / len(vals)
    print(f"  {k}: {mean_v:.4f}  (min={min(vals):.4f}, max={max(vals):.4f})")
