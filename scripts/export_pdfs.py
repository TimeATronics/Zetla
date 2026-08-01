"""Convert PDF files in test/ to text files in test/txt/"""
import os, sys
from pathlib import Path

def main():
    test_dir = Path("test")
    out_dir = test_dir / "txt"
    out_dir.mkdir(exist_ok=True)

    import fitz  # pymupdf

    for pdf_path in sorted(test_dir.glob("*.pdf")):
        txt_path = out_dir / (pdf_path.stem + ".txt")
        if txt_path.exists():
            print(f"SKIP {pdf_path.name} (already exists)")
            continue

        doc = fitz.open(str(pdf_path))
        text = ""
        for page in doc:
            text += page.get_text()
        doc.close()

        txt_path.write_text(text, encoding="utf-8")
        print(f"OK   {pdf_path.name} -> {len(text)} chars")

if __name__ == "__main__":
    main()
