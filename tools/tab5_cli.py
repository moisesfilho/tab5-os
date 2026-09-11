#!/usr/bin/env python3
"""Small NDJSON client for the tab5 serial automation bridge."""
import argparse
import base64
import json
import sys
import itertools
import binascii
import zlib


class Tab5Session:
    _rid_counter = itertools.count(1)

    def __init__(self, transport):
        self.transport = transport

    def exchange(self, command_line):
        if isinstance(command_line, str):
            payload = command_line.encode()
        else:
            payload = bytes(command_line)
        if not payload.endswith(b"\n"):
            payload += b"\n"
        request = None
        try:
            request = json.loads(payload.decode().strip())
        except (ValueError, TypeError, UnicodeDecodeError):
            pass
        self.transport.write(payload)
        frames = []
        is_dump = isinstance(request, dict) and request.get("cmd") in {
            "screen.dump", "screen.dump.retry", "screen.dump.resume"
        }
        expected = None
        chunks = {}
        got_end = False
        while True:
            raw = self.transport.readline()
            if not raw:
                break
            line = raw.decode() if isinstance(raw, (bytes, bytearray)) else raw
            try:
                frame = json.loads(line.strip())
            except (TypeError, ValueError):
                # USB Serial-JTAG shares the console with ESP_LOG.  Console
                # lines are deliberately not protocol errors; only a missing
                # valid frame is an exchange failure.
                continue
            if not isinstance(frame, dict):
                continue
            frames.append(frame)
            if is_dump and frame.get("event") == "start":
                expected = frame.get("chunks")
            if is_dump and isinstance(frame.get("chunk"), int) and "b64" in frame:
                self._merge_chunk(chunks, frame, "")
            if is_dump and frame.get("event") == "end":
                got_end = True
            if frame.get("event") == "end":
                break
            if len(frames) == 1 and frame.get("event") != "start":
                break
        if not frames:
            raise RuntimeError("nenhuma resposta do dispositivo")

        if is_dump and not any(f.get("status") == "error" for f in frames):
            start = next((f for f in frames if f.get("event") == "start"), None)
            if start is None or not isinstance(start.get("size"), int) or not isinstance(start.get("chunks"), int):
                raise RuntimeError("screen.dump: start invalido (size/chunks obrigatorios)")
            expected = start["chunks"]
            if expected < 0 or start["size"] < 0:
                raise RuntimeError("screen.dump: size/chunks invalidos")
            self._validate_chunks(chunks, expected)
            missing = ([i for i in range(expected) if i not in chunks]
                       if isinstance(expected, int) and expected >= 0 else [])
            if missing or not got_end:
                retry_frames = self._retry_dump(request, missing)
                for retry in retry_frames:
                    if retry.get("event") == "start":
                        continue
                    if isinstance(retry.get("chunk"), int) and "b64" in retry:
                        self._merge_chunk(chunks, retry, " na retransmissao")
                    if retry.get("event") == "end":
                        got_end = True
                        frames.append(retry)
                if isinstance(expected, int):
                    missing = [i for i in range(expected) if i not in chunks]
                self._validate_chunks(chunks, expected)
                if missing or not got_end:
                    raise RuntimeError(
                        "screen.dump incompleto: chunks ausentes=%s end=%s" %
                        (missing, got_end))
            end = next((f for f in reversed(frames) if f.get("event") == "end"), None)
            if end is None and got_end:
                # The end may have arrived only in the retry response.  It is
                # retained above; this guard makes the invariant explicit.
                raise RuntimeError("screen.dump: end retransmitido nao preservado")
            if end is not None and end.get("status") == "error":
                raise RuntimeError("screen.dump: end indica erro")
            frames = [start] + [chunks[i] for i in sorted(chunks)] + ([end] if end else [])
            payload = b"".join(base64.b64decode(chunks[i]["b64"], validate=True) for i in range(expected))
            if len(payload) != start["size"]:
                raise RuntimeError("screen.dump: tamanho recebido=%d declarado=%d" % (len(payload), start["size"]))
            if "crc32" in start:
                if not isinstance(start["crc32"], int) or (zlib.crc32(payload) & 0xFFFFFFFF) != start["crc32"]:
                    raise RuntimeError("screen.dump: CRC32 divergente")
        return frames

    @staticmethod
    def _validate_chunks(chunks, expected):
        for index, frame in chunks.items():
            if not isinstance(index, int) or index < 0 or index >= expected:
                raise RuntimeError("screen.dump: indice de chunk invalido %s" % index)
            try:
                decoded = base64.b64decode(frame["b64"], validate=True)
            except (ValueError, binascii.Error, TypeError) as exc:
                raise RuntimeError("screen.dump: Base64 invalido no chunk %s" % index) from exc
            if base64.b64encode(decoded).decode("ascii") != frame["b64"]:
                raise RuntimeError("screen.dump: Base64 nao canonico no chunk %s" % index)

    @staticmethod
    def _merge_chunk(chunks, frame, suffix):
        """Merge a chunk by index, accepting only an identical retransmission.

        A retry commonly repeats the first chunk at the requested boundary.
        Replacing an existing value would let a corrupted retry alter the
        stream, while rejecting every repeat makes legitimate retries fail.
        """
        index = frame.get("chunk")
        if index in chunks:
            previous = chunks[index]
            if previous.get("b64") != frame.get("b64"):
                raise RuntimeError("screen.dump: chunk duplicado conflitante%s %s" % (suffix, index))
            return
        chunks[index] = frame

    def _retry_dump(self, request, missing):
        if not isinstance(request, dict):
            return []
        retry = {"cmd": "screen.dump.retry", "from": min(missing) if missing else 0, "missing": missing}
        for key in ("path", "rid"):
            if key in request:
                retry[key] = request[key]
        self.transport.write((json.dumps(retry, separators=(",", ":")) + "\n").encode())
        result = []
        while True:
            raw = self.transport.readline()
            if not raw:
                break
            try:
                frame = json.loads(raw.decode() if isinstance(raw, bytes) else raw)
            except (TypeError, ValueError, UnicodeDecodeError):
                continue
            if isinstance(frame, dict):
                result.append(frame)
                if frame.get("event") == "end":
                    break
        return result


