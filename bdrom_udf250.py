#!/usr/bin/env python3
"""
bdrom_udf250.py — Create BD-ROM compliant UDF 2.50 image with Metadata Partition.
Usage: python3 bdrom_udf250.py <input_dir> <output.iso>
"""

import struct, sys, os, time
from pathlib import Path

BS = 2048

def udf_crc(data):
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x11021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    crc = 0
    for b in data:
        crc = ((crc << 8) ^ t[(crc >> 8) ^ b]) & 0xFFFF
    return crc

def tag(tid, loc, body):
    t = bytearray(struct.pack('<HHBxHHHI', tid, 3, 0, 1, udf_crc(body), len(body), loc))
    s = 0
    for i in range(16):
        if i != 4: s = (s + t[i]) & 0xFF
    t[4] = s
    return bytes(t)

def ts():
    t = time.localtime()
    tz = -(time.timezone if not t.tm_isdst else time.altzone) // 60
    return struct.pack('<hHBBBBBBBx', (tz & 0x0FFF)|0x1000,
        t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0, 0)

def charspec():
    c = bytearray(64); c[0]=0; c[1:24]=b'OSTA Compressed Unicode'; return bytes(c)

def regid(ident, suffix=b''):
    r = bytearray(32); r[1:1+min(len(ident),23)] = ident[:23]
    if suffix: r[24:24+min(len(suffix),8)] = suffix[:8]
    return bytes(r)

def udf_suf(): return struct.pack('<HBB4s', 0x0250, 0x04, 0x01, b'\x00'*4)
def dom_suf(): return struct.pack('<HBB4s', 0x0250, 0, 0, b'\x00'*4)

def dstr(s, size):
    e = b'\x08' + s.encode('ascii')[:size-2]
    r = bytearray(size); r[:len(e)] = e; r[size-1] = len(e)
    return bytes(r)

def long_ad(length, blk, part):
    return struct.pack('<I', length) + struct.pack('<IH', blk, part) + b'\x00'*6

def short_ad(length, pos):
    return struct.pack('<II', length, pos)

def pad(data):
    return data + b'\x00'*(BS - len(data)) if len(data) < BS else data[:BS]

TS = ts()


def scan(path):
    entries = []
    for item in sorted(path.iterdir()):
        if item.name.startswith('.'): continue
        if item.is_dir():
            entries.append({'n': item.name, 'p': item, 'd': True, 'c': scan(item), 's': 0})
        elif item.is_file():
            entries.append({'n': item.name, 'p': item, 'd': False, 'c': [], 's': item.stat().st_size})
    return entries


def collect_files(tree, out):
    for e in tree:
        if e['d']: collect_files(e['c'], out)
        else: out.append(e)


def collect_dirs(tree, out, parent_mb):
    for e in tree:
        if e['d']:
            e['_pmb'] = parent_mb
            out.append(e)
            collect_dirs(e['c'], out, None)  # _meta_block set later


def make_efe(meta_blk, ftype, info_len, obj_size, blocks, links, uid, alloc, ad_flag):
    body = bytearray()
    # icbTag (20)
    body += struct.pack('<IHHH BB IH H', 0, 4, 0, 1, 0, ftype, 0, 0, ad_flag)
    body += struct.pack('<II', 0xFFFFFFFF, 0xFFFFFFFF)  # uid, gid
    body += struct.pack('<I', 0x00001CA5)  # permissions: rwxr-xr-x in UDF format
    body += struct.pack('<H BB I', links, 0, 0, 0)  # linkCount, recFmt, recDispAttr, recLen
    body += struct.pack('<Q', info_len)
    body += struct.pack('<Q', obj_size)
    body += struct.pack('<Q', blocks)
    body += TS * 4  # access, mod, create, attr time
    body += struct.pack('<I', 1)  # checkpoint
    body += struct.pack('<I', 0)  # reserved
    body += long_ad(0, 0, 0)  # extendedAttrICB
    body += long_ad(0, 0, 0)  # streamDirectoryICB
    body += regid(b'*mkudffs-mac', udf_suf())
    body += struct.pack('<Q', uid)
    body += struct.pack('<II', 0, len(alloc))  # lenExtAttr, lenAllocDescs
    body += alloc
    return tag(266, meta_blk, bytes(body)) + bytes(body)


def make_fid(name, icb_blk, is_dir, is_parent, partition):
    if is_parent:
        nb = b''; fc = 0x0A
    else:
        nb = b'\x08' + name.encode('ascii'); fc = 0x02 if is_dir else 0x00
    body = bytearray()
    body += struct.pack('<H B B', 1, fc, len(nb))
    body += long_ad(BS, icb_blk, partition)
    body += struct.pack('<H', 0)
    body += nb
    total = 16 + len(body)
    body += b'\x00' * ((4 - total % 4) % 4)
    return tag(257, icb_blk, bytes(body)) + bytes(body)


