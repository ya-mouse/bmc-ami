#!/usr/bin/env python
import os
import sys
import numpy as np
import datetime
import binascii
from struct import pack, unpack

try:
    import ConfigParser as cp
except:
    import configparser as cp

FMH_SIGNATURE = np.fromstring(b'$MODULE$', dtype=np.uint32)
FMH_MAJOR = 1
FMH_MINOR = 5
FMH_END_SIGNATURE = 0x55aa
FMH_SIZE = 64 >> 2
ALT_FMH_SIZE = 16 >> 2

MODULE_FMH_FIRMWARE = 0x02

MODULE_FORMAT_FIRMWARE_INFO = 0x02

MODULE_FIRMWARE_1_4 = ((MODULE_FORMAT_FIRMWARE_INFO << 8) | MODULE_FMH_FIRMWARE)

MODULE_FLAG_BOOTPATH_OS        = 0x0001
MODULE_FLAG_BOOTPATH_DIAG      = 0x0002
MODULE_FLAG_BOOTPATH_RECOVERY  = 0x0004
MODULE_FLAG_COPY_TO_RAM        = 0x0008
MODULE_FLAG_EXECUTE            = 0x0010
MODULE_FLAG_COMPRESSION_MASK   = 0x00e0
MODULE_FLAG_COMPRESSION_LSHIFT = 5
MODULE_FLAG_VALID_CHECKSUM     = 0x0100

INVALID_FMH_OFFSET = 0xffffffff

FMH_MODULE_CHECKSUM_OFFSET = 0x32

OFF_FMH_ALLOC   = 3
OFF_FMH_LOC     = 4
OFF_FMH_MODULE  = 6
OFF_FMH_END_SIG = 15
OFF_MOD_TYPE    = 2
OFF_MOD_LOC     = 3
OFF_MOD_SIZE    = 4
OFF_MOD_FLAGS   = 5
OFF_MOD_LOAD    = 5
OFF_MOD_CRC32   = 6

def scan_for_fmh(image, offset, block_size):
    def validate100(offset, size):
        return np.sum(image[offset:offset+size].view(dtype=np.uint8), dtype=np.uint8) == 0

    def normal(offset):
        if not (image[offset:offset+2] == FMH_SIGNATURE).all():
            return INVALID_FMH_OFFSET
        if (image[offset+OFF_FMH_END_SIG] >> 16) != FMH_END_SIGNATURE:
            return INVALID_FMH_OFFSET
        if not validate100(offset, FMH_SIZE):
            return INVALID_FMH_OFFSET
        return offset

    def alternate(offset):
        if not (image[offset+2:offset+4] == FMH_SIGNATURE).all():
            return INVALID_FMH_OFFSET
        if (image[offset] & 0xffff) != FMH_END_SIGNATURE:
            return INVALID_FMH_OFFSET
        if not validate100(offset, ALT_FMH_SIZE):
            return INVALID_FMH_OFFSET
        return image[offset+1] >> 2

    fmh = normal(offset)
    if fmh != INVALID_FMH_OFFSET:
        return fmh
    fmh = alternate(offset + block_size - ALT_FMH_SIZE)
    if fmh != INVALID_FMH_OFFSET:
        fmh = normal(offset + fmh)
    return fmh

