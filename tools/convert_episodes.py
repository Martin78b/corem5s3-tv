#!/usr/bin/env python3
"""
CoreM5S3 TV Episode Converter

Converts video files (mp4, mkv, avi, etc.) into the .mjpeg + .pcm format
for playback on the M5Stack CoreS3 SE Simpsons TV.

Usage:
    python3 convert_episodes.py --input /path/to/episodes --output /path/to/sdcard
    python3 convert_episodes.py --input ./videos --output ./sdcard --fps 20 --quality 8
    python3 convert_episodes.py --single episode.mp4 --output ./sdcard

Requirements:
    - ffmpeg (must be installed and in PATH)
    - Python 3.7+
"""

import argparse
import os
import re
import subprocess
import sys
import struct
import tempfile
from pathlib import Path
from typing import Optional


# ── Default settings ──
DEFAULT_FPS = 15
DEFAULT_QUALITY = 8  # JPEG quality 1-31, lower = better
DEFAULT_WIDTH = 320
DEFAULT_HEIGHT = 240
DEFAULT_AUDIO_RATE = 44100

MJPEG_EXT = ".mjpeg"
PCM_EXT = ".pcm"

# ── Episode name parsing ──
EPISODE_PATTERNS = [
    re.compile(r"[sS](\d{1,2})[eE](\d{1,2})"),            # S01E01, s1e1
    re.compile(r"(\d{1,2})x(\d{1,2})"),                      # 1x01
    re.compile(r"Season[._ ](\d{1,2})[._ ]Episode[._ ](\d{1,2})", re.IGNORECASE),
    re.compile(r"(\d{1,2})-(\d{1,2})"),                      # 03-05, 04-12
    re.compile(r"(\d{1,2})[._ ](\d{1,2})\s"),               # 1 01 (loose)
]


def parse_episode_info(filename: str) -> Optional[tuple]:
    """Extract (season, episode, title) from filename."""
    name = Path(filename).stem
    for pattern in EPISODE_PATTERNS:
        m = pattern.search(name)
        if m:
            season = int(m.group(1))
            episode = int(m.group(2))
            title = pattern.sub("", name).strip("._- ")
            return (season, episode, title)
    return None


def generate_output_name(filename: str, index: int) -> tuple:
    """Generate output filenames for video and audio."""
    info = parse_episode_info(filename)
    if info:
        season, episode, title = info
        base = f"S{season:02d}E{episode:02d}"
        if title:
            safe_title = re.sub(r'[^\w\s-]', '', title).strip().replace(' ', '_')[:40]
            base += f"_{safe_title}"
    else:
        base = f"EPISODE_{index:03d}"

    return (f"{base}{MJPEG_EXT}", f"{base}{PCM_EXT}")


# ── Conversion functions ──
def convert_to_mjpeg(
    input_path: str,
    output_path: str,
    fps: int,
    quality: int,
    width: int,
    height: int,
) -> bool:
    """
    Convert a video file to MJPEG format.
    Output: [4-byte frame size][JPEG bytes]... (sequential)
    """
    print(f"  Converting video: {input_path} -> {output_path}")
    print(f"    Settings: {width}x{height} @ {fps}fps, JPEG quality={quality}")

    temp_dir = tempfile.mkdtemp()
    temp_pattern = os.path.join(temp_dir, "frame_%06d.jpg")

    try:
        cmd = [
            "ffmpeg", "-y",
            "-i", input_path,
            "-c:v", "mjpeg",
            "-q:v", str(quality),
            "-vf", f"fps={fps},scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black",
            "-an",
            temp_pattern,
        ]
        print(f"    Running: {' '.join(cmd)}")
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=3600
        )
        if result.returncode != 0:
            print(f"    ERROR: ffmpeg failed:\n{result.stderr[:500]}")
            return False

        frame_files = sorted(
            [f for f in os.listdir(temp_dir) if f.endswith(".jpg")]
        )
        if not frame_files:
            print("    ERROR: No frames produced")
            return False

        total_frames = len(frame_files)
        print(f"    Extracted {total_frames} frames")

        with open(output_path, "wb") as out:
            for i, frame_file in enumerate(frame_files):
                frame_path = os.path.join(temp_dir, frame_file)
                with open(frame_path, "rb") as f:
                    jpeg_data = f.read()

                frame_size = len(jpeg_data)
                out.write(struct.pack("<I", frame_size))
                out.write(jpeg_data)

                if frame_size & 1:
                    out.write(b"\x00")

                if (i + 1) % 500 == 0 or (i + 1) == total_frames:
                    print(f"    Progress: {i + 1}/{total_frames} frames")

        file_size_mb = os.path.getsize(output_path) / (1024 * 1024)
        duration_s = total_frames / fps
        print(f"    Done: {file_size_mb:.1f} MB, {duration_s:.0f}s ({total_frames} frames)")
        return True

    except subprocess.TimeoutExpired:
        print("    ERROR: ffmpeg timed out (video too long?)")
        return False
    except FileNotFoundError:
        print("    ERROR: ffmpeg not found. Install ffmpeg first.")
        return False
    except Exception as e:
        print(f"    ERROR: {e}")
        return False
    finally:
        for f in os.listdir(temp_dir):
            os.remove(os.path.join(temp_dir, f))
        os.rmdir(temp_dir)


