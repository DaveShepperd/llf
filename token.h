/*
    token.h - Part of llf, a cross linker. Part of the macxx tool chain.
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

#ifndef _TOKEN_H_
#define _TOKEN_H_ 1

#include <inttypes.h>

/* The following are token codes for pass2 decode */

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

enum token {
	MAX_TOKEN 	=2048,		/* maximum length for token strings and input string */
	MAX_LINE  	=264,		/* maximum length for output line */
	
	HASH_TABLE_SIZE =3001,	/* hash table size (prime number) */
	
	XREF_BLOCK_SIZE =4,		/* 3 filenames/xref block + link to next */
	
	TOKEN_cmd      =0,   /* command char follows (char) */
	TOKEN_ID       =1,   /* ID string follows (s) */
	TOKEN_ID_num   =2,   /* ID enumerator follows (int) */
	TOKEN_const    =3,   /* constant follows (long) */
	TOKEN_oper     =4,   /* expression operator follows (char) */
	TOKEN_expr_tag =5,	/* expression tag follows (char) */
	TOKEN_bins     =6,   /* binary string */
	TOKEN_ascs     =7,   /* ASCII text string follows (s) */
	TOKEN_LB       =8,   /* Length/Base operator follows (char) */
	TOKEN_sep      =9,   /* line seperator */
	TOKEN_repf    =10,   /* replication factor follows (long) */
	TOKEN_uc      =11,   /* segment alignment code u or c */
	TOKEN_LINK   =126,	/* link to next segment */
	TOKEN_EOF    =127,	/* end of file */
	
	TMP_NUM    =0x7F,	/* constant value between -64 and + 63 */
	TMP_NNUM   =0x80,	/* Not a number */
	TMP_SIZE   =0x03,	/* bit mask for size bits */
	TMP_ZERO   =0x00,	/* Value is 0 */
	TMP_B8     =0x01,	/* Value is 8 bits */
	TMP_B16    =0x02,	/* Value is 16 bits */
	TMP_B32x   =0x03,	/* value is 32 (or 64) bits */
	TMP_EOF    =0x80,	/* tmp file entry is end of file */
	TMP_EXPR   =0x84,	/* tmp file entry is a expression */
	TMP_ASTNG  =0x88,	/* ascii string follows */
	TMP_TEST   =0x8C,	/* test expression follows */
	TMP_TAG    =0x90,	/* expression tag */
	TMP_ORG    =0x94,	/* change PC */
	TMP_START  =0x98,	/* set the starting address */
	TMP_BSTNG  =0x9C,	/* binary string follows */

/* Codes in the sequence 0xA0...0xDC reserved for expression specific stuff (see EXPR_xxx below) */

	TMP_SCOPE  =0xE4,	/* change procedure block */
	TMP_STAB   =0xE8,	/* debug info record */
	TMP_BOFF   =0xEC,	/* branch offset out of range test */
	TMP_OOR    =0xF0,	/* tom/jerry operand value out of range test */
	TMP_LINK   =0xFF,	/* link to next temp segment */

/* WARNING... The following codes are actually 8  bits codes with bit 8 assumed
 * 	to be 1. Therefore these codes fit into the TMP_xxx range beginning
 *	with 0xA0 in increments of 4. (0xAx-0xDC reserved for EXPR_xxx)
 */

	EXPR_SYM   =0x20,	/* expression component is a symbol */
	EXPR_VALUE =0x24,	/* expression component is an absolute value */
	EXPR_OPER  =0x28,	/* expression component is an operator */
	EXPR_L     =0x2C,	/* expression component is an L */
	EXPR_B     =0x30,	/* expression component is a  B */
	EXPR_TAG   =0x34,	/* expression tag follows */
	EXPR_IDENT =0x38,	/* symbol identifier follows */
	EXPR_LINK  =0x3C,	/* link to another expression */
	
	OUTPUT_HEX =0,	/* output mode tekhex */
	OUTPUT_OL,		/* output mode relative ascii */
	OUTPUT_VLDA,		/* output absolute binary */
	OUTPUT_OBJ,		/* output relative binary */
	
	OUT_FN_ABS =0,	/* indicies into the output fn_struct table */
	OUT_FN_MAP,
	OUT_FN_STB,
	OUT_FN_SYM,
	OUT_FN_SEC,
	OUT_FN_TMP,
	OUT_FN_MAX,		/* maximum # of output files */
	
	MSG_WARN	=0,	/* error message severities */
	MSG_SUCCESS,
	MSG_ERROR,
	MSG_INFO,
	MSG_FATAL,
	MSG_CONT		/* continue message */
};

extern void exit(int);

#ifdef VMS
#define EXIT_FALSE exit (0x10000000)	/* exit code for quiet failure */
#define EXIT_TRUE  exit (0x10000001)	/* exit code for quiet success */
#else
#define EXIT_FALSE exit (1)		/* exit code for failure */
#define EXIT_TRUE  exit (0)		/* exit code for success */
#endif

#endif /* _TOKEN_H_ */
