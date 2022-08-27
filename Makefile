# $Id$
# LLF makefile
# 080915 - D. Shepperd

A1 = llf.o gc.o gctable.o pass1.o pass2.o memmgt.o symbol.o reserve.o
A2 = hashit.o insert_id.o lc.o grpmgr.o reloc.o symdef.o help.o 
A3 = outx.o mapsym.o timer.o object.o qksort.o err2str.o add_defs.o

AC1 = llf.c gc.c gctable.c pass1.c pass2.c memmgt.c symbol.c reserve.c
AC2 = hashit.c insert_id.c lc.c grpmgr.c reloc.c symdef.c help.c 
AC3 = outx.c mapsym.c timer.c object.c qksort.c err2str.c add_defs.c

OBJ_FILES = $(A1) $(A2) $(A3)

ALLH = version.h token.h structs.h header.h add_defs.h

# Extra defines available #-DALIGNMENT=2 #-DBIG_ENDIAN #-DDEBUG_MALLOC #-DDEBUG_LINK 
DEFINES = -DLLF -DM_UNIX -DEXTERNAL_PACKED_STRUCTS -D_ISOC99_SOURCE

MODE = -m32

INCS = -I.
OPT = -O2
DBG = #-g3
CFLAGS = $(OPT) $(DBG) $(DEFINES) $(MODE) $(INCS) -std=c99 -Wall -ansi -pedantic -Wno-char-subscripts
CC = gcc 
CO = co

ECHO = /bin/echo -e

C = $(CC) -c $(CFLAGS)
D = $(CC) -c $(CFLAGS)
L = $(CC) $(DBG) $(MODE)

default: llf vecextract
	$(ECHO) "\tDone..."

% :
	@echo "Hmmm...Don't know how to make $@..."

.SILENT:

llf : $(OBJ_FILES) Makefile
	@$(ECHO) "\tlinking..."
	$L -o $@ $(filter-out Makefile,$^)

vecextract : vecextract.c vlda_structs.h segdef.h
	$(ECHO) "\tBuilding vecextract..."
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE -o $@ $<
        
llf.ln : Makefile
	llf -OUT=llf -MAP=llf.tmap -err -rel -deb llfst -opt

%.o : %.c
	$(ECHO) "\tCompiling $<..."
	$(CC) $(CFLAGS) -c -DFILE_ID_NAME=$(basename $<)_id $(SUPPRESS_FILE_ID) $<

clean:
	/bin/rm -rf *.o *.lis llf vecextract core* Debug Release

add_defs.o: add_defs.c add_defs.h memmgt.h
err2str.o: err2str.c 
gc.o: gc.c token.h structs.h add_defs.h header.h memmgt.h gcstruct.h version.h
gctable.o: gctable.c token.h structs.h add_defs.h gcstruct.h
grpmgr.o: grpmgr.c  token.h structs.h add_defs.h header.h memmgt.h
hashit.o: hashit.c
help.o: help.c memmgt.h
insert_id.o: insert_id.c token.h structs.h add_defs.h header.h memmgt.h
lc.o: lc.c token.h structs.h add_defs.h header.h memmgt.h
llf.o: llf.c version.h add_defs.h token.h structs.h header.h memmgt.h
mapsym.o: mapsym.c version.h token.h structs.h add_defs.h header.h memmgt.h qksort.h
memmgt.o: memmgt.c 
object.o: object.c version.h token.h structs.h add_defs.h header.h memmgt.h \
  vlda_structs.h pragma1.h segdef.h pragma.h exproper.h
outx.o: outx.c version.h token.h structs.h add_defs.h header.h memmgt.h \
  vlda_structs.h pragma1.h segdef.h pragma.h exproper.h
pass1.o: pass1.c token.h structs.h add_defs.h header.h memmgt.h exproper.h
pass2.o: pass2.c token.h structs.h add_defs.h header.h memmgt.h vlda_structs.h pragma1.h \
  segdef.h pragma.h
qksort.o: qksort.c token.h structs.h add_defs.h qksort.h
reloc.o: reloc.c version.h token.h structs.h add_defs.h header.h memmgt.h
reserve.o: reserve.c token.h structs.h add_defs.h header.h memmgt.h
symbol.o: symbol.c token.h structs.h add_defs.h header.h memmgt.h
symdef.o: symdef.c token.h structs.h add_defs.h header.h memmgt.h exproper.h
timer.o: timer.c token.h structs.h add_defs.h header.h memmgt.h