def build_image(cfg):
    def parse_bytes(num):
        fix = num[-1]
        try:
            num = int(num, 0)
        except:
            num = int(num[:-1], 0)
        if fix == 'M':
            num *= 1024
            fix = 'K'
        if fix == 'K':
            num *= 1024
        return num

    def create_fwinfo(mj, mn):
        buildno = cfg.getint('GLOBAL', 'BuildNo')
        desc = os.getenv('FW_DESC')
        _now = datetime.datetime.now()
        _date = os.getenv('FW_DATE')
        if not _date:
            _date = _now.strftime('%b %d %Y')
        _time = os.getenv('FW_BUILDTIME')
        if not _time:
            _time = _now.strftime('%H:%M:%S')
        out = 'FW_VERSION={}.{}.{}\nFW_DATE={}\nFW_BUILDTIME={}\n'.format(
            mj, mn, buildno, _date, _time)
        if desc:
            out += 'FW_DESC={}\n'.format(desc)
        else:
            out += 'FW_DESC=WARNING : UNOFFICIAL BUILD!! \n'
        try:    out += 'FW_PRODUCTID={}\n'.format(cfg.getint('GLOBAL', 'ProductId'))
        except: pass
        try:    out += 'FW_PRODCUTNAME={}\n'.format(cfg.get('GLOBAL', 'ProductName'))
        except: pass
        return out
