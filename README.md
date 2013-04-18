bmc-ami
=======

Dump image
=============
$ dumpimage -i FIRMWARE.IMA -o OUTPUT-DIRECTORY

Generate image
==============
From the directory containing "genimage.ini" run:
$ genimage

u-Boot image
============
First check current flags:
$ mkimage -l ROOT.bin
Image Name:
Created:      Thu Dec  6 23:09:39 2012
Image Type:   SuperH Linux RAMDisk Image (uncompressed)
Data Size:    15495168 Bytes = 15132.00 kB = 14.78 MB
Load Address: 00000000
Entry Point:  00000000

To re-create u-Boot image (for SuperH arch use "sh"):
$ mkimage -A sh -O linux -T ramdisk -C none -d ROOT-new.cramfs ROOT-new.bin
