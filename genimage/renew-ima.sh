#!/bin/sh -e

PROG=${0##*/}

usage()
{
    cat <<EOF
ASMBx renewal util

$PROG [options]

  -p,--platform MODEL    Platform MODEL (required)
  -m,--mac MAC           IPMI MAC address (required)
  -d,--dump FILE         Input dump file
  -o,--output FILE       Output file

  --help                 Show this help

E.g. $PROG -p Z8NR-D12 -m bc:ae:c5:03:dc:dd
   will create output file \`Z8NR-D12-bcaec503dcdd.ima'

For bugs & features please contact Anton D. Kachalov <mouse@yandex-team.ru>
EOF
    [ "$1" = '-n' ] || exit ${1:-2}
}

fatal_msg()
{
    echo "FATAL: $@" 1>&2
    exit 1
}

fatal_usage_msg()
{
    usage -n
    fatal_msg "$@"
}

TEMP=`getopt -o p:d:o:m:h --long platofrm:,dump:,output:,mac:,help -n "$PROG" -- "$@" || usage 1>&2`

eval set -- "$TEMP"

opt_offset=$((0x30000))
opt_platform=
opt_dump=
opt_output=
opt_mac=
while :; do
    case "$1" in
    -p|--platform) shift
        opt_platform="$1"
        ;;
    -d|--dump) shift
        opt_dump="$1"
        ;;
    -o|--output) shift
        opt_output="$1"
        ;;
    -m|--mac) shift
        opt_mac="$(echo $1 | tr '[[:lower:]]' '[[:upper:]]')"
        ;;
    -h|--help)
        usage 0
        ;;
    --) shift
        break
        ;;
    *)
        usage 1
        ;;
    esac
    shift
done

[ -n "$opt_platform" ] || fatal_usage_msg "Platform not specified"

[ -n "$opt_mac" ] || fatal_usage_msg "IPMI MAC not specified"

echo -n "$opt_mac" | egrep -q '^[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}$' ||
    fatal_msg "MAC ($opt_mac) is not in right format (aa:bb:cc:dd:ee:ff)"

[ -e "stock/$opt_platform.ima" ] ||
    fatal_msg "Platform $opt_platform is not supported. Available platforms:\n$(ls -1 stock/*.ima | sed "s,\.ima$,,; s,^stock/,\t,")"

[ -n "$opt_output" ] || opt_output="$opt_platform-$(echo -n "$opt_mac" | tr '[[:upper:]]' '[[:lower:]]' | sed 's,:,,g').bin"

[ -z "$opt_dump" ] || ./dumpimage -s -i "$opt_dump" -f "$(printf "%#x" $(($(stat -c %s stock/$opt_platform.ima) - 0x10000)))"

case "$(($(stat -c %s stock/$opt_platform.ima)/0x100000))" in
32) # ASMB6
    flash_size=32M
    cmdline=`cat <<EOF
bootcmd=bootfmh
bootdelay=3
baudrate=38400
loads_echo=1
autoload=no
ethaddr=$opt_mac
eth1addr=$opt_mac
stdin=serial
stdout=serial
stderr=serial
ethact=astnic#0
boot_fwupd=0
mode=1
EOF`
    ;;
16|*) # ASMB4
    flash_size=16M
    cmdline=`cat <<EOF
bootcmd=bootfmh
bootdelay=3
baudrate=38400
loads_echo=1
autoload=no
stdin=serial
stdout=serial
stderr=serial
ethact=ast_eth0
ethaddr=$opt_mac
eth1addr=$opt_mac
EOF`
    ;;
esac

# Copy part prior NVRAM space
dd if=stock/$opt_platform.ima of=$opt_output bs=$((0x10000)) count=3

# Create NVRAM
echo "$cmdline" | awk -vcnt=0 '
{ cnt += length($0)+1; printf "%s%c", $0, 0 }
END { for (i=65528-cnt; i > 0; i--) printf "%c", 0 }' > $opt_output.cmdline

# Calculate CRC32 for NVRAM and append it to the output
cksfv -c $opt_output.cmdline | awk -vok=0 '
$1 != ";" { for (i=7; i>0; i-=2) printf "%c", int("0x" substr($2, i, 2)); ok = 1 }
END { if (!ok) exit(1) }' >> $opt_output

# Output NVRAM
cat $opt_output.cmdline >> $opt_output

# Append 0xFFFF_FFFF
awk 'END {printf "%c%c%c%c", 255, 255, 255, 255}' < /dev/null >> $opt_output

rm -f $opt_output.cmdline

# Append rest of firmware image
dd if=stock/$opt_platform.ima of=$opt_output bs=$((0x40000)) skip=1 seek=1

# Pad with zeroes to the flash size
dd if=$opt_output of=$opt_output bs=$flash_size conv=notrunc,sync count=1
