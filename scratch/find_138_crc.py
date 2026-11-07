import zlib

with open('original/extracted/assets/138.tga', 'rb') as f:
    data = f.read()

target_crc = 0x05FE99DD
size = 512 * 240 * 2
for off in range(18, len(data) - size + 1, 2):
    sub = data[off : off + size]
    if zlib.crc32(sub) == target_crc:
        row = (off - 18) // (512 * 2)
        print(f"FOUND at byte offset {off} (row {row}) in 138.tga!")
        break
else:
    print("Not found in 138.tga directly.")

