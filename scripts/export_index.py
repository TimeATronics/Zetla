"""Export pipeline: chunk MD files -> BGE embed -> PCA -> save binary for C++."""
import os, sys, struct, time, numpy as np
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hgnfs_py.multimodal.batch_embedder import BatchEmbedder
from hgnfs_py.rag.chunker import chunk_text, read_file_text
from sklearn.decomposition import PCA

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MD_DIR = os.path.join(ROOT, "papers", "md")
OUT_DIR = os.path.join(ROOT, "build")

HYP_DIM = 128
TEST_QUERIES = [
    "exponential map formula for Lorentz model",
    "contrastive learning in hyperbolic space",
    "Fermi Dirac decoder link prediction",
    "numerical stability near boundary",
    "mobius addition gyrovector formula",
    "what is the Busemann function",
    "outward Einstein midpoint pooling",
    "graph convolution in hyperbolic space",
    "Euclidean parametrization trick for optimization",
]

os.makedirs(OUT_DIR, exist_ok=True)

# 1. Load + chunk
print("Loading markdown files...")
chunks = []
md_files = sorted(f for f in os.listdir(MD_DIR) if f.endswith(".md"))
for fname in md_files:
    text = read_file_text(os.path.join(MD_DIR, fname), max_chars=200_000)
    if not text: continue
    for c in chunk_text(text, 300, 60):
        if c.strip(): chunks.append((fname, c))
print(f"  {len(chunks)} chunks from {len(md_files)} files")

# 2. BGE embed
print("BGE embedding...")
embedder = BatchEmbedder(model_name="all-MiniLM-L6-v2", hyp_dim=16, batch_size=256)
texts = [c[1] for c in chunks]
t0 = time.perf_counter()
euc = embedder.model.encode(texts, convert_to_numpy=True, show_progress_bar=True, normalize_embeddings=True, batch_size=256)
print(f"  [{time.perf_counter()-t0:.1f}s] {euc.shape}")

# 3. PCA
print(f"PCA: {euc.shape[1]}D -> {HYP_DIM}D...")
pca = PCA(n_components=HYP_DIM)
Z = pca.fit_transform(euc).astype(np.float32)
norms = np.linalg.norm(Z, axis=1, keepdims=True)
Z *= np.clip(3.0 / norms.clip(1e-8), None, 1.0)
print(f"  variance: {pca.explained_variance_ratio_.sum()*100:.1f}%")

# 4. Embed test queries
print(f"Embedding {len(TEST_QUERIES)} test queries...")
q_euc = embedder.model.encode(TEST_QUERIES, convert_to_numpy=True, show_progress_bar=False, normalize_embeddings=True)
Q = pca.transform(q_euc).astype(np.float32)
qn = np.linalg.norm(Q, axis=1, keepdims=True)
Q *= np.clip(3.0 / qn.clip(1e-8), None, 1.0)

# 5. Save index binary
paths = sorted(set(c[0] for c in chunks))
path_to_id = {p: i for i, p in enumerate(paths)}
index_path = os.path.join(OUT_DIR, "index.bin")
with open(index_path, "wb") as f:
    f.write(struct.pack("iiii", HYP_DIM, len(chunks), HYP_DIM, euc.shape[1]))
    f.write(pca.mean_.astype(np.float32).tobytes())
    f.write(pca.components_.astype(np.float32).tobytes())
    f.write(Z.tobytes())
    f.write(struct.pack("i", len(paths)))
    for p in paths:
        pb = p.encode("utf-8")
        f.write(struct.pack("i", len(pb))); f.write(pb)
    for i, (fname, _) in enumerate(chunks):
        f.write(struct.pack("ii", i, path_to_id[fname]))

# 6. Save query binary
query_path = os.path.join(OUT_DIR, "queries.bin")
with open(query_path, "wb") as f:
    f.write(struct.pack("ii", HYP_DIM, len(TEST_QUERIES)))
    f.write(Q.tobytes())
    for q in TEST_QUERIES:
        qb = q.encode("utf-8")
        f.write(struct.pack("i", len(qb))); f.write(qb)

print(f"\nExported: {os.path.getsize(index_path)/1024:.0f} KB index, "
      f"{os.path.getsize(query_path)/1024:.0f} KB queries")
print(f"  dim={HYP_DIM}  chunks={len(chunks)}  files={len(paths)}  queries={len(TEST_QUERIES)}")
