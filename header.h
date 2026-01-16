/*
    header.h - Part of llf, a cross linker. Part of the macxx tool chain.
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

#ifndef _HEADER_H_
#define _HEADER_H_

#include <inttypes.h>
#include "formats.h"

#ifndef n_elts
#define n_elts(x) (sizeof(x)/sizeof((x)[0]))
#endif

#ifdef LINTING
#ifdef stdin
#undef stdin
#undef stdout
#undef stderr
extern FILE *stdin,*stdout,*stderr;
#endif
#endif
extern int16_t pass;		/* pass number indicator */
extern int16_t output_mode;		/* output mode */
extern char *map_subtitle;	/* pointer to map subtitle */
extern char *inp_str;		/* temp string area */

#include "memmgt.h"

extern int32_t id_table_size;	/* size of ID number table */
extern int32_t id_table_base;	/* offset for ID number table */
extern int32_t grp_pool_used;	/* amount of memory used for grps */
extern int16_t new_ident;		/* new identifier assignment */

extern int32_t group_list_free;		   /* number of free spaces left */
extern void err_msg(int severity, const char *msg);		/* error message routine */
#define EMSG_SIZE (512)
extern char emsg[EMSG_SIZE];	/* error message buffer */
extern int error_count[5];
extern char *commandLine;

extern char *def_lib_ptr[];
extern char *def_obj_ptr[];
extern int info_enable;
extern int32_t token_value;    /* value of current token */
extern int token_type;      /* token type */
extern char *token_pool;    /* pointer to token pool */
extern int token_pool_size;     /* size of remaining token pool */
extern char *inp_ptr;       /* pointer to char in inp_str */
extern char *tkn_ptr;       /* pointer to char in inp_str */
extern char *inp_str;       /* external array of input chars */
extern char err_str[];      /* external error string array */
extern int err_str_size;
extern void add_to_reserve(uint32_t start, uint32_t len);
extern int check_reserve(uint32_t start, uint32_t len);
extern int get_free_space(uint32_t len, uint32_t *start, int align);
extern unsigned int hashit( char *strng, int hash_size );
extern void sym_stats( void );

extern FILE *outxsym_fp;	/* global pointer for outx routines */
extern FILE *outxabs_fp;
extern int outx_width,outx_swidth;  /* outx default record length */

extern char ascii_date[];	/* need pointer to current date string */
extern int32_t tot_ids,max_idu;
extern char *target;        /* pointer to "target" string */

extern char def_map[],
def_stb[],
def_sym[],
def_sec[],
def_hex[],
def_ln[],
def_lb[],
def_opt[],
def_vlda[],
def_ol[],
def_obj[],
def_lib[],
def_tmp[];

extern int32_t fn_pool_used;
extern int32_t xref_pool_used;
extern int32_t grp_pool_used;
extern int32_t rm_pool_used;
extern int32_t misc_pool_used;
extern int32_t tmp_pool_used;
extern int32_t sym_pool_used;
extern int32_t symdef_pool_used;
extern int32_t total_mem_used;
extern int32_t peak_mem_used;
extern int map_line;        /* lines remaining on map page */
extern int32_t record_count;    /* total records input */
extern int32_t object_count;    /* total records input */
extern int haveLiteralPool;

#endif /* _HEADER_H_ */
