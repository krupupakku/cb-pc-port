import struct

with open('build/upload_138.bin', 'rb') as f:
    up = f.read()

# Let's search inside original/extracted/assets/138.tga
with open('original/extracted/assets/138.tga', 'rb') as f:
    tga = f.read()

# Let's see if 138.tga was flipped vertically!
# TGA has 256 rows, up has 240 rows.
# What if up rows are rows 255..16 of TGA, or rows 239..0 of TGA?
tga_data = tga[18:]
row_len = 512 * 2

# Check inverted rows:
for start_row in range(256 - 240 + 1):
    flipped = bytearray()
    for r in range(240):
        src_r = start_row + 239 - r
        flipped.extend(tga_data[src_r * row_len : (src_r + 1) * row_len])
    if flipped == up:
        print(f"EXACT MATCH FLIPPED with start_row={start_row}!")
        break
else:
    print("Not exact match flipped.")

# What if it's unflipped with start_row?
for start_row in range(256 - 240 + 1):
    unflipped = tga_data[start_row * row_len : (start_row + 240) * row_len]
    if unflipped == up:
        print(f"EXACT MATCH UNFLIPPED with start_row={start_row}!")
        break
else:
    print("Not exact match unflipped.")

