"""Export the canonical, user-edited SVG as a Windows multi-size icon."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, required=True)
    args = parser.parse_args()
    source = ROOT / "assets/branding/concepts/frame-player-icon-v1.svg"
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    preview = ROOT / "build-ui/app-icon"
    preview.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.renderer.resolve()), str(source), str(preview / "frame"),
                    *map(str, SIZES)], check=True)
    entries, payloads = [], []
    offset = 6 + len(SIZES) * 16
    for size in SIZES:
        png = (preview / f"frame-{size}.png").read_bytes()
        assert png[:8] == b"\x89PNG\r\n\x1a\n"
        assert struct.unpack(">II", png[16:24]) == (size, size)
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                                   1, 32, len(png), offset))
        payloads.append(png)
        offset += len(png)
    icon = ROOT / "assets/app.ico"
    icon.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + b"".join(entries + payloads))
    (ROOT / "assets/branding/app-icon.json").write_text(json.dumps({
        "source": "concepts/frame-player-icon-v1.svg", "sourceSha256": source_hash,
        "iconSha256": hashlib.sha256(icon.read_bytes()).hexdigest(), "sizes": SIZES
    }, indent=2) + "\n", encoding="utf-8")
    assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
    print("Exported application ICO from unchanged canonical SVG:", source_hash)


if __name__ == "__main__":
    main()
