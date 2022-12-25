/*
    gctable.c - Part of llf, a cross linker. Part of the macxx tool chain.
    Copyright (C) 2008 David Shepperd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "token.h"
#include "structs.h"
#include "gcstruct.h"

/***********************************************************************
 * NOTE: If you change the order of any of the following items, you'll
 * also have to change the xxx_desc definitions in GC.C
 ***********************************************************************/

struct qual qual_tbl[] = {
    {0,0,0,0,0,0,0,0,0,"OPTIONS",0,QUAL_OPT},    /* no value on this option */
    {0,0,0,0,0,0,0,0,0,"LIBRARY",0,QUAL_LIB},    /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"CROSS_REFERENCE",0,QUAL_CROSS},  /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"RELATIVE",0,QUAL_REL},   /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"ERROR",0,QUAL_ERR},  /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"VLDA",0,QUAL_VLDA},  /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"OBJECT",0,QUAL_OBJ}, /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"OCTAL",0,QUAL_OCTAL},    /* no value on this option */
    {0,1,1,0,0,1,0,0,0,"DEBUG",0,QUAL_DEB},
    {0,1,0,1,0,1,0,0,OUT_FN_TMP,"TEMPFILE",0,0},
    {0,1,0,1,0,1,0,0,OUT_FN_ABS,"OUTPUT",0,0},   /* all the rest may have... */
    {0,1,0,1,0,1,0,0,OUT_FN_SYM,"SYMBOL",0,0},   /* ...filename values	   */
    {0,1,0,1,0,1,0,0,OUT_FN_MAP,"MAP",0,0},
    {0,1,0,1,0,1,0,0,OUT_FN_SEC,"SECTION",0,0},
    {0,1,0,1,0,1,0,0,OUT_FN_STB,"STB",0,0},
    {1,0,0,0,0,1,0,0,0,"BINARY",0,QUAL_VLDA},    /* no value on this option */
    {1,0,0,0,0,1,0,0,0,"MISER",0,QUAL_MISER}, /* no value on this option */
	{1,0,0,0,0,1,0,0,0,"QUIET",0,QUAL_QUIET}	/* no value on this option */
};

const int max_qual = sizeof(qual_tbl)/sizeof(struct qual);

#define DEFTYP(nam,string) char nam[] = {string};

#if defined(VMS) || defined(MS_DOS)
DEFTYP(def_map,".MAP")
DEFTYP(def_stb,".STB")
DEFTYP(def_sym,".SYM")
DEFTYP(def_sec,".SEC")
DEFTYP(def_hex,".HEX")
DEFTYP(def_ln, ".LN")
DEFTYP(def_lb, ".LB")
DEFTYP(def_opt,".OPT")
DEFTYP(def_vlda,".VLDA")
DEFTYP(def_ol, ".OL")
DEFTYP(def_obj,".OB")
DEFTYP(def_ob,".ob")
DEFTYP(def_lib,".LIB")
DEFTYP(def_tmp,".")
#else
DEFTYP(def_map,".map")
DEFTYP(def_stb,".stb")
DEFTYP(def_sym,".sym")
DEFTYP(def_sec,".sec")
DEFTYP(def_hex,".hex")
DEFTYP(def_ln, ".ln")
DEFTYP(def_lb, ".lb")
DEFTYP(def_opt,".opt")
DEFTYP(def_vlda,".vlda")
DEFTYP(def_ol, ".ol")
DEFTYP(def_obj,".ob")
DEFTYP(def_ob,".ob")
DEFTYP(def_lib,".lib")
DEFTYP(def_tmp,".")
#endif
