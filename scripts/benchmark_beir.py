"""
Benchmark hyperbolic RAG against BEIR dataset (SciFact).
Uses the same pipeline as our C++: BGE word embed -> add_time -> Einstein midpoint -> Lorentz search.
"""
import os, sys, struct, time, math, json
from pathlib import Path
import numpy as np
from collections import defaultdict

#  Download BEIR SciFact 
def download_beir(dataset="scifact"):
    from beir import util
    from beir.datasets.data_loader import GenericDataLoader
    url = f"https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/{dataset}.zip"
    data_path = util.download_and_unzip(url, "beir_data")
    return GenericDataLoader(data_folder=data_path).load(split="test")

#  BGE word embeddings (same as C++) 
def load_bge():
    path = Path("Zetla/Zetla/data/src/main/assets/bge_model/word_embeds.bin")
    with open(path, "rb") as f:
        vocab, dim = struct.unpack("<ii", f.read(8))
        data = np.frombuffer(f.read(), dtype=np.float32).reshape(vocab, dim)
    return data

def load_vocab():
    path = Path("Zetla/Zetla/data/src/main/assets/bge_model/vocab.txt")
    vocab = {}
    with open(path) as f:
        for i, line in enumerate(f):
            vocab[line.strip()] = i
    return vocab

from transformers import AutoTokenizer
tokenizer = AutoTokenizer.from_pretrained("Zetla/Zetla/data/src/main/assets/bge_model")

#  Hyperbolic embedding (same as C++ hyp_embedder) 
def embed_text(text, embed, max_len=512):
    enc = tokenizer(text, padding="max_length", max_length=max_len,
                    truncation=True, return_tensors="np")
    ids = enc["input_ids"][0]
    mask = enc["attention_mask"][0]

    # Collect word embeddings
    points = []
    for i, tid in enumerate(ids):
        if mask[i] == 0: continue
        if tid >= len(embed): continue
        vec = embed[tid]
        # add_time
        n2 = float((vec ** 2).sum())
        t = np.sqrt(n2 + 1.0)
        hyp = np.concatenate([[t], vec])  # [time, x1...xd]
        points.append(hyp)

    if not points:
        return np.zeros(len(embed[0]) + 1)

    # Einstein midpoint
    points = np.array(points)
    weights = points[:, 0]  # Lorentz factor = x0
    weights = weights / weights.sum()
    weighted = (points * weights[:, None]).sum(axis=0)
    # Reproject to hyperboloid
    inner = -weighted[0]**2 + (weighted[1:]**2).sum()
    scale = np.sqrt(1.0 / max(-inner, 1e-8))
    return weighted * scale

def lorentz_inner(a, b):
    return -a[0] * b[0] + (a[1:] * b[1:]).sum()

#  Benchmark 
def main():
    print("Downloading SciFact from BEIR...")
    corpus, queries, qrels = download_beir("scifact")

    print(f"Corpus: {len(corpus)} docs, Queries: {len(queries)}, Qrels: {len(qrels)} entries")

    print("Loading BGE word embeddings...")
    embed = load_bge()
    print(f"  {embed.shape[0]} tokens × {embed.shape[1]} dim")

    # Embed all documents
    print("Embedding documents...")
    t0 = time.time()
    doc_embeds = {}
    doc_ids = list(corpus.keys())
    for i, did in enumerate(doc_ids):
        text = corpus[did]["title"] + " " + corpus[did]["text"]
        doc_embeds[did] = embed_text(text, embed)
        if (i + 1) % 100 == 0:
            print(f"  {i+1}/{len(doc_ids)}")
    print(f"  Embedded {len(doc_ids)} docs in {time.time()-t0:.1f}s")

    # Run queries
    print("Running queries...")
    results = {}
    t0 = time.time()
    query_ids = list(queries.keys())
    for i, qid in enumerate(query_ids):
        q_embed = embed_text(queries[qid], embed)
        scores = {}
        for did, d_embed in doc_embeds.items():
            scores[did] = float(lorentz_inner(q_embed, d_embed))

        # Top-100
        ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)[:100]
        results[qid] = {did: score for did, score in ranked}

    search_time = (time.time() - t0) / len(query_ids)
    print(f"  {len(query_ids)} queries in {time.time()-t0:.1f}s ({search_time*1000:.1f} ms/query)")

    # Evaluate
    print("\n=== Evaluation ===")
    from beir.retrieval.evaluation import EvaluateRetrieval
    evaluator = EvaluateRetrieval()
    ndcg, _map, recall, precision = evaluator.evaluate(qrels, results, [1, 5, 10, 100])

    for metric, values in [("NDCG", ndcg), ("MAP", _map), ("Recall", recall), ("P", precision)]:
        print(f"  {metric}:")
        for k, v in values.items():
            print(f"    @{k}: {v:.4f}")

if __name__ == "__main__":
    main()