def build(input_dir, output_path, label="BLURAY"):
    tree = scan(Path(input_dir))

    files = []; collect_files(tree, files)
    dirs = [{'n': '', 'd': True, 'c': tree, '_pmb': 1}]  # root
    collect_dirs(tree, dirs, 1)

    nf = len(files)
    nd = len(dirs)
    uid_counter = [16]
    def nuid():
        u = uid_counter[0]; uid_counter[0] += 1; return u

    # Assign metadata block offsets
    # 0=FSD, 1=root_dir, 2+=subdirs, then files
    dirs[0]['_mb'] = 1
    nm = 2
    for d in dirs[1:]: d['_mb'] = nm; nm += 1
    for f in files: f['_mb'] = nm; nm += 1
    total_meta = nm

    # Fix parent pointers
    dirs[0]['_pmb'] = 1  # root parent is itself
    for d in dirs[1:]:
        if d['_pmb'] is None: d['_pmb'] = 1  # default to root

    # Now fix child dirs' parent pointers properly
    def fix_parents(entries, parent_mb):
        for e in entries:
            if e['d']:
                e['_pmb'] = parent_mb
                fix_parents(e['c'], e['_mb'])
    fix_parents(tree, 1)

    # Data layout for files
    ndata = 0
    for f in files:
        f['_db'] = ndata
        f['_dn'] = (f['s'] + BS - 1) // BS if f['s'] > 0 else 0
        ndata += f['_dn']

    # Physical partition layout
    meta_start = 2  # blocks 0,1 = meta file + mirror
    data_start = meta_start + total_meta
    part_blocks = data_start + ndata

    part_abs = 257  # partition starts at absolute block 257
    total_blocks = part_abs + part_blocks + 300
    rvds = total_blocks - 48

    print(f"UDF 2.50 BD-ROM: {nf} files, {nd} dirs, {total_blocks} blocks ({total_blocks*BS/(1024**3):.2f} GB)")

    with open(output_path, 'wb') as f:
        f.truncate(total_blocks * BS)

        # VRS
        f.seek(16*BS)
        for ident in (b'BEA01', b'NSR03', b'TEA01'):
            v = bytearray(BS); v[1:6]=ident; v[6]=1; f.write(bytes(v))

        # MVDS at block 32
        def write_vds(start):
            f.seek(start*BS)
            # PVD
            b = bytearray()
            b += struct.pack('<II', 0, 0)
            b += dstr(label, 32)
            b += struct.pack('<HHHH II', 1, 1, 2, 3, 1, 1)
            b += dstr(os.urandom(8).hex()+label, 128)
            b += charspec()*2
            b += struct.pack('<IIII', 0,0,0,0)
            b += regid(b'')
            b += TS
            b += regid(b'*UDF LV Info', udf_suf())
            b += b'\x00'*64
            b += struct.pack('<IH', 0, 0) + b'\x00'*22
            f.write(pad(tag(1, start, bytes(b)) + bytes(b)))

            # LVD
            pm1 = struct.pack('<BBHH', 1, 6, 1, 0)
            pm2 = bytearray(64)
            pm2[0]=2; pm2[1]=64
            pm2[4:36] = regid(b'*UDF Metadata Partition', udf_suf())
            struct.pack_into('<HH', pm2, 36, 1, 0)
            struct.pack_into('<III', pm2, 40, 0, 1, 0xFFFFFFFF)
            struct.pack_into('<IHB', pm2, 52, 1, 0, 0)
            mt = pm1 + bytes(pm2)
            b = bytearray()
            b += struct.pack('<I', 1)
            b += charspec()
            b += dstr(label, 128)
            b += struct.pack('<I', BS)
            b += regid(b'*OSTA UDF Compliant', dom_suf())
            b += long_ad(BS, 0, 1)  # FSD at meta partition block 0
            b += struct.pack('<II', len(mt), 2)
            b += regid(b'*mkudffs-mac', udf_suf())
            b += b'\x00'*128
            b += struct.pack('<II', 2*BS, 48)  # LVID extent
            b += mt
            f.write(pad(tag(6, start+1, bytes(b)) + bytes(b)))

            # PD
            b = bytearray()
            b += struct.pack('<I HH', 2, 0, 0)
            b += regid(b'+NSR03')
            b += b'\x00'*128  # partitionContentsUse
            b += struct.pack('<III', 1, part_abs, part_blocks)  # accessType=read-only
            b += regid(b'*mkudffs-mac', udf_suf())
            b += b'\x00'*128 + b'\x00'*156
            f.write(pad(tag(5, start+2, bytes(b)) + bytes(b)))

            # IUVD
            b = bytearray()
            b += struct.pack('<I', 3)
            b += regid(b'*UDF LV Info', udf_suf())
            b += charspec() + dstr(label, 128) + dstr('',36)*3
            b += regid(b'*mkudffs-mac', udf_suf()) + b'\x00'*128
            f.write(pad(tag(4, start+3, bytes(b)) + bytes(b)))

            # USD
            b = struct.pack('<II', 4, 0)
            f.write(pad(tag(7, start+4, b) + b))

            # TD
            b = b'\x00'*496
            f.write(pad(tag(8, start+5, b) + b))

        write_vds(32)

        # LVID at block 48
        f.seek(48*BS)
        b = bytearray()
        b += TS + struct.pack('<I II', 1, 0, 0)
        lvu = bytearray(32); struct.pack_into('<Q', lvu, 0, uid_counter[0]); b += bytes(lvu)
        b += struct.pack('<II', 2, 46)  # numPart, impUseLen
        b += struct.pack('<IIII', 0, 0, part_blocks, total_meta)
        imp = bytearray(46)
        imp[0:32] = regid(b'*UDF Linux LVID', udf_suf())
        struct.pack_into('<IIHHH', imp, 32, nf, nd, 0x0250, 0x0250, 0x0250)
        b += bytes(imp)
        f.write(pad(tag(9, 48, bytes(b)) + bytes(b)))
        b = b'\x00'*496
        f.write(pad(tag(8, 49, b) + b))

        # AVDP at 256
        f.seek(256*BS)
        b = struct.pack('<II II', 16*BS, 32, 16*BS, rvds) + b'\x00'*480
        f.write(pad(tag(2, 256, b) + b))

        # Metadata File EFE at partition block 0
        f.seek((part_abs+0)*BS)
        alloc = short_ad(total_meta*BS, meta_start)
        f.write(pad(make_efe(0, 250, total_meta*BS, total_meta*BS, total_meta, 1, 0, alloc, 0)))

        # Metadata Mirror EFE at partition block 1
        f.seek((part_abs+1)*BS)
        f.write(pad(make_efe(1, 251, total_meta*BS, total_meta*BS, total_meta, 1, 0, alloc, 0)))

        # FSD at metadata block 0 (physical = part_abs + meta_start + 0)
        f.seek((part_abs + meta_start + 0)*BS)
        b = bytearray()
        b += TS + struct.pack('<HH II II', 3, 3, 1, 1, 0, 0)
        b += charspec() + dstr(label, 128) + charspec()
        b += dstr(label, 32) + dstr('', 32) + dstr('', 32)
        b += long_ad(BS, 1, 1)  # root dir ICB at meta block 1, partition 1
        b += regid(b'*OSTA UDF Compliant', dom_suf())
        b += long_ad(0,0,0)*2 + b'\x00'*32
        f.write(pad(tag(256, 0, bytes(b)) + bytes(b)))

        # Directory EFEs
        for d in dirs:
            mb = d['_mb']
            # Build FIDs
            fids = bytearray()
            fids += make_fid(None, d['_pmb'], True, True, 1)  # parent
            for child in d['c']:
                if child['d']:
                    fids += make_fid(child['n'], child['_mb'], True, False, 1)
                else:
                    fids += make_fid(child['n'], child['_mb'], False, False, 1)
            lc = sum(1 for c in d['c'] if c['d']) + 1
            efe = make_efe(mb, 4, len(fids), len(fids), 0, lc, nuid(), bytes(fids), 3)
            f.seek((part_abs + meta_start + mb)*BS)
            f.write(pad(efe))

        # File EFEs
        for fi in files:
            mb = fi['_mb']
            size = fi['s']
            nblk = fi['_dn']
            phys_blk = data_start + fi['_db']
            # Build allocation descriptors
            alloc = b''
            rem = size; pos = phys_blk
            while rem > 0:
                ext = min(rem, 0x3FFFFFFF)
                if rem > 0x3FFFFFFF:
                    ext = (ext // BS) * BS
                alloc += short_ad(ext, pos)
                pos += (ext + BS - 1) // BS
                rem -= ext
            if size == 0:
                alloc = b''
            efe = make_efe(mb, 5, size, size, nblk, 1, nuid(), alloc, 0)
            f.seek((part_abs + meta_start + mb)*BS)
            f.write(pad(efe))

        # File data
        print("  Writing file data...")
        for fi in files:
            if fi['_dn'] == 0: continue
            f.seek((part_abs + data_start + fi['_db'])*BS)
            with open(fi['p'], 'rb') as src:
                rem = fi['s']
                while rem > 0:
                    chunk = src.read(min(rem, 8*1024*1024))
                    if not chunk: break
                    f.write(chunk); rem -= len(chunk)
            tail = fi['s'] % BS
            if tail: f.write(b'\x00'*(BS-tail))

        # RVDS
        write_vds(rvds)

        # AVDP at N-257 and N-1
        for loc in (total_blocks-257, total_blocks-1):
            f.seek(loc*BS)
            b = struct.pack('<II II', 16*BS, 32, 16*BS, rvds) + b'\x00'*480
            f.write(pad(tag(2, loc, b) + b))

    print(f"  Done: {output_path}")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_dir> <output.iso>"); sys.exit(1)
    if not os.path.isdir(sys.argv[1]):
        print(f"Error: {sys.argv[1]} is not a directory"); sys.exit(1)
    build(sys.argv[1], sys.argv[2])
