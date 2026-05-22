# UDF 2.50 ISO Creator for macOS — Feasible Approaches

macOS has no native tool to create UDF 2.50 images (`hdiutil` caps at 1.50, `mkisofs` at 1.02).

## 1. Port Linux `udftools` to macOS

[udftools](https://github.com/pali/udftools) includes `mkudffs` which creates UDF 1.02–2.60.

- **Work**: patch out Linux-only ioctls, replace block device handling with file I/O
- **Pros**: battle-tested, full UDF 2.50 support, active project
- **Cons**: GPL license, creates raw filesystem image (not ISO 9660 hybrid)

## 2. Use `libcdio` / `libudf`

[GNU libcdio](https://www.gnu.org/software/libcdio/) includes `libudf` for reading UDF. Extend it to write UDF 2.50 into an ISO image.

- **Work**: implement UDF 2.50 write path (currently read-only)
- **Pros**: handles ISO 9660 container, cross-platform, C library
- **Cons**: significant effort to add write support

## 3. Implement UDF 2.50 writer from spec

Build a standalone tool from ECMA-167 + OSTA UDF 2.50 specs.

- **Specs**: [ECMA-167](https://ecma-international.org/publications-and-standards/standards/ecma-167/) + [UDF 2.50](http://www.osta.org/specs/pdf/udf250.pdf)
- **Key structures**:
  - Anchor Volume Descriptor Pointer
  - Primary/Logical Volume Descriptors
  - Partition Descriptor + Space Bitmap
  - File Set Descriptor, FIDs, ICBs
  - NSR03 recognition sequence (marks UDF 2.00+)
- **Pros**: no dependencies, permissive license, full control
- **Cons**: most effort — UDF is complex (metadata partition, allocation strategies)

## 4. Swift/ObjC wrapper around FUSE-based approach

Use [macFUSE](https://osxfuse.github.io/) + UDF 2.50 driver:
1. Create a sparse disk image
2. Mount via FUSE with UDF 2.50 driver
3. Copy files in
4. Flatten to ISO

- **Pros**: leverages existing FUSE UDF drivers
- **Cons**: macFUSE runtime dependency, indirect

## 5. Wrap `mkudffs` as Homebrew-distributed CLI (Recommended starting point)

Cross-compile `mkudffs` for macOS, pair with a script:
1. Create raw UDF 2.50 image via patched `mkudffs`
2. Populate it (mount or direct file injection)
3. Output `.iso` / `.img` ready to burn with `hdiutil burn`

- **Pros**: fastest path to working tool
- **Cons**: depends on patching udftools successfully

---

## Recommendation

- **Quick win**: Approach 5 — port `mkudffs`, wrap in a script
- **Clean standalone**: Approach 3 — implement from spec, limit scope to sequential/ROM (no rewritable/metadata partition), which covers Blu-ray ROM use case
