"""
Extract BGE word embedding matrix from model.onnx -> binary file for C++ lookup.
No ORT inference needed - just reads the first layer's weight tensor.
"""
import os, sys, struct
import onnx
import numpy as np
from pathlib import Path

def main():
    model_dir = Path("Zetla/Zetla/data/src/main/assets/bge_model")
    model_path = model_dir / "model.onnx"

    print("Loading ONNX model...")
    model = onnx.load(str(model_path))

    # Find the embedding weight: shape [30522, 384], name typically "bert.embeddings.word_embeddings.weight"
    embed_weight = None
    embed_shape = None
    for init in model.graph.initializer:
        if "word_embeddings" in init.name.lower() or "embedding" in init.name.lower():
            dims = [d for d in init.dims]
            if len(dims) == 2 and dims[0] > 1000 and dims[1] > 100:
                embed_weight = init
                embed_shape = dims
                print(f"  Found: {init.name} shape={dims}")
                break

    if embed_weight is None:
        # Fall back: find any large 2D tensor
        for init in model.graph.initializer:
            dims = [d for d in init.dims]
            if len(dims) == 2 and dims[0] > 1000 and dims[1] > 100:
                embed_weight = init
                embed_shape = dims
                print(f"  Fallback: {init.name} shape={dims}")
                break

    if embed_weight is None:
        print("ERROR: Could not find embedding matrix")
        sys.exit(1)

    vocab_size, embed_dim = embed_shape

    # Extract raw float data
    raw = embed_weight.raw_data
    data = np.frombuffer(raw, dtype=np.float32).reshape(vocab_size, embed_dim)

    # Normalize each row (L2 norm = 1)
    norms = np.linalg.norm(data, axis=1, keepdims=True)
    norms = np.maximum(norms, 1e-8)
    data = data / norms

    # Write binary file
    out_path = model_dir / "word_embeds.bin"
    with open(out_path, "wb") as f:
        f.write(struct.pack("<ii", vocab_size, embed_dim))
        f.write(data.astype(np.float32).tobytes())

    size_mb = os.path.getsize(out_path) / 1024 / 1024
    print(f"\nExtracted: {vocab_size} tokens × {embed_dim} dim = {size_mb:.1f} MB")
    print(f"Written to: {out_path}")

if __name__ == "__main__":
    main()
