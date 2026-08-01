"""
Export BAAI/bge-small-en-v1.5 to ONNX for Android on-device embedding.
Uses optimum-cli for reliable export.

Output:
  - model.onnx         - BERT model
  - vocab.txt          - WordPiece vocabulary for C++ tokenizer
"""

import os, subprocess, sys

MODEL_ID = "BAAI/bge-small-en-v1.5"
OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "Zetla", "Zetla", "data", "src", "main", "assets", "bge_model"
)

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # Save vocabulary
    print("Loading tokenizer ...")
    from transformers import AutoTokenizer
    tokenizer = AutoTokenizer.from_pretrained(MODEL_ID)
    vocab = tokenizer.get_vocab()
    sorted_vocab = sorted(vocab.items(), key=lambda x: x[1])
    vocab_path = os.path.join(OUT_DIR, "vocab.txt")
    with open(vocab_path, "w", encoding="utf-8") as f:
        for token, _ in sorted_vocab:
            f.write(token + "\n")
    print(f"  vocab.txt: {len(sorted_vocab)} tokens")
    tokenizer.save_pretrained(OUT_DIR)

    # Export ONNX via optimum-cli
    onnx_path = os.path.join(OUT_DIR, "model.onnx")
    print(f"Exporting {MODEL_ID} to ONNX via optimum-cli ...")
    result = subprocess.run([
        sys.executable, "-m", "optimum.exporters.onnx.__main__",
        "-m", MODEL_ID,
        "--task", "default",
        "--opset", "14",
        "--framework", "pt",
        onnx_path.replace(".onnx", ""),
    ], capture_output=True, text=True, check=False)

    if result.returncode != 0:
        print("Export failed:", result.stderr)
        # Try the CLI directly
        result2 = subprocess.run([
            "optimum-cli", "export", "onnx",
            "-m", MODEL_ID,
            "--task", "default",
            "--opset", "14",
            onnx_path.replace(".onnx", ""),
        ], capture_output=True, text=True, check=False)
        if result2.returncode != 0:
            print("optimum-cli also failed:", result2.stderr)
        else:
            print(result2.stdout)

    # Check result
    if os.path.exists(onnx_path):
        size_mb = os.path.getsize(onnx_path) / 1024 / 1024
        print(f"  model.onnx: {size_mb:.1f} MB")
    else:
        # Check if exported to a subfolder
        model_dir = onnx_path.replace(".onnx", "")
        possible = os.path.join(model_dir, "model.onnx")
        if os.path.exists(possible):
            os.rename(possible, onnx_path)
            size_mb = os.path.getsize(onnx_path) / 1024 / 1024
            print(f"  model.onnx: {size_mb:.1f} MB")
        else:
            print("ERROR: model.onnx not found")
            for root, dirs, files in os.walk(model_dir):
                for f in files:
                    print(f"  Found: {os.path.join(root, f)}")
            sys.exit(1)

    # Verify
    print("Verifying ...")
    import onnxruntime as ort
    import numpy as np
    session = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    inputs = [i.name for i in session.get_inputs()]
    outputs = [o.name for o in session.get_outputs()]
    print(f"  Inputs: {inputs}")
    print(f"  Outputs: {outputs}")

    test = "This is a test sentence for embedding."
    enc = tokenizer(test, return_tensors="np", padding="max_length",
                    max_length=64, truncation=True)
    feed = {n: enc[n].astype(np.int64) for n in inputs if n in enc}
    out = session.run(None, feed)
    print(f"  Output[0] shape: {out[0].shape}")

    # Compare with PyTorch
    import torch
    from transformers import AutoModel
    model = AutoModel.from_pretrained(MODEL_ID)
    model.eval()
    with torch.no_grad():
        pt_enc = {k: torch.tensor(v) for k, v in enc.items()}
        pt_out = model(**pt_enc)
        pt_vec = pt_out.last_hidden_state[:, 0, :].numpy()
    onnx_vec = out[0][:, 0, :]
    diff = np.max(np.abs(onnx_vec - pt_vec))
    print(f"  Max diff vs PyTorch: {diff:.6f}")

    print(f"\nDone. Files in {OUT_DIR}:")
    for f in sorted(os.listdir(OUT_DIR)):
        path = os.path.join(OUT_DIR, f)
        size = os.path.getsize(path)
        print(f"  {f}: {size:,} bytes")

if __name__ == "__main__":
    main()
