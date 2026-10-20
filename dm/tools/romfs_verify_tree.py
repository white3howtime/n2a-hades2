#!/usr/bin/env python3
"""Check that a RomFS image matches a directory tree: same file set, same sizes, same SHA-256 for every file.

    romfs_verify_tree.py <image> <directory>

The image is parsed by romfs_inventory.py's independent reader, which also checks the tree, hash tables and payload ranges.
"""
import hashlib, os, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfs_inventory import ImageReader


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(8 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    image, root = Path(sys.argv[1]), Path(sys.argv[2])
    reader = ImageReader(image)
    try:
        disk = {}
        for r, _, fs in os.walk(root):
            for f in fs:
                p = Path(r) / f
                disk[p.relative_to(root).as_posix()] = p
        if set(disk) != set(reader.paths):
            sys.exit(f"file sets differ: only on disk {sorted(set(disk) - set(reader.paths))[:5]}, only in image {sorted(set(reader.paths) - set(disk))[:5]}")
        bad = 0
        for name, p in sorted(disk.items()):
            entry = reader.find_file(name)
            if entry is None or entry["size"] != p.stat().st_size or reader.file_digest(entry) != file_sha256(p):
                bad += 1; print("MISMATCH", name)
        if bad:
            sys.exit(f"{bad} files differ")
        print(f"{len(disk)} files identical, {len(reader.directory_paths) - 1} directories, image {image.stat().st_size} bytes")
    finally:
        reader.close()


if __name__ == "__main__":
    main()
