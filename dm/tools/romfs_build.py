#!/usr/bin/env python3
"""Build an ordinary Switch RomFS image from an extracted directory tree (the format romfs_inventory.py and romfs_image.c read).

    romfs_build.py <source dir> <output image>

Layout: 0x50-byte header, directory hash table, directory table, file hash table, file table, then file payloads (0x10-aligned).
Directory and file records are in depth-first order with names sorted bytewise, so the same tree always gives the same image.
"""
import hashlib, os, struct, sys
from pathlib import Path

NONE = 0xFFFFFFFF
DATA_ALIGN = 0x10
MAX_NAME = 0x300   # romfs_image.c refuses longer names
CHUNK = 8 << 20


def name_hash(parent: int, name: bytes) -> int:
    value = (parent ^ 123456789) & 0xFFFFFFFF
    for byte in name:
        value = (((value >> 5) | (value << 27)) & 0xFFFFFFFF) ^ byte
    return value


def bucket_count(entries: int) -> int:
    if entries < 3:
        return 3
    if entries < 19:
        return entries | 1
    count = entries
    while any(count % p == 0 for p in (2, 3, 5, 7, 11, 13, 17)):
        count += 1
    return count


def align(value: int, to: int) -> int:
    return (value + to - 1) // to * to


class Node:
    __slots__ = ("name", "parent", "dirs", "files", "offset", "sibling", "path")

    def __init__(self, name: bytes, parent, path):
        self.name, self.parent, self.dirs, self.files, self.path = name, parent, [], [], path
        self.offset = self.sibling = None


def scan(root: Path) -> Node:
    top = Node(b"", None, root)
    stack = [top]
    while stack:
        node = stack.pop()
        with os.scandir(node.path) as it:
            entries = sorted(it, key=lambda e: e.name.encode("utf-8"))
        for e in entries:
            raw = e.name.encode("utf-8")
            if not raw or len(raw) > MAX_NAME or b"/" in raw or b"\\" in raw:
                raise ValueError(f"unusable RomFS name: {e.name!r}")
            if e.is_symlink():
                raise ValueError(f"symbolic link not allowed: {e.path}")
            if e.is_dir():
                node.dirs.append(Node(raw, node, Path(e.path)))
            elif e.is_file():
                node.files.append((raw, Path(e.path), e.stat().st_size))
            else:
                raise ValueError(f"not a regular file or directory: {e.path}")
        stack.extend(reversed(node.dirs))   # keeps preorder = sorted order
    return top


def build(src: Path, dest: Path) -> dict:
    root = scan(src)
    dirs, files = [], []       # dirs: Node in preorder; files: (parent Node, name, path, size)

    def walk(node):
        dirs.append(node)
        for d in node.dirs:
            walk(d)
    walk(root)
    for node in dirs:
        for name, path, size in node.files:
            files.append((node, name, path, size))
    # assign table offsets
    pos = 0
    for node in dirs:
        node.offset = pos
        pos += 24 + align(len(node.name), 4)
    dir_table_size = pos
    file_offsets, pos = [], 0
    for _, name, _, _ in files:
        file_offsets.append(pos)
        pos += 32 + align(len(name), 4)
    file_table_size = pos
    # payload offsets (relative to the data region)
    data_rel, pos = [], 0
    for *_, size in files:
        pos = align(pos, DATA_ALIGN)
        data_rel.append(pos)
        pos += size
    data_size = pos
    # hash tables
    ndb, nfb = bucket_count(len(dirs)), bucket_count(len(files))
    dir_heads, file_heads = [NONE] * ndb, [NONE] * nfb
    dir_next, file_next = {}, {}
    for node in dirs:
        parent_off = node.parent.offset if node.parent else 0
        b = name_hash(parent_off, node.name) % ndb
        dir_next[node.offset] = dir_heads[b]; dir_heads[b] = node.offset
    for (node, name, _, _), off in zip(files, file_offsets):
        b = name_hash(node.offset, name) % nfb
        file_next[off] = file_heads[b]; file_heads[b] = off
    # siblings / children
    for node in dirs:
        node.sibling = NONE
    for node in dirs:
        for a, b in zip(node.dirs, node.dirs[1:]):
            a.sibling = b.offset
    first_file, file_sibling = {}, {}
    last = {}
    for (node, name, _, _), off in zip(files, file_offsets):
        if id(node) in last:
            file_sibling[last[id(node)]] = off
        else:
            first_file[id(node)] = off
        last[id(node)] = off
    # serialise metadata
    dir_blob = bytearray()
    for node in dirs:
        child = node.dirs[0].offset if node.dirs else NONE
        dir_blob += struct.pack("<6I", node.parent.offset if node.parent else 0, node.sibling if node.parent else NONE,
                                child, first_file.get(id(node), NONE), dir_next[node.offset], len(node.name))
        dir_blob += node.name + b"\0" * (align(len(node.name), 4) - len(node.name))
    file_blob = bytearray()
    for (node, name, _, size), off, rel in zip(files, file_offsets, data_rel):
        file_blob += struct.pack("<IIQQII", node.offset, file_sibling.get(off, NONE), rel, size, file_next[off], len(name))
        file_blob += name + b"\0" * (align(len(name), 4) - len(name))
    assert len(dir_blob) == dir_table_size and len(file_blob) == file_table_size
    dir_hash = struct.pack(f"<{ndb}I", *dir_heads)
    file_hash = struct.pack(f"<{nfb}I", *file_heads)
    o1 = 0x50
    o2 = o1 + len(dir_hash)
    o3 = o2 + len(dir_blob)
    o4 = o3 + len(file_hash)
    meta_end = o4 + len(file_blob)
    data_off = align(meta_end, 0x200)
    header = struct.pack("<10Q", 0x50, o1, len(dir_hash), o2, len(dir_blob), o3, len(file_hash), o4, len(file_blob), data_off)
    total = data_off + data_size
    digest = hashlib.sha256()
    with open(dest, "wb") as out:
        def put(chunk):
            out.write(chunk); digest.update(chunk)
        put(header); put(dir_hash); put(bytes(dir_blob)); put(file_hash); put(bytes(file_blob)); put(b"\0" * (data_off - meta_end))
        written = 0
        for (_, name, path, size), rel in zip(files, data_rel):
            if rel > written:
                put(b"\0" * (rel - written)); written = rel
            left = size
            with open(path, "rb") as f:
                while left:
                    block = f.read(min(CHUNK, left))
                    if not block:
                        raise IOError(f"{path} shrank while building")
                    put(block); left -= len(block)
            written += size
        if out.tell() != total:
            raise AssertionError("size accounting error")
    return {"bytes": total, "sha256": digest.hexdigest(), "files": len(files), "directories": len(dirs)}


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    r = build(Path(sys.argv[1]), Path(sys.argv[2]))
    print(f"{r['files']} files, {r['directories']} directories, {r['bytes']} bytes, sha256 {r['sha256']}")
