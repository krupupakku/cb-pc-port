import struct, zlib

for name, splash_id in [('136.tga', 136), ('137.tga', 137), ('138.tga', 138), ('139.tga', 139), ('140.tga', 140)]:
    with open('original/extracted/assets/' + name, 'rb') as f:
        hdr = f.read(18)
        w, h, bpp, desc = struct.unpack('<HHBB', hdr[12:18])
        raw = f.read(w * min(h, 240) * 2)
        crc = zlib.crc32(raw)
        print(f"Splash {splash_id} ({name}): w={w} h={min(h, 240)} CRC32 = {crc:08X}")