def convert_to_pcm(
    input_path: str,
    output_path: str,
    sample_rate: int,
) -> bool:
    """
    Convert audio track to raw PCM.
    Output: raw 16-bit signed mono PCM samples.
    """
    print(f"  Converting audio: {input_path} -> {output_path}")
    print(f"    Settings: {sample_rate} Hz, 16-bit, mono")

    try:
        cmd = [
            "ffmpeg", "-y",
            "-i", input_path,
            "-vn",
            "-ar", str(sample_rate),
            "-ac", "1",
            "-sample_fmt", "s16",
            "-f", "s16le",
            output_path,
        ]
        print(f"    Running: {' '.join(cmd)}")
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=3600
        )
        if result.returncode != 0:
            print(f"    ERROR: ffmpeg failed:\n{result.stderr[:500]}")
            return False

        file_size_mb = os.path.getsize(output_path) / (1024 * 1024)
        duration_s = os.path.getsize(output_path) / (sample_rate * 2)
        print(f"    Done: {file_size_mb:.1f} MB, {duration_s:.0f}s")
        return True

    except subprocess.TimeoutExpired:
        print("    ERROR: ffmpeg timed out")
        return False
    except FileNotFoundError:
        print("    ERROR: ffmpeg not found")
        return False
    except Exception as e:
        print(f"    ERROR: {e}")
        return False


def convert_single(
    input_path: str,
    output_dir: str,
    fps: int,
    quality: int,
    width: int,
    height: int,
    audio_rate: int,
    index: int,
    dry_run: bool,
) -> bool:
    """Convert a single video file."""
    filename = os.path.basename(input_path)
    video_name, audio_name = generate_output_name(filename, index)

    video_path = os.path.join(output_dir, video_name)
    audio_path = os.path.join(output_dir, audio_name)

    print(f"\n{'='*60}")
    print(f"Processing: {filename}")
    print(f"  Output:   {video_name}, {audio_name}")
    print(f"{'='*60}")

    if dry_run:
        print("  [DRY RUN - skipping conversion]")
        return True

    os.makedirs(output_dir, exist_ok=True)

    ok = convert_to_mjpeg(input_path, video_path, fps, quality, width, height)
    if ok:
        ok = convert_to_pcm(input_path, audio_path, audio_rate)

    if ok:
        video_size = os.path.getsize(video_path) / (1024 * 1024)
        audio_size = os.path.getsize(audio_path) / (1024 * 1024)
        print(f"\n  ✓ Success: {video_name} ({video_size:.1f} MB) + {audio_name} ({audio_size:.1f} MB)")
    else:
        print(f"\n  ✗ Failed: {filename}")

    return ok


def scan_directory(input_dir: str) -> list:
    """Scan directory for video files."""
    video_exts = {
        ".mp4", ".mkv", ".avi", ".mov", ".m4v",
        ".wmv", ".flv", ".webm", ".ts", ".mpeg",
        ".mpg", ".3gp",
    }
    files = []
    for f in sorted(os.listdir(input_dir)):
        ext = os.path.splitext(f)[1].lower()
        if ext in video_exts:
            files.append(os.path.join(input_dir, f))
    return files


