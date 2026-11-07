import os, glob, struct, zlib

assets_dir = 'original/extracted/assets'
tex_files = sorted(glob.glob(os.path.join(assets_dir, '*.tex')))
tga_files = sorted(glob.glob(os.path.join(assets_dir, '*.tga')))

print(f"Indexing {len(tex_files)} .tex and {len(tga_files)} .tga files...")

# Binary format:
# Magic: "CBTX" (4 bytes)
# Count: uint32
# Entries:
#   crc32: uint32
#   pack_id: uint16 (0..999, or 0xFFFF for splashes)
#   img_idx: uint16 (0..255)
#   width: uint16
#   height: uint16
#   bpp: uint8
#   pad: uint8

entries = []
seen_crcs = {}

for fpath in tex_files:
    fname = os.path.basename(fpath)
    pack_id = int(os.path.splitext(fname)[0])
    with open(fpath, 'rb') as f:
        data = f.read()
    if len(data) < 20: continue
    magic, size, numImages, numCluts, imgOff, clutOff = struct.unpack('<IIHHII', data[:20])
    
    clut_base = 0x10 + clutOff
    cluts_lens = []
    pos = clut_base
    for _ in range(numCluts):
        if pos + 2 > len(data): break
        nc = struct.unpack('<H', data[pos:pos+2])[0]
        cluts_lens.append(nc)
        pos += 2 + nc * 2

    img_pos = 0x0C + imgOff
    for idx in range(numImages):
        if img_pos + 20 > len(data): break
        w_raw, h_raw = struct.unpack('<HH', data[img_pos:img_pos+4])
        pal_idx = struct.unpack('<H', data[img_pos+12:img_pos+14])[0]
        clut_idx = min(pal_idx >> 1, len(cluts_lens) - 1) if cluts_lens else 0
        nc = cluts_lens[clut_idx] if cluts_lens else 16
        bpp = 4 if nc < 17 else 8
        width = (16 // bpp) * w_raw
        height = h_raw
        raw_bytes = data[img_pos+20 : img_pos+20 + w_raw * h_raw * 2]
        crc = zlib.crc32(raw_bytes)
        entries.append((crc, pack_id, idx, width, height, bpp))
        img_pos += 20 + w_raw * h_raw * 2

for fpath in tga_files:
    fname = os.path.basename(fpath)
    splash_id = int(os.path.splitext(fname)[0])
    with open(fpath, 'rb') as f:
        hdr = f.read(18)
        w, h, bpp, desc = struct.unpack('<HHBB', hdr[12:18])
        raw_full = f.read()
    # 1. Full image as on disk
    entries.append((zlib.crc32(raw_full), 0xFFFF, splash_id, w, h, 16))
    # 2. 240 rows vertically flipped (as uploaded to VRAM by Crash Bash game engine)
    row_len = w * 2
    start_row = max(0, (h - 240) // 2)
    flipped = bytearray()
    for r in range(min(h, 240)):
        src_r = start_row + min(h, 240) - 1 - r
        flipped.extend(raw_full[src_r * row_len : (src_r + 1) * row_len])
    crc_flipped = zlib.crc32(flipped)
    entries.append((crc_flipped, 0xFFFF, splash_id, w, 240, 16))
    # 3. 240 rows unflipped
    unflipped = raw_full[start_row * row_len : (start_row + 240) * row_len]
    entries.append((zlib.crc32(unflipped), 0xFFFF, splash_id, w, 240, 16))

print(f"Total indexed entries: {len(entries)}")

# Write textures/textures_index.dat
os.makedirs('textures', exist_ok=True)
out_path = 'textures/textures_index.dat'
with open(out_path, 'wb') as f:
    f.write(b'CBTX')
    f.write(struct.pack('<I', len(entries)))
    for crc, pack_id, img_idx, width, height, bpp in entries:
        f.write(struct.pack('<IHHHHBB', crc, pack_id, img_idx, width, height, bpp, 0))

print(f"Written index to {out_path}, size = {os.path.getsize(out_path)} bytes")

