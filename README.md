# udftools for macOS

A macOS port of [udftools](https://github.com/pali/udftools) (v2.3) — the Linux UDF filesystem utilities. Creates, inspects, and relabels UDF filesystem images (1.01–2.60) on macOS, including UDF 2.50 for Blu-ray.

## What's Included

| Tool | Description |
|------|-------------|
| `mkudffs` | Create UDF filesystem images |
| `udfinfo` | Display UDF filesystem information |
| `udflabel` | Show or change UDF filesystem label/UUID |

The Linux-only tools (`cdrwtool`, `pktsetup`) are not ported as they depend on Linux kernel interfaces for optical/packet writing.

## Differences from Upstream

| | Upstream (Linux) | This Port (macOS) |
|---|---|---|
| Build system | autotools (`./configure && make`) | Hand-written Makefiles |
| Block device ioctls | `BLKGETSIZE64`, `BLKSSZGET` | `DKIOCGETBLOCKCOUNT`, `DKIOCGETBLOCKSIZE` |
| CD-ROM probing | `CDROMMULTISESSION`, `CDROM_LAST_WRITTEN` | Stubbed (not available on macOS) |
| Media autodetection | Probes device type via ioctls | Defaults to HD |
| Metadata Partition | Not implemented for `mkudffs` | Fully implemented (required for BD-ROM) |
| `fdatasync` | Used directly | `fcntl(F_FULLFSYNC)` on macOS |
| Geometry ioctl | `HDIO_GETGEO` | LBA-Assist Translation fallback |

All platform-specific changes are guarded by `#ifdef __APPLE__` so the code remains compilable on Linux.

## Build

Requires only Xcode command-line tools (`xcode-select --install`).

```bash
# Build all tools
cd mkudffs && make && cd ..
cd udfinfo && make && cd ..
cd udflabel && make && cd ..

# Install to /usr/local/bin (optional)
sudo cp mkudffs/mkudffs udfinfo/udfinfo udflabel/udflabel /usr/local/bin/
```

## Usage

### mkudffs — Create UDF Filesystem

```bash
# UDF 2.50 image with Metadata Partition (BD-ROM compatible)
mkudffs --new-file --udfrev=2.50 --blocksize=2048 --label="BLURAY" output.iso 10000000

# UDF 2.01 image (general purpose)
mkudffs --new-file --blocksize=2048 --label="MyDisk" output.img 1024

# Dry-run (simulate without writing)
mkudffs --no-write --blocksize=2048 somefile.img
```

UDF 2.50+ automatically includes a Type 2 Metadata Partition Map when not using VAT (sequential media).

### udfinfo — Show UDF Filesystem Information

```bash
# Show full UDF metadata
udfinfo image.iso

# Example output:
#   label=BLURAY
#   uuid=1fc25c61a7d5da0b
#   blocksize=2048
#   blocks=10286100
#   udfrev=2.50
#   accesstype=readonly
#   metadatapartition=yes
#   metadatafileloc=0
#   metadatamirrorfileloc=1
#   metadatasize=44
```

Options:
- `-b, --blocksize=N` — Override block size detection
- `--startblock=N` — Where the UDF filesystem starts
- `--lastblock=N` — Where the UDF filesystem ends
- `--vatblock=N` — Location of the VAT (for sequential media)

### udflabel — Show or Change UDF Label

```bash
# Show current label
udflabel image.iso

# Change label
udflabel image.iso "NEW_LABEL"

# Change label on read-only image (force mode)
udflabel --force image.iso "NEW_LABEL"

# Dry-run (show what would change)
udflabel --no-write image.iso "NEW_LABEL"

# Change UUID
udflabel -u random image.iso

# Change multiple identifiers
udflabel --lvid="My Volume" --vid="MYVOL" --uuid=random image.iso
```

## Repository Structure

```
include/        Shared headers (ecma_167.h, osta_udf.h, libudffs.h, bswap.h, config.h)
libudffs/       Shared library source (crc, extent, unicode, misc)
mkudffs/        mkudffs tool source
udfinfo/        udfinfo tool source
udflabel/       udflabel tool source
```

Mirrors the [upstream udftools](https://github.com/pali/udftools) layout.

## BD-ROM Workflow (PS4/PS5 Compatible)

```bash
# 1. Create UDF 2.50 image
mkudffs --new-file --udfrev=2.50 --blocksize=2048 --label="BLURAY" disc.iso 10000000

# 2. Mount and populate
hdiutil attach -nobrowse disc.iso
cp -R /path/to/BDMV /Volumes/BLURAY/
cp -R /path/to/CERTIFICATE /Volumes/BLURAY/
hdiutil detach /dev/diskN

# 3. Verify
udfinfo disc.iso

# 4. Burn
hdiutil burn disc.iso
```

## License

GPL-2.0 (inherited from upstream udftools).