#        if len(out) % 4 == 0:
#            return out
#        return out + '\xff' * (4 - (len(out) % 4))

    outname = os.path.dirname(sys.argv[0])+'/'+cfg.get('GLOBAL', 'Output')+'.new'
    flash_size = parse_bytes(cfg.get('GLOBAL', 'FlashSize')) >> 2
    block_size = parse_bytes(cfg.get('GLOBAL', 'BlockSize')) >> 2
    output = np.memmap(outname, mode='w+', dtype=np.uint32, shape=(flash_size,))
    output[:] = 0xffffffff
    image_header_start = 0xffffffff

    mods = []
    for modname in cfg.sections():
        if modname == 'GLOBAL':
            continue
        # Module version
        mj = cfg.getint(modname, 'Major')
        mn = cfg.getint(modname, 'Minor')

        # Module type
        tp = int(cfg.get(modname, 'Type'), 0)
        mformat = tp >> 8

        # Location
        try: fmhloc = parse_bytes(cfg.get(modname, 'FMHLoc'))
        except: fmhloc = 0
        if fmhloc != 0:
            modloc = 0
        elif tp in (0x10,0x11,0x20,0x21) or mformat in (0x11, 0x12):
            modloc = block_size << 2
        else:
            modloc = 0x40
        try: modloc = parse_bytes(cfg.get(modname, 'Offset'))
        except: pass

        # Flags
        flags = 0
        try:
            if cfg.getboolean(modname, 'BootOS'): flags |= MODULE_FLAG_BOOTPATH_OS
        except: pass
        try:
            if cfg.getboolean(modname, 'BootDIAG'): flags |= MODULE_FLAG_BOOTPATH_DIAG 
        except: pass
        try:
            if cfg.getboolean(modname, 'BootRECO'): flags |= MODULE_FLAG_BOOTPATH_RECOVERY
        except: pass
        try:
            if cfg.getboolean(modname, 'CopyToRAM'): flags |= MODULE_FLAG_COPY_TO_RAM
        except: pass
        try:
            if cfg.getboolean(modname, 'Execute'): flags |= MODULE_FLAG_EXECUTE
        except: pass
        try:
            if cfg.getboolean(modname, 'CheckSum'): flags |= MODULE_FLAG_VALID_CHECKSUM
        except: pass
        try: flags |= int(cfg.get(modname, 'Compress'), 0) << MODULE_FLAG_COMPRESSION_LSHIFT
        except: pass

        # Load address
        try: load = parse_bytes(cfg.get(modname, 'Load'))
        except: load = 0xffffffff
        if load == 0xffffffff:
            flags &= ~MODULE_FLAG_COPY_TO_RAM

        # Allocation size
        try: alloc = parse_bytes(cfg.get(modname, 'Alloc'))
        except: alloc = 0

        if tp == MODULE_FMH_FIRMWARE or tp == MODULE_FIRMWARE_1_4:
            alloc = block_size << 2

        # Location
        loc = 0xffffffff
        _loc = cfg.get(modname, 'Locate')
        if _loc == 'START':
            loc = 0
        elif _loc == 'END':
            loc = flash_size - (alloc >> 2)
        if loc == 0xffffffff:
            loc = parse_bytes(_loc) >> 2
        if loc + (alloc  >> 2) > flash_size:
            print('ERROR: module location {}, alloc {} > flash {} for {}'.format(
                  loc << 2, alloc, flash_size << 2, modname))
            break

        # TODO: Update chain for overlap checks

        if tp == MODULE_FMH_FIRMWARE or tp == MODULE_FIRMWARE_1_4:
            _mj = os.getenv('FW_MAJOR')
            _mn = os.getenv('FW_MINOR')
            if _mj: mj = int(_mj, 0)
            if _mn: mn = int(_mn, 0)
            _fwinfo = create_fwinfo(mj, mn)
            modsize = len(_fwinfo)
            if modsize > ((64 * 1024) - 0x40):
                modsize = 0
            fmh_cksum = 0
            image_header_start = loc
            if modsize > 0:
                output.view(dtype=np.uint8)[(loc << 2)+modloc:(loc << 2) + modloc + modsize] = np.fromstring(_fwinfo, dtype=np.uint8)
        else:
            # Mandatory field `File'
            infile = np.memmap(cfg.get(modname, 'File'), mode='r', dtype=np.uint32)
            modsize = len(infile) << 2
            minsize = modloc + modsize
            minsize += (block_size << 2) - 1
            minsize = minsize/(block_size << 2) * (block_size << 2)
            if alloc < minsize:
                alloc = minsize
            fmh_cksum = binascii.crc32(infile, 0) & 0xffffffff
            print('%s %x %d %x %x' % (modname, fmh_cksum, len(infile), alloc, modloc))
            # Write module
            output[loc + (modloc >> 2):loc+(modloc >> 2)+len(infile)] = infile

        # CreateFMH
        mname = modname[:8].lower()
        if len(mname) != 8:
            mname += '\x00' * (8 - len(mname) % 8)
        fmh = np.array([
            FMH_SIGNATURE[0],
            FMH_SIGNATURE[1],
            FMH_SIZE << 18 | FMH_MINOR << 8 | FMH_MAJOR,
            alloc,
            (loc << 2) + fmhloc,
            0x00000000,
            # Module info
            np.fromstring(mname[:4], dtype=np.uint32),
            np.fromstring(mname[4:8], dtype=np.uint32),
            tp << 16 | mn << 8 | mj,
            modloc,
            modsize,
            (load & 0xffff) << 16 | flags & 0xffff,
            fmh_cksum << 16 | load >> 16,
            fmh_cksum >> 16,
            0x00000000,
            FMH_END_SIGNATURE << 16,
        ], dtype=np.uint32)
        fmh[5] = ((~np.sum(fmh.view(dtype=np.uint8), dtype=np.uint8))+1) << 24

        if fmhloc != 0:
            altfmh = np.array([
                FMH_END_SIGNATURE,
                fmhloc,
                FMH_SIGNATURE[0],
                FMH_SIGNATURE[1],
            ], dtype=np.uint32)
            altfmh[0] |= ((~np.sum(altfmh.view(dtype=np.uint8), dtype=np.uint8))+1) << 16
            output[loc+(fmhloc >> 2):loc+(fmhloc >> 2) + len(fmh)] = fmh
            output[loc+(block_size - 4):loc+block_size] = altfmh
        else:
            output[loc:loc+len(fmh)] = fmh

    if image_header_start != 0xffffff:
        output8 = output.view(dtype=np.uint8)
        off8 = image_header_start << 2
        cksum = binascii.crc32(output8[:off8 + 0x17], 0) & 0xffffffff
        cksum = binascii.crc32(output8[off8 + 0x18:off8 + 0x32], cksum) & 0xffffffff
        cksum = binascii.crc32(output8[off8 + 0x36:off8 + (block_size << 2)], cksum) & 0xffffffff
        output8[off8+FMH_MODULE_CHECKSUM_OFFSET:off8+FMH_MODULE_CHECKSUM_OFFSET+4].view(dtype=np.uint32)[:] = cksum
        output8[off8+0x17] = 0x00
        output8[off8+0x17] = (~np.sum(output8[off8:off8+(FMH_SIZE << 2)], dtype=np.uint8))+1

    output.flush()
