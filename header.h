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

#ifdef LINTING
#ifdef stdin
#undef stdin
#undef stdout
#undef stderr
extern FILE *stdin,*stdout,*stderr;
#endif
#endif
extern short pass;		/* pass number indicator */
extern short output_mode;		/* output mode */
extern char *map_subtitle;	/* pointer to map subtitle */
extern char *inp_str;		/* temp string area */

#include "memmgt.h"

extern long id_table_size;	/* size of ID number table */
extern long id_table_base;	/* offset for ID number table */
extern long grp_pool_used;	/* amount of memory used for grps */
extern short new_ident;		/* new identifier assignment */

extern long group_list_free;		   /* number of free spaces left */
extern void err_msg();		/* error message routine */
extern char emsg[];		/* error message buffer */
extern int error_count[5];
extern int gc_argc;		/* arg count */
extern char **gc_argv;	/* arg value */

extern char *def_lib_ptr[];
extern char *def_obj_ptr[];
extern int info_enable;
extern long token_value;    /* value of current token */
extern int token_type;      /* token type */
extern char *token_pool;    /* pointer to token pool */
extern int token_pool_size;     /* size of remaining token pool */
extern char *inp_ptr;       /* pointer to char in inp_str */
extern char *tkn_ptr;       /* pointer to char in inp_str */
extern char *inp_str;       /* external array of input chars */
extern char err_str[];      /* external error string array */
extern int err_str_size;
extern void add_to_reserve(unsigned long start, unsigned long len);
extern int check_reserve(unsigned long start, unsigned long len);
extern int get_free_space(unsigned long len, unsigned long *start, int align);
extern int hashit( char *strng, int hash_size );
extern void sym_stats( void );

extern FILE *outxsym_fp;	/* global pointer for outx routines */
extern FILE *outxabs_fp;
extern int outx_width,outx_swidth;  /* outx default record length */

extern char ascii_date[];	/* need pointer to current date string */
extern long tot_ids,max_idu;
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

extern long fn_pool_used;
extern long xref_pool_used;
extern long grp_pool_used;
extern long rm_pool_used;
extern long misc_pool_used;
extern long tmp_pool_used;
extern long sym_pool_used;
extern long symdef_pool_used;
extern long total_mem_used;
extern long peak_mem_used;
extern int map_line;        /* lines remaining on map page */
extern long record_count;    /* total records input */
extern long object_count;    /* total records input */


#endif /* _HEADER_H_ */
