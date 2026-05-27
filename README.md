# udftools for macOS

A macOS port of [udftools](https://github.com/pali/udftools) (v2.3) — the Linux UDF filesystem utilities. Creates, inspects, and relabels UDF filesystem images (1.01–2.60) on macOS, including UDF 2.50 for Blu-ray.

## What's Included

| Tool | Description |
|------|-------------|
| `mkbdrom` | Create UDF 2.50 BD-ROM images directly from a source directory (PS4/PS5 compatible) |
| `mkudffs` | Create UDF filesystem images (general purpose) |
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

## Install

### Homebrew (recommended)

```bash
brew install caikn/udftools/udftools-mac
```

Or using tap:

```bash
brew tap caikn/udftools
brew install udftools-mac
```

### Build from Source

Requires only Xcode command-line tools (`xcode-select --install`).

```bash
make
sudo make install
```

This installs `mkbdrom`, `mkudffs`, `udfinfo`, and `udflabel` to `/usr/local/bin/`. To customize the prefix:

```bash
make install PREFIX=/opt/udftools
```

### Regression Check

Run the mkbdrom regression script to verify anchor placement and metadata sizing:

```bash
./scripts/test_mkbdrom_regression.sh
```

This tests that:
- The final block count matches the physical file size (no stale anchors from a post-layout resize)
- Metadata partition size is credible for the number of files packed (not frozen before packing)

## Usage

### mkbdrom — Create BD-ROM Image from Directory

Creates a UDF 2.50 BD-ROM ISO directly from a source directory. No mounting required — files are packed directly into the image. This is the recommended tool for creating PS4/PS5 compatible Blu-ray disc images on macOS.

In most cases, the best choice is to omit `--disc-capacity` and let `mkbdrom` create the smallest valid UDF 2.50 image that fits the source content plus metadata. Use `--disc-capacity` only when you need a specific final image geometry, such as matching a known disc size.

```bash
# Recommended: create the minimum-size valid BD-ROM image
mkbdrom --source /path/to/content --label "BLURAY" output.iso

# Optional: force a 25 GB BD-R-sized image
mkbdrom --source /path/to/content --label "BLURAY" --disc-capacity 12219392 output.iso

# Source directory should contain standard BD-ROM structure:
# content/
# ├── BDMV/
# │   ├── index.bdmv
# │   ├── MovieObject.bdmv
# │   ├── STREAM/*.m2ts
# │   ├── CLIPINF/*.clpi
# │   └── PLAYLIST/*.mpls
# └── CERTIFICATE/
```

Options:
- `--source <dir>` — Source directory containing BDMV/CERTIFICATE structure
- `--label <name>` — Volume label (default: BLURAY)
- `--disc-capacity <blocks>` — Optional final image size in blocks; omit it unless you need a specific target geometry such as 12219392 for 25GB BD-R
- `--blocksize <n>` — Block size in bytes (default: 2048)

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
mkbdrom/        mkbdrom tool source (BD-ROM image creator)
mkudffs/        mkudffs tool source (general UDF formatter)
udfinfo/        udfinfo tool source
udflabel/       udflabel tool source
```

Mirrors the [upstream udftools](https://github.com/pali/udftools) layout.

## BD-ROM Workflow (PS4/PS5 Compatible)

```bash
# 1. Create BD-ROM image directly from source content
# Recommended: let mkbdrom choose the minimum valid size
mkbdrom --source /path/to/content --label "BLURAY" disc.iso

# Optional: if you need a full 25 GB BD-R-sized image
# mkbdrom --source /path/to/content --label "BLURAY" --disc-capacity 12219392 disc.iso

# 2. Verify
udfinfo disc.iso

# 3. Test with VLC
open -a VLC "bluray:///path/to/disc.iso"

# 4. Burn to BD-R
hdiutil burn disc.iso
```

Common disc capacities (in 2048-byte blocks):
- BD-R SL (25 GB): `--disc-capacity 12219392`
- BD-R DL (50 GB): `--disc-capacity 24438784`

## License

GPL-2.0 (inherited from upstream udftools).
