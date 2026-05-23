# UDF 2.50 BD-ROM Creator for macOS

Create PS4-compatible Blu-ray disc images on macOS without any third-party tools like ImgBurn.

## Requirements

- macOS with Xcode command-line tools (`xcode-select --install`)
- A BD-R disc and Blu-ray burner (for burning)

## Quick Start

```bash
# Build mkudffs (only needed once)
cd src && make && cd ..

# Create a UDF 2.50 image with Metadata Partition
src/mkudffs --new-file --udfrev=2.50 --blocksize=2048 --label="BLURAY" output.iso 10000000

# Mount, populate with BDMV content, unmount
hdiutil attach -nobrowse output.iso
cp -R /path/to/BDMV /Volumes/BLURAY/
cp -R /path/to/CERTIFICATE /Volumes/BLURAY/
hdiutil detach /dev/diskN

# Burn to BD-R
hdiutil burn output.iso
```

## mkudffs

A macOS port of Linux [udftools](https://github.com/pali/udftools) `mkudffs` with added **Metadata Partition** support for UDF 2.50+.

### Features

- Creates UDF filesystems from version 1.01 to 2.60
- **UDF 2.50+ automatically includes a Type 2 Metadata Partition Map** (required by BD-ROM spec)
- Works with image files and macOS block devices
- Supports all media types: HD, DVD, DVD-RAM, DVD-RW, CD-RW, BD-R

### Usage

```bash
# UDF 2.50 with Metadata Partition (automatic for rev >= 2.50)
src/mkudffs --new-file --udfrev=2.50 --blocksize=2048 --label="DISC" image.iso 10000000

# UDF 2.01 (general purpose, writable)
src/mkudffs --new-file --blocksize=2048 --label="MyDisk" image.img 1024

# Dry-run (simulate without writing)
src/mkudffs --no-write --blocksize=2048 image.img

# Write to pre-allocated file
dd if=/dev/zero of=image.img bs=2048 count=4096
src/mkudffs --blocksize=2048 --label="MyDisk" image.img
```

### Build

```bash
cd src && make
```

No autotools or third-party dependencies — just Xcode command-line tools.

## Alternative: bdrom_udf250.py

A standalone Python script that creates BD-ROM images by writing UDF 2.50 structures directly (no mounting needed). Use this if you want to avoid the mount/copy/unmount workflow.

```bash
python3 bdrom_udf250.py /path/to/bluray_content output.iso
hdiutil burn output.iso
```

## Input Directory Structure

Your Blu-ray content must follow the BD-ROM structure:

```
content/
├── BDMV/
│   ├── index.bdmv
│   ├── MovieObject.bdmv
│   ├── STREAM/
│   │   └── *.m2ts
│   ├── CLIPINF/
│   │   └── *.clpi
│   ├── PLAYLIST/
│   │   └── *.mpls
│   └── BACKUP/
└── CERTIFICATE/
    ├── id.bdmv
    └── BACKUP/
```

Use [tsMuxeR](https://github.com/justdan96/tsMuxeR) or [MakeMKV](https://www.makemkv.com/) to produce this structure from video files or existing discs.

## Working with Existing ISOs

```bash
# Mount the source ISO
hdiutil attach -readonly -nobrowse source.iso

# Create new UDF 2.50 image sized for the content
src/mkudffs --new-file --udfrev=2.50 --blocksize=2048 --label="BLURAY" new.iso BLOCK_COUNT

# Mount new image, copy content (skip macOS junk)
hdiutil attach -nobrowse new.iso
cp -R /Volumes/SOURCE/BDMV /Volumes/BLURAY/
cp -R /Volumes/SOURCE/CERTIFICATE /Volumes/BLURAY/
hdiutil detach /dev/disk_new

# Burn
hdiutil burn new.iso
```

## What Makes This PS4-Compatible

PS4's Blu-ray player requires strict BD-ROM UDF 2.50 compliance:

| Requirement | Status |
|---|---|
| UDF 2.50 (NSR03) | ✓ |
| Metadata Partition (Type 2 partition map) | ✓ |
| Metadata File + Mirror (fileType 0xFA/0xFB) | ✓ |
| Extended File Entries (EFE) | ✓ |
| LVID integrity: closed | ✓ |

## Troubleshooting

**PS4 error CE-35486-6**: The disc's UDF structure is not recognized. Ensure:
- You're using a BD-R disc (not DVD-R or CD-R)
- Your Blu-ray burner supports BD-R writing
- The BDMV structure is valid (has `index.bdmv` and `MovieObject.bdmv`)
- The video streams use PS4-supported codecs (H.264/AVC or H.265/HEVC)

**macOS mounts UDF 2.50 images as read-only**: This is expected — macOS doesn't support writing to UDF volumes with Metadata Partition. Mount the image *before* `mkudffs` formats it (use a pre-allocated file), or use `bdrom_udf250.py` which injects files directly.

**Disc burns but won't play**: Verify your source video is properly muxed as a Blu-ray structure with valid playlists and clip info. Use tsMuxeR for remuxing.

## Tools

| Tool | Purpose |
|---|---|
| `src/mkudffs` | UDF filesystem creator with Metadata Partition support (C, compiled) |
| `bdrom_udf250.py` | Standalone BD-ROM image creator (Python, no dependencies) |
