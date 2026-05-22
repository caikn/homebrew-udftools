# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

A macOS port of `mkudffs` from Linux [udftools](https://github.com/pali/udftools) (v2.3). Creates UDF filesystem images (1.01–2.60) on macOS, including UDF 2.50 for Blu-ray.

## Build

```bash
cd src
make        # produces ./mkudffs
make clean  # remove build artifacts
```

No autotools, no dependencies beyond Xcode command-line tools. Single `make` invocation, C99.

## Usage Examples

```bash
# Create UDF 2.50 image (Blu-ray compatible)
./mkudffs --new-file --media-type=hd --udfrev=2.50 --blocksize=2048 --label="MY_DISC" output.iso 10000000

# Create UDF 2.01 image (general purpose)
./mkudffs --new-file --blocksize=2048 --label="MyDisk" output.img 1024

# Dry-run (simulate without writing)
./mkudffs --no-write --blocksize=2048 somefile.img

# Mount on macOS
hdiutil attach output.iso
```

## macOS Port: What Was Changed From Upstream

All platform-specific changes are guarded by `#ifdef __APPLE__` so the code remains compilable on Linux.

### `main.c`
- Replaced `linux/fs.h`, `linux/fd.h`, `sys/sysmacros.h` with `sys/disk.h`
- `get_blocks()`: uses `DKIOCGETBLOCKCOUNT` + `DKIOCGETBLOCKSIZE` instead of `BLKGETSIZE64`/`BLKGETSIZE`/`FDGETPRM`
- `detect_blocksize()`: uses `DKIOCGETBLOCKSIZE` instead of `BLKSSZGET`
- `is_whole_disk()`: stubbed (returns 1 — no `/sys/dev/block/` on macOS)
- `is_removable_disk()`: stubbed (returns -1)

### `mkudffs.c`
- Removed `linux/hdreg.h` include
- `fill_mbr()`: skips `HDIO_GETGEO` ioctl, always uses LBA-Assist Translation fallback for CHS calculation

### `options.c`
- Removed `linux/cdrom.h` include
- Media autodetection: defaults to `MEDIA_TYPE_HD` on macOS (no CDROM ioctl probing)
- UDF 2.50+ without VAT: converted from hard error to warning on macOS (allows `--media-type=hd --udfrev=2.50`)

### `config.h`
- Hand-written for macOS (little-endian arm64, `_FILE_OFFSET_BITS=64`)

## Architecture

```
src/
├── main.c          Entry point, I/O (open/write/seek), block device probing
├── mkudffs.c       UDF structure setup (VRS, anchors, partitions, VDS, VAT, MBR)
├── options.c       CLI argument parsing, media type detection
├── defaults.c      Default UDF descriptor initializers (PVD, LVD, FSD, etc.)
├── file.c          File/directory creation, FID insertion, space allocation
├── extent.c        Extent list management (linked list of disc regions)
├── unicode.c       OSTA Compressed Unicode encoding/decoding
├── crc.c           UDF CRC-ITU-T (CCITT) implementation
├── misc.c          Utilities (randu32, read/write_nointr, strtou32, UUID gen)
├── ecma_167.h      ECMA-167 on-disc structure definitions
├── osta_udf.h      OSTA UDF extension structure definitions
├── libudffs.h      Core types (udf_disc, udf_extent, udf_desc, flags, enums)
├── bswap.h         Endian conversion macros (le/be ↔ cpu)
└── config.h        Build configuration (version, endianness)
```

Key data flow: `main()` → `udf_init_disc()` → `parse_args()` → `split_space()` → `setup_*()` → `write_disc()`

The `udf_disc` struct (in `libudffs.h`) is the central state: it holds all UDF descriptors, the extent linked list, and write callback. `split_space()` in `mkudffs.c` is where block layout decisions happen.

## Key Limitation

UDF 2.50+ spec requires a Metadata Partition for non-VAT (overwritable) disks. This implementation skips it. The resulting images work with macOS, Windows, and Linux UDF drivers but are not strictly spec-compliant for UDF 2.50 overwritable media. VAT-based (BDR) images are fully compliant but mount read-only on macOS.

## License

GPL-2.0 (inherited from upstream udftools).
