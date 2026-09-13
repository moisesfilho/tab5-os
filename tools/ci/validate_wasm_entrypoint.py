#!/usr/bin/env python3
"""Validate that a Tab5 WASM module exports a supported entrypoint."""

import sys
import argparse
from pathlib import Path


def read_uleb(data, offset):
    value = 0
    shift = 0
    while True:
        if offset >= len(data):
            raise ValueError("truncated unsigned LEB128 value")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte & 0x80 == 0:
            return value, offset
        shift += 7


def exported_names(data):
    if data[:8] != b"\x00asm\x01\x00\x00\x00":
        raise ValueError("invalid WASM header")

    names = set()
    offset = 8
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        section_size, offset = read_uleb(data, offset)
        section_end = offset + section_size
        if section_end > len(data):
            raise ValueError("truncated WASM section")
        if section_id == 7:
            count, offset = read_uleb(data, offset)
            for _ in range(count):
                length, offset = read_uleb(data, offset)
                name_end = offset + length
                if name_end > section_end:
                    raise ValueError("truncated WASM export name")
                name = data[offset:name_end].decode("utf-8")
                offset = name_end
                if offset >= section_end:
                    raise ValueError("truncated WASM export kind")
                offset += 1
                _, offset = read_uleb(data, offset)
                names.add(name)
        offset = section_end
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--require-export", action="append", default=[])
    args = parser.parse_args()
    path = args.wasm
    try:
        names = exported_names(path.read_bytes())
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"[ERROR] {path}: {exc}", file=sys.stderr)
        return 1
    supported = names & {"main", "app_main"}
    if not supported:
        print(f"[ERROR] {path}: missing main/app_main export", file=sys.stderr)
        return 1
    missing = [name for name in args.require_export if name not in names]
    if missing:
        print(f"[ERROR] {path}: missing required export(s): {', '.join(missing)}", file=sys.stderr)
        return 1
    print(f"[OK] {path}: entrypoint(s) {', '.join(sorted(supported))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
