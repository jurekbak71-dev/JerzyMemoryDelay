"""Convert a P6 RGB test capture to PNG using only Python's standard library."""
from pathlib import Path
import struct, sys, zlib

def convert(source, destination):
    magic, size, maximum, pixels = Path(source).read_bytes().split(b'\n', 3)
    if magic != b'P6' or maximum != b'255':
        raise ValueError('Expected an 8-bit P6 RGB image')
    width, height = map(int, size.split())
    if len(pixels) != width * height * 3:
        raise ValueError('Truncated image')
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    rows = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    Path(destination).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
