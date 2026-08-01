"""Export BEIR dataset to simple text format for C++ benchmark."""
import sys, os, json
from pathlib import Path

dataset = sys.argv[1] if len(sys.argv) > 1 else "scifact"
out_dir = Path(f"beir_bench/{dataset}")
out_dir.mkdir(parents=True, exist_ok=True)

from beir import util
from beir.datasets.data_loader import GenericDataLoader
url = f"https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/{dataset}.zip"
data_path = util.download_and_unzip(url, "beir_data")
corpus, queries, qrels = GenericDataLoader(data_folder=data_path).load(split="test")

# Export corpus: one doc per line: ID\tTITLE\tTEXT
with open(out_dir / "corpus.tsv", "w", encoding="utf-8") as f:
    for did, doc in corpus.items():
        title = doc.get("title", "").replace("\t", " ").replace("\n", " ")
        text = doc.get("text", "").replace("\t", " ").replace("\n", " ")
        f.write(f"{did}\t{title}\t{text}\n")

# Export queries: one per line: ID\tQUERY
with open(out_dir / "queries.tsv", "w", encoding="utf-8") as f:
    for qid, query in queries.items():
        q = query.replace("\t", " ").replace("\n", " ")
        f.write(f"{qid}\t{q}\n")

# Export qrels: query_id\tdoc_id\tscore
with open(out_dir / "qrels.tsv", "w") as f:
    for qid, docs in qrels.items():
        for did, score in docs.items():
            f.write(f"{qid}\t{did}\t{score}\n")

print(f"Exported to {out_dir}/")
print(f"  corpus: {len(corpus)} docs")
print(f"  queries: {len(queries)}")
print(f"  qrels: {sum(len(d) for d in qrels.values())} judgments")