#    del output

def dump_image(cfg, fwimgname):
    def name(mod):
        return image[mod:mod+2].tostring().replace('\x00','').upper()

    def dump_fwinfo(offset):
        # Get string end position
        idx = np.where(image[offset:offset+block_size].view(dtype=np.uint8) == 0xff)
        if not len(idx[0]):
            return
        desc = image[offset:offset+(idx[0][0] >> 2)].tostring()
        for s in desc.split('\n'):
            try:
                k,v = s.split('=')
            except ValueError:
                continue
            print('{}=\"{}\"'.format(k, v))
            if summary:
                continue
            if k == 'FW_VERSION':
                try:
                    mj, mn, no = v.split('.')
                except ValueError:
                    continue
                cfg.set(modname, 'BuildNo', no)
            elif k == 'FW_PRODUCTID':
                cfg.set(modname, 'ProductId', v)
            elif k[:4] == 'OEM_':
                print(' *** TODO: Process OEM keys here! ***')

    def dump_fmh(fmh, mod, modname):
        mj =  image[mod + OFF_MOD_TYPE] & 0xff
        mn = (image[mod + OFF_MOD_TYPE] >> 8) & 0xff
        tp =  image[mod + OFF_MOD_TYPE] >> 16
        if summary:
            print('{}\t{}.{}\t0x{:x}\n'.format(modname, mj, mn, tp))
            return

        is_mod = not (tp == MODULE_FMH_FIRMWARE or tp == MODULE_FIRMWARE_1_4)
        cfg.add_section(modname)
        cfg.set(modname, 'Major', mj)
        cfg.set(modname, 'Minor', mn)
        cfg.set(modname, 'Type', '0x{:04x}'.format(tp))

        if is_mod:
            # Allocate output
            if image[fmh + OFF_FMH_ALLOC] - image[mod + OFF_MOD_LOC] > image[mod + OFF_MOD_SIZE] + (block_size << 2):
                cfg.set(modname, 'Alloc', '{}K'.format(image[fmh + OFF_FMH_ALLOC] >> 10))

            # Output flags
            flags = image[mod + OFF_MOD_FLAGS] & 0xffff
            cfg.set(modname, 'CheckSum', 'YES' if (flags & MODULE_FLAG_VALID_CHECKSUM) else 'NO')
            if flags & MODULE_FLAG_BOOTPATH_OS:
                cfg.set(modname, 'BootOS', 'YES')
            if flags & MODULE_FLAG_BOOTPATH_DIAG:
                cfg.set(modname, 'BootDIAG', 'YES')
            if flags & MODULE_FLAG_BOOTPATH_RECOVERY:
                cfg.set(modname, 'BootRECO', 'YES')
            if flags & MODULE_FLAG_EXECUTE:
                cfg.set(modname, 'Execute', 'YES')
            if flags & MODULE_FLAG_COPY_TO_RAM:
                cfg.set(modname, 'CopyToRAM', 'YES')
            comp = (flags & MODULE_FLAG_COMPRESSION_MASK) >> MODULE_FLAG_COMPRESSION_LSHIFT
            if comp > 0:
                cfg.set(modname, 'Compress', comp)

            # Filename
            cfg.set(modname, 'File', '{}.bin'.format(modname))

        # Location
        if image[fmh + OFF_FMH_LOC] < (block_size << 2):
            cfg.set(modname, 'Locate', 'START')
        elif image[fmh + OFF_FMH_LOC] == loc:
            cfg.set(modname, 'Locate', 'END')
        elif image[fmh + OFF_FMH_LOC] >> 10 < 0x801:
            cfg.set(modname, 'Locate', '{}K'.format(image[fmh + OFF_FMH_LOC] >> 10))
        else:
            cfg.set(modname, 'Locate', '0x{:x}'.format(image[fmh + OFF_FMH_LOC]))

        if is_mod:
            # Offset output
            if image[mod + OFF_MOD_LOC] > 0x40:
                cfg.set(modname, 'Offset', '{}K'.format(image[mod + OFF_MOD_LOC] >> 10))

            # Load output
            load = (image[mod + OFF_MOD_LOAD] >> 16) | ((image[mod + OFF_MOD_LOAD + 1] & 0xffff) << 16)
            if load != 0xffffffff:
                cfg.set(modname, 'Load', '{}M'.format(load >> 20))

            # Alternate FMH location
            if image[fmh + OFF_FMH_LOC] % block_size != 0:
                cfg.set(modname, 'FMHLoc', '0x{:04x}'.format(image[fmh + OFF_FMH_LOC]))

    def dump_module(fmh, mod, modname, dirname):
        tp = image[mod + OFF_MOD_TYPE] >> 16
        if tp == MODULE_FMH_FIRMWARE or tp == MODULE_FIRMWARE_1_4:
            return

        print(' -- processing {}...'.format(modname))
        _off = ((image[fmh + OFF_FMH_LOC] & (~0xffff)) >> 2) + (image[mod + OFF_MOD_LOC] >> 2)
        _len = image[mod + OFF_MOD_SIZE] >> 2
        image[_off:_off+_len].tofile('{}/{}.bin'.format(dirname, modname))

    summary = False
    block_size = 0
    image = np.memmap(fwimgname, dtype=np.uint32)
    if not block_size:
        block_size = 0x10 * 0x1000

    block_size >>= 2 # align by 4 bytes
    fwinfo = len(image) - block_size
    fmh = scan_for_fmh(image, fwinfo, block_size)
    if fmh == INVALID_FMH_OFFSET:
        raise Exception('Cannot find FMH header')
    mod = fmh + OFF_FMH_MODULE
    loc = image[fmh + OFF_FMH_LOC]

    modname = 'GLOBAL'
    if not summary:
        cfg.add_section(modname)
        cfg.set(modname, 'Output', os.path.basename(fwimgname))
        cfg.set(modname, 'FlashSize', '{}M'.format(len(image) >> 18))
        cfg.set(modname, 'BlockSize', '{}K'.format(block_size >> 8))

    dump_fwinfo(fwinfo+0x10)

    modname = name(mod)

    if summary:
        out.write('-----------------------------------\n')

    dump_fmh(fmh, mod, modname)

    fwinfo = 0
    while fwinfo < len(image) - block_size:
        fmh = scan_for_fmh(image, fwinfo, block_size)
        if fmh == INVALID_FMH_OFFSET:
            # Possible GAP
            fwinfo += block_size
            continue

        # Last block reached, stop
        if image[fmh + 4] == loc:
            break

        mod = fmh + OFF_FMH_MODULE
        modname = name(mod)

        dump_fmh(fmh, mod, modname)
        if not summary:
            dump_module(fmh, mod, modname, sys.argv[2]+'.dump')

        fwinfo += (image[fmh + OFF_FMH_ALLOC] >> 2) - block_size

    image.flush()
#    del image

if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '-b':
        cfg = cp.ConfigParser()
        cfg.optionxform = str
        cfg.read('genimage.ini')
        build_image(cfg)
    elif len(sys.argv) == 3 and sys.argv[1] == '-d':
        try: os.mkdir(sys.argv[2]+'.dump')
        except: pass
        cfg = cp.ConfigParser()
        cfg.optionxform = str
        with open(sys.argv[2]+'.dump/genimage.ini', 'w+') as f:
            dump_image(cfg, sys.argv[2])
            cfg.write(f)
    else:
        print('Usage: fmh.py [-d firmware.ima | -b]')
        sys.exit(1)
