PARSERDIR = ./iniparser-2.14

CFLAGS  = -Wall -I$(PARSERDIR)/src -m32 -g -Wno-format
#CFLAGS += -DDEBUG					# Uncomment to enable debug
LFLAGS  = -m32 -L$(PARSERDIR) -g
RM      = rm -f

all: genimage dumpimage
	rm fwinfo.o

$(PARSERDIR)/libini.a:
	@(make -C $(PARSERDIR) all)

genimage: genimage.o fmhcore.o fwinfo.o $(PARSERDIR)/libini.a
	@(echo "generating  genimage ...")
	@($(CC)  -o genimage genimage.o fwinfo.o fmhcore.o $(LFLAGS) -lini)

dumpimage: dumpimage.o fmhcore.o
	@(echo "generating  dumpimage ...")
	@($(CC)  -o dumpimage dumpimage.o fmhcore.o $(LFLAGS))


clean:
	@($(RM) genimage dumpimage *o)
	@(make -C $(PARSERDIR) clean)


COMPILE.c=$(CC) $(CFLAGS) -c
.c.o:
	@(echo "compiling $< ...")
	@($(COMPILE.c) -o $@ $<)

