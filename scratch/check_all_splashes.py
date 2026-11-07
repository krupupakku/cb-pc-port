import struct, zlib

row_len = 512 * 2

for splash_id in [136, 137, 138, 139, 140]:
    tga_path = f'original/extracted/assets/{splash_id}.tga'
    with open(tga_path, 'rb') as f:
        tga = f.read()
    hdr = tga[:18]
    w, h, bpp, desc = struct.unpack('<HHBB', hdr[12:18])
    tga_data = tga[18:]
    
    # Check start_row for 240 rows flipped
    start_row = (h - 240) // 2
    flipped = bytearray()
    for r in range(240):
        src_r = start_row + 239 - r
        flipped.extend(tga_data[src_r * row_len : (src_r + 1) * row_len])
    
    crc = zlib.crc32(flipped)
    print(f"Splash {splash_id} (h={h}): start_row={start_row}, uploaded CRC32 = {crc:08X}")