def estimate_storage(files: list, fps: int, quality: int, width: int, height: int) -> None:
    """Estimate total storage needed."""
    total_duration = 0
    total_count = len(files)
    for f in files:
        try:
            cmd = [
                "ffprobe", "-v", "error",
                "-show_entries", "format=duration",
                "-of", "default=noprint_wrappers=1:nokey=1",
                f,
            ]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if result.returncode == 0 and result.stdout.strip():
                total_duration += float(result.stdout.strip())
        except:
            pass

    est_bitrate = width * height * fps * quality * 0.15 / 1000000  # rough MB/s
    est_video_mb = est_bitrate * total_duration
    est_audio_mb = total_duration * DEFAULT_AUDIO_RATE * 2 / (1024 * 1024)

    print(f"\n{'='*60}")
    print(f"Storage Estimate ({total_count} files, {total_duration:.0f}s total):")
    print(f"  Video:  ~{est_video_mb:.0f} MB")
    print(f"  Audio:  ~{est_audio_mb:.0f} MB")
    print(f"  Total:  ~{est_video_mb + est_audio_mb:.0f} MB")
    print(f"  SD card needed: ~{(est_video_mb + est_audio_mb) / 1024 + 1:.0f} GB")
    print(f"{'='*60}\n")


# ── Main ──
def main():
    parser = argparse.ArgumentParser(
        description="CoreM5S3 TV Episode Converter",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --input ./videos --output /Volumes/SDCARD
  %(prog)s --single episode.mp4 --output ./sdcard
  %(prog)s --input ./videos --output ./sdcard --fps 15 --quality 10
  %(prog)s --input ./videos --dry-run
        """,
    )

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--input", "-i", help="Input directory of video files")
    group.add_argument("--single", "-s", help="Single video file to convert")

    parser.add_argument("--output", "-o", default="./output", help="Output directory (default: ./output)")
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS, help=f"Target FPS (default: {DEFAULT_FPS})")
    parser.add_argument("--quality", "-q", type=int, default=DEFAULT_QUALITY,
                        help=f"JPEG quality 1-31, lower=better (default: {DEFAULT_QUALITY})")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH, help=f"Output width (default: {DEFAULT_WIDTH})")
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT, help=f"Output height (default: {DEFAULT_HEIGHT})")
    parser.add_argument("--audio-rate", type=int, default=DEFAULT_AUDIO_RATE,
                        help=f"Audio sample rate (default: {DEFAULT_AUDIO_RATE})")
    parser.add_argument("--dry-run", "-n", action="store_true", help="Scan and estimate only, don't convert")
    parser.add_argument("--shuffle", action="store_true", help="Shuffle output filenames")
    parser.add_argument("--start-index", type=int, default=0, help="Starting index for unnamed episodes")

    args = parser.parse_args()

    # Check ffmpeg
    try:
        subprocess.run(["ffmpeg", "-version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ERROR: ffmpeg not found. Install it:")
        print("  macOS: brew install ffmpeg")
        print("  Ubuntu: sudo apt install ffmpeg")
        print("  Windows: choco install ffmpeg")
        sys.exit(1)

    if args.single:
        if not os.path.isfile(args.single):
            print(f"ERROR: File not found: {args.single}")
            sys.exit(1)
        files = [args.single]
    else:
        if not os.path.isdir(args.input):
            print(f"ERROR: Directory not found: {args.input}")
            sys.exit(1)
        files = scan_directory(args.input)
        if not files:
            print(f"ERROR: No video files found in {args.input}")
            sys.exit(1)

    print(f"\CoreM5S3 TV Episode Converter")
    print(f"{'='*60}")
    print(f"Found {len(files)} video file(s)")
    print(f"Output: {args.output}")

    estimate_storage(files, args.fps, args.quality, args.width, args.height)

    success = 0
    fail = 0

    for i, f in enumerate(files):
        ok = convert_single(
            input_path=f,
            output_dir=args.output,
            fps=args.fps,
            quality=args.quality,
            width=args.width,
            height=args.height,
            audio_rate=args.audio_rate,
            index=args.start_index + i,
            dry_run=args.dry_run,
        )
        if ok:
            success += 1
        else:
            fail += 1

    print(f"\n{'='*60}")
    print(f"Summary: {success} converted, {fail} failed")
    print(f"Output directory: {os.path.abspath(args.output)}")
    print(f"{'='*60}\n")
    print("Next steps:")
    print(f"  1. Copy all .mjpeg and .pcm files to the root of a FAT32 microSD card")
    print(f"  2. Insert SD card into M5Stack CoreS3 SE")
    print(f"  3. Power on - the TV will start playing automatically!")
    print()


if __name__ == "__main__":
    main()
