#
# iniparser Makefile
#

CC      = gcc -m32
CFLAGS  = -O3 -Wall -g
AR	    = ar
ARFLAGS = rcv
RM      = rm -f

# Implicit rules

SUFFIXES = .o .c .h .a .so .sl

COMPILE.c=$(CC) $(CFLAGS) -c
.c.o:
	@(echo "compiling $< ...")
	@($(COMPILE.c) -o $@ $<)


SRCS = src/iniparser.c \
	   src/dictionary.c \
	   src/strlib.c

OBJS = $(SRCS:.c=.o)

all:	libini.a

libini.a:	$(OBJS)
	@($(AR) $(ARFLAGS) libini.a $(OBJS))

clean:
	@($(RM) $(OBJS) libini.a)

