bmc-ami
=======

Dump image
=============
```sh
$ dumpimage -i FIRMWARE.IMA -o OUTPUT-DIRECTORY
```

Generate image
==============
From the directory containing "genimage.ini" run:
```sh
$ genimage
```

u-Boot image
============
First check current flags:
```sh
$ mkimage -l ROOT.bin
Image Name:
Created:      Thu Dec  6 23:09:39 2012
Image Type:   SuperH Linux RAMDisk Image (uncompressed)
Data Size:    15495168 Bytes = 15132.00 kB = 14.78 MB
Load Address: 00000000
Entry Point:  00000000
```

To re-create u-Boot image (for SuperH arch use "sh"):
```sh
$ mkimage -A sh -O linux -T ramdisk -C none -d ROOT-new.cramfs ROOT-new.bin
```

Manage the MTD image
====================
1. Expand extracted CONF.bin to the minimal erase block count (8 by 64k, maximum is around 1.5M):  
```$ s=$(stat -c "%s" CONF.bin)```  
```$ dd if=/dev/zero of=CONF.bin seek=$s bs=1 count=$((0x80000 - $s))```
2. Associate /dev/loopX with CONF.bin via losetup:  
    ```$ losetup /dev/loop0 CONF.bin```
3. Associate /dev/loopX with MTD char device (erase size is 64k):  
```$ modprobe block2mtd block2mtd=/dev/loop0,0x10000```
4. Insmod MTDBLOCK module to mount image:  
```$ modprobe mtdblock```  
```$ mount -tjffs2 /dev/mtdblock0 MOUNT_POINT```
5. Modify content on your own
6. Re-create MTD image:
``` mkfs.jffs2 -c 12 -l -f -s 0x800 -q -x lzo  --pad=0x00170000 -e 0x10000 -r MOUNT_POINT -o CONF-NEW.bin```
6. Remove modules & deassociate loop-back device
```sh
$ rmmod mtdblock; rmmod block2mtd
$ losetup -d /dev/loop0
```