Session = Tab5Session


def open_session(transport):
    return Tab5Session(transport)


def connect(transport):
    return open_session(transport)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("command", nargs="+")
    args = parser.parse_args(argv)
    try:
        import serial
        transport = serial.Serial(args.port, args.baud, timeout=2)
    except ImportError:
        parser.error("pyserial e necessario para o transporte serial")
    command = args.command
    if command[0] == "app" and len(command) > 1:
        request = {"cmd": "app." + command[1]}
        if len(command) > 2:
            request["id"] = command[2]
        if command[1] == "open" and len(command) > 3:
            request["file"] = command[3]
    elif command[0] == "screenshot":
        request = {"cmd": "screen.dump" if "--download" in command else "screen.shot"}
    elif command[0] == "server" and len(command) > 1:
        request = {"cmd": "server." + command[1]}
    elif command[0] == "click" and len(command) == 3:
        request = {"cmd": "ui.click", "x": int(command[1]), "y": int(command[2])}
    else:
        request = {"cmd": command[0]}
    frames = open_session(transport).exchange(json.dumps(request, separators=(",", ":")))
    if command[0] == "screenshot" and "--download" in command:
        try:
            destination = command[command.index("--download") + 1]
            payload = b"".join(base64.b64decode(frame["b64"])
                                for frame in frames if "b64" in frame)
            with open(destination, "wb") as output:
                output.write(payload)
        except (IndexError, OSError, ValueError, KeyError) as exc:
            parser.error(f"falha ao salvar screenshot: {exc}")
    print("\n".join(json.dumps(frame, ensure_ascii=False) for frame in frames))
    return 0


if __name__ == "__main__":
    sys.exit(main())
