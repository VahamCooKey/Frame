"""Regenerate small synthetic track-selection fixtures; requires ffmpeg on PATH."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1] / "assets" / "test"
AUDIO = [
    ("eng", "English", "default"), ("spa", "Spanish", "0"),
    ("fra", "French", "0"), ("hin", "Hindi", "0"),
    ("tam", "Tamil", "0"), ("deu", "German", "0"),
    ("jpn", "Japanese", "0"), ("jpn", "Japanese commentary", "comment"),
    ("kor", "Korean", "0"), ("ben", "Bengali", "0"),
]
SUBS = [
    ("eng", "English", "default"), ("eng", "English forced", "forced"),
    ("spa", "Spanish", "0"), ("fra", "French", "0"),
    ("hin", "Hindi", "0"), ("tam", "Tamil", "0"),
    ("jpn", "Japanese", "0"), ("deu", "German", "0"),
    ("eng", "English SDH", "hearing_impaired"), ("ben", "Bengali", "0"),
]


def generate(name, audio, subtitles, srt):
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "color=c=0x383838:s=64x36:r=1:d=12",
            "-f", "lavfi", "-i", "anullsrc=r=16000:cl=mono",
            "-i", str(srt), "-map", "0:v"]
    for _ in audio:
        args += ["-map", "1:a"]
    for _ in subtitles:
        args += ["-map", "2:s"]
    args += ["-t", "12", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "40",
             "-threads:v", "1", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "16k", "-c:s", "srt"]
    for kind, tracks in [("a", audio), ("s", subtitles)]:
        for index, (language, title, disposition) in enumerate(tracks):
            args += [f"-metadata:s:{kind}:{index}", f"language={language}",
                     f"-metadata:s:{kind}:{index}", f"title={title}",
                     f"-disposition:{kind}:{index}", disposition]
    target = ROOT / name
    subprocess.run(args + [str(target)], check=True)
    print(f"{target.name}: {target.stat().st_size} bytes")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="frame-track-fixtures-") as folder:
        srt = Path(folder) / "captions.srt"
        srt.write_text("1\n00:00:00,000 --> 00:00:11,800\nFrame synthetic track test\n", encoding="utf-8")
        generate("track-choice-a.mkv", AUDIO, SUBS, srt)
        generate("track-choice-b.mkv", [AUDIO[i] for i in [3, 2, 6, 1, 4, 5, 0, 9, 7, 8]],
                 [SUBS[i] for i in [1, 8, 4, 3, 6, 7, 0, 9, 2, 5]], srt)
        generate("track-choice-fallback.mkv", AUDIO[:1], SUBS[:1], srt)
