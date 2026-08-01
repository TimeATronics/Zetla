"""Convert SPIR-V binaries to C uint32_t arrays."""
import os, sys

def spv_to_c(path, name):
    with open(path, 'rb') as f:
        data = f.read()
    # Pad to uint32_t alignment
    while len(data) % 4: data += b'\x00'
    words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]
    lines = []
    lines.append(f'// Auto-generated from {os.path.basename(path)}')
    lines.append(f'const uint32_t {name}[] = {{')
    for i in range(0, len(words), 8):
        chunk = ', '.join(f'0x{w:08X}' for w in words[i:i+8])
        lines.append(f'    {chunk},')
    lines.append('};')
    lines.append(f'const size_t {name}_SIZE = sizeof({name});')
    return '\n'.join(lines)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spvs = [
    (os.path.join(ROOT, 'build', 'src', 'exp_o.spv'), 'SPIRV_EXP_O'),
    (os.path.join(ROOT, 'build', 'src', 'inner_product.spv'), 'SPIRV_INNER_PRODUCT'),
]
out = os.path.join(ROOT, 'src', 'hgnfs', 'gpu', 'spirv_data.h')
with open(out, 'w') as f:
    f.write('#pragma once\n#include <cstdint>\n#include <cstddef>\n\n')
    for path, name in spvs:
        f.write(spv_to_c(path, name) + '\n\n')
print(f'Generated: {out} ({os.path.getsize(out)} bytes)')
