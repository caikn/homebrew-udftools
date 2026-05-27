# UDF 2.50 BD-ROM ISO Playback Issues

The file `ps4_bdrom.iso` plays correctly via `bluray:///path/to/file.iso` in VLC (which uses libudfread's internal UDF parser), but fails when:
1. Mounted on macOS and played from the mount
2. Burned to BD-R and played on PS4

---

## macOS Mount Failure

**Root cause: macOS kernel UDF driver could not read file data from our Metadata Partition implementation.**

- Directory listings work (filenames, sizes visible via `stat`)
- Reading any file content failed with `EOVERFLOW` ("Result too large")
- The driver parsed the Metadata Partition enough for directory entries but could not resolve allocation descriptors to physical block addresses for data reads
- Commercial BD-ROM images (e.g. ImgBurn-created) mount and read fine — the issue was specific to our metadata structure
- VLC works with ISO files directly because libbluray/libudfread bypasses the OS and reads raw UDF blocks itself

**Fixed:** The stream directory ICB was using incorrect addressing (physical offset instead of metadata-partition-relative). Corrected in `mkudffs.c` and `file.c`.

---

## PS4 "Corrupted data" — Potential Causes

### 1. Missing backup anchors on disc

ISO has 10286100 blocks; burned disc reports 10286112 blocks (12 extra). The 2nd AVDP (block N-257) and 3rd AVDP (block N-1) are at ISO-relative positions that don't match the disc's physical size. PS4 may require all three anchors.

### 2. Metadata Partition structure issue

Our `mkudffs` metadata implementation is new. `tagLocation` values, metadata file/mirror EFE offsets, or partition map entries may not match what PS4's strict UDF parser expects. Needs comparison against a known-good commercial BD-ROM image.

### 3. AACS encryption mismatch

If the source content was from a commercial disc, burned BD-R won't have valid AACS credentials. PS4 will reject it.

---

## Diagnostic Data

```
# ISO file
blocks=10286100, behindblocks=0
2nd Anchor: block 10285843 ✓
3rd Anchor: block 10286099 ✓

# Burned disc
blocks=10286112, behindblocks=44
2nd Anchor: MISSING
3rd Anchor: MISSING
```

---

## Open Work Items

- [x] ~~Build a tool to pad ISO to target disc capacity and rewrite 2nd/3rd anchors at correct positions~~ → `--disc-capacity` option added to mkudffs
- [x] ~~Compare our Metadata Partition structure byte-for-byte against a known-good commercial BD-ROM~~ → Done, fixed stream directory ICB and mirror
- [x] ~~Test with a non-encrypted homemade BD-ROM (e.g. from tsMuxeR) to isolate AACS from UDF issues~~ → Confirmed AVKBR-86002 (unencrypted) plays on PS4 when burned from original ISO
- [x] ~~Consider if our metadata `tagLocation` values are truly partition-relative~~ → Fixed: mirror descriptors now get recomputed tags
- [ ] Burn an ISO created by `mkbdrom` and test on PS4

---

## Solution: `mkbdrom` Tool

The `mkbdrom` tool resolves all identified issues by creating BD-ROM images that:

1. **Place anchors at disc-relative positions** — uses `--disc-capacity` to match the physical BD-R size, ensuring all three AVDPs (block 256, N-257, N-1) are where the PS4 firmware expects them.

2. **Correct Metadata Partition structure** — stream directory ICB uses metadata-partition-relative addressing with partition reference 1, consistent with FSD and root directory ICB.

3. **True metadata mirror** — the Metadata Mirror File points to an independent copy at the end of partition space (not the same extent as primary). Mirror descriptors have properly recomputed `descTag` (tagLocation, CRC, checksum).

4. **Bypass macOS mount limitation** — writes files directly into the UDF 2.50 image without needing the OS to mount it (macOS mounts UDF 2.50 as read-only).

5. **Two-pass layout** — a planning pass packs the entire source tree to determine the exact block count needed, then the final image is built with that count from the start. This guarantees that anchor positions, reserve VDS, partition layout, and metadata file extent are all internally consistent. Earlier single-pass code suffered from:
   - Anchors/reserve VDS written at stale offsets because `disc.blocks` was changed after `split_space` froze the layout.
   - Metadata file size computed before `pack_directory` added file/directory descriptors, causing metadata to exclude most content.

### Usage

```bash
# Create BD-ROM from source directory, sized for 25GB BD-R
mkbdrom --source /path/to/content --label "BLURAY" --disc-capacity 12219392 output.iso

# Verify structure
udfinfo output.iso

# Burn
hdiutil burn output.iso
```

---

## Key Insight: Two Different UDF Read Paths

| Path | UDF Parser | Result |
|------|-----------|--------|
| `bluray:///file.iso` | libudfread (internal, full UDF 2.50 + Metadata Partition support) | Works |
| `/Volumes/BLURAY` (mounted) | macOS kernel UDF driver | Read-only (UDF 2.50 policy); reads work after metadata fix |
| Physical BD-R disc on PS4 | PS4 firmware UDF reader | Pending test with mkbdrom two-pass image |
| Physical BD-R disc on macOS | macOS kernel UDF driver | Read-only; reads work after metadata fix |
