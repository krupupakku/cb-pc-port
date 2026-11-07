import zlib

with open('original/extracted/assets/139.tga', 'rb') as f:
    data = f.read()

# Let's search data for a 512*240*2 slice that has CRC FE0713A6!
target_crc = 0xFE0713A6
print('Searching 139.tga...')
size = 512 * 240 * 2
for off in range(0, len(data) - size + 1, 2):
    sub = data[off : off + size]
    if zlib.crc32(sub) == target_crc:
        print(f"FOUND at offset {off} in 139.tga!")
        break
else:
    print("Not found in 139.tga directly.")

