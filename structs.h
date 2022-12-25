/*
    structs.h - Part of llf, a cross linker. Part of the macxx tool chain.
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

#ifndef _STRUCTS_H_
#define _STRUCTS_H_ 1

#include "add_defs.h"

#define EOL -2			/* end of line */

typedef struct cli_options {
   unsigned char cross;		/* /CROSS reference required */
   unsigned char full;		/* /FULL map spec required */
   unsigned char rel;		/* signal writing a relative output */
   unsigned char objdef;	/* default input type is .OBJ */
   unsigned char vldadef;	/* output format is to be vlda */
   unsigned char octal;		/* map to be displayed in octal */
   unsigned char err;		/* report all errors (for rel mode) */
   unsigned char debug;		/* debug mode */
   unsigned char option;	/* option switch input */
   unsigned char library;	/* library switch input */
   unsigned char miser;		/* miser mode */
   unsigned char quiet;		/* quiet the warnings about multiple definitions */
} CLI_options;

#if !defined(offsetof)
#define offsetof(s, m)    ((int)(&(((s *)0)->m)))
#endif

#define QUAL_CROSS offsetof(CLI_options, cross)
#define QUAL_FULL  offsetof(CLI_options, full)
#define QUAL_REL   offsetof(CLI_options, rel)
#define QUAL_OBJ   offsetof(CLI_options, objdef)
#define QUAL_VLDA  offsetof(CLI_options, vldadef)
#define QUAL_OCTAL offsetof(CLI_options, octal)
#define QUAL_ERR   offsetof(CLI_options, err)
#define QUAL_DEB   offsetof(CLI_options, debug)
#define QUAL_OPT   offsetof(CLI_options, option)
#define QUAL_LIB   offsetof(CLI_options, library)
#define QUAL_MISER offsetof(CLI_options, miser)
#define QUAL_QUIET offsetof(CLI_options, quiet)

extern CLI_options *options;	/* declare command options */

typedef struct my_desc {	/* define a descriptor structure cuz	*/
   unsigned short md_len;	/* the stock descrip definitions cause	*/
#ifdef VMS
   char md_type;		/* the compiler to think that they are	*/
   char md_class;		/* floating point data types.		*/
#endif
   char *md_pointer;		/* The stupid thing.			*/
} MY_desc;

#ifdef VMS
#define DSC$K_DTYPE_T 14
#define DSC$K_CLASS_S  1
#define $DESCRIPTOR(name,string) struct my_desc name = {\
   	sizeof(string)-1,\
   	DSC$K_DTYPE_T,DSC$K_CLASS_S,\
   	string };
#endif

typedef struct dbg_seclist {
   struct dbg_seclist *next;	/* ptr to next section */
   struct ss_struct *segp;	/* ptr to segment */
} DBG_seclist;

typedef struct fn_struct {
#ifdef VMS
   unsigned short d_length;	/* length of s_buff			*/
   char s_type;			/* descriptor constant (uses 0)		*/
   char s_class;		/* descriptor constant (uses 0)		*/
#endif
   char *fn_buff;		/* pointer to filename buffer		*/
   unsigned short r_length;	/* length of string stored in s_buff	*/
   char *fn_name_only;		/* filename without dvc/dir/ver stuff	*/
   long fn_min_id;		/* smallest identifier found in file	*/
   long fn_max_id;		/* largest identifier found in file	*/
   struct fn_struct *fn_next;	/* pointer to next filename desc	*/
   FILE *fn_file;		/* pointer to FILE structure		*/
   FILE_name *fn_nam;		/* pointer to file_name structure 	*/
   char *fn_credate;		/* pointer to creation dat string	*/
   char *fn_target;		/* pointer to target processor string	*/
   char *fn_xlator;		/* pointer to image that created file	*/
   char *od_name;		/* pointer to name of .od file */
   char *od_version;		/* pointer to version of .od file */
   DBG_seclist *od_seclist_top;	/* pointer to chain of od section list */
   DBG_seclist *od_seclist_curr;	/* ptr to next available section */
   unsigned fn_present:1;	/* this file is present (T/F)		*/
   unsigned fn_library:1;	/* this file is a library		*/
   unsigned fn_gotit:1;		/* this file has already been included  */
   unsigned fn_obj:1;		/* file is an .OBJ file			*/
   unsigned fn_rt11:1;		/* file is RT-11 format .OBJ file	*/
   unsigned fn_stb:1;		/* file is an symbol table file		*/
   unsigned fn_option:1;	/* file is an option file 		*/
   unsigned fn_nosym:1;		/* don't put symbols into .SYM file	*/
   unsigned fn_nostb:1;		/* don't put symbols into .STB file	*/
} FN_struct;

extern FN_struct *xfer_fnd;
extern FN_struct *current_fnd; /* global current_fnd for error handlers */
extern FN_struct output_files[OUT_FN_MAX]; /* output file structs */
#define map_fp output_files[OUT_FN_MAP].fn_file
#define sym_fp output_files[OUT_FN_SYM].fn_file
#define sec_fp output_files[OUT_FN_SEC].fn_file
#define abs_fp output_files[OUT_FN_ABS].fn_file
#define stb_fp output_files[OUT_FN_STB].fn_file
#define tmp_fp output_files[OUT_FN_TMP].fn_file
extern FN_struct *first_inp;
extern FN_struct *library( void );
extern FN_struct *inp_files,*option_file;

typedef struct df_struct {
   unsigned char df_len;
   struct ss_struct *df_ptr;
} DF_struct;

typedef struct seg_spec_struct {
   unsigned short seg_salign;	/* alignment of group/segment in memory */
   unsigned short seg_dalign;	/* alignment of data within segment */
   unsigned long seg_base;	/* group/segment base */
   unsigned long seg_len;	/* length of group/segment */
   unsigned long seg_maxlen;	/* maximum length for the group */
   unsigned long seg_offset;	/* output offset to apply to segment */
   struct ss_struct *seg_reloffset; /* output is placed in this segment */
   struct ss_struct *seg_group; /* pointer to group owning this segment */
   struct ss_struct *seg_first;	/* pointer to first segment in cluster */
   unsigned sflg_reloffset:1;	/* section is output into another segment */
   unsigned sflg_absolute:1;	/* an absolute section */
   unsigned sflg_zeropage:1;	/* a zero page section */
   unsigned sflg_data:1;	/* a data segment */
   unsigned sflg_ro:1;		/* read only */
   unsigned sflg_noref:1;	/* segment has no references to it */
   unsigned sflg_fit:1;		/* group is to be fit */
   unsigned sflg_stable:1;	/* keep named segments in order */
   unsigned sflg_literal:1;	/* literal pool */
} SEG_spec_struct;

typedef struct ss_struct {
   unsigned  flg_segment:1;	/* this is a segment struct */
   unsigned  flg_symbol:1;	/* this is a symbol struct */
   unsigned  flg_group:1;	/* this is a group struct */
   unsigned  flg_member:1;	/* this segment is a member of a group */
   unsigned  flg_defined:1;	/* symbol/section defined */
   unsigned  flg_local:1;	/* symbol is local to this module */
   unsigned  flg_exprs:1;	/* symbol definition is expression */
   unsigned  flg_abs:1;		/* symbol/section an absolute value */
   unsigned  flg_ovr:1;		/* section is unique */
   unsigned  flg_more:1;	/* section name following has the same name */
   unsigned  flg_based:1;	/* segment has been based */
   unsigned  flg_ident:1;	/* segment/symbol has a new identifier assigned */
   unsigned  flg_libr:1;	/* symbol found in a library already */
   unsigned  flg_stb:1;		/* symbol defined by an .STB file */
   unsigned  flg_noout:1;	/* don't output this segment */
   unsigned  flg_nosym:1;	/* don't output this symbol to .SYM file */
   unsigned short ss_strlen;	/* symbol name string length (also sym ID) */
   long ss_value;		/* symbol value */
   char *ss_string;		/* pointer to ASCII identifier name */
   struct ss_struct *ss_next; 	/* pointer to next node */
   struct ss_struct **ss_prev;	/* pointer to previous structs next ptr */
   struct fn_struct *ss_fnd;	/* pointer to fnd of first reference */
   struct fn_struct **ss_xref;	/* pointer to cross reference table */
   struct exp_stk *ss_exprs;	/* pointer to expression definition area */
   struct seg_spec_struct *seg_spec; /* pointer to segment relevant items */
} SS_struct;

#define ss_ident ss_strlen	/* equate strlen to ident */

extern SS_struct **id_table;  /* pointer to id_table */
extern SS_struct *first_symbol; /* pointer to first if duplicates */
extern short new_symbol;		/* symbol insertion flag */
   				/* value (additive) */
   				/*   1 - symbol added to symbol table */
   				/*   2 - symbol added to hash table */
   				/*   4 - symbol is duplicated */

extern struct ss_struct *group_list_default; /* pointer to default group name */
extern SS_struct *hash[]; /* hash table is array of pointers */
extern SS_struct *base_page_nam;
extern SS_struct *abs_group_nam;
extern SS_struct *last_seg_ref;
extern SS_struct *sym_lookup( char *strng, long strlen, int err_flag);
extern SS_struct *sym_delete( SS_struct *old_ptr );
extern int write_to_symdef( SS_struct *ptr );
extern void do_xref_symbol( SS_struct *sym_ptr, unsigned int def);
extern void insert_id( long id, SS_struct *id_ptr);
extern SEG_spec_struct *get_seg_spec_mem( SS_struct *sym_ptr);
extern SS_struct *get_symbol_block( int flag );
extern SS_struct **find_seg_in_group(SS_struct *, SS_struct **);
extern int chk_mdf(int flag, SS_struct *sym_ptr, int quietly);
extern void outseg_def(SS_struct *sym_ptr, unsigned long len, int based );

typedef struct grp_struct {
   struct ss_struct **grp_top;	/* top of group list */
   struct ss_struct **grp_next;	/* next free entry */
   long grp_free;		/* number of free entries left */
} GRP_struct;

extern int add_to_group( SS_struct *sym_ptr, SS_struct *grp_nam,GRP_struct *grp_ptr);
extern void insert_intogroup( GRP_struct *grp_ptr, SS_struct *sym_ptr, SS_struct *grp_nam );
extern GRP_struct *get_grp_ptr( SS_struct *grp_nam, long align, long maxlen );

typedef struct ofn_struct {
   struct fn_struct ofn[OUT_FN_MAX];
} OFN_struct;

extern OFN_struct *out_files;

typedef union token_union {
   char *s_ptr;
   long *i_ptr;
} TOKEN_union;

typedef struct token_struct {
   char token_type;
   union token_union token_;
} TOKEN_struct;

typedef struct expr_token {
   unsigned char expr_code;	/* expression code */
   long expr_value;		/* value */
   struct ss_struct *expr_ptr;	/* pointer to term's symbol/seg */
} EXPR_token;

typedef struct exp_stk {
   int len;
   struct expr_token *ptr;
} EXP_stk;

extern int evaluate_expression(struct exp_stk *eptr);

typedef struct rm_struct {	/* reserved memory list */
   unsigned long rm_start;	/* start address */
   unsigned long rm_len;	/* length of area to exclude */
   struct rm_struct *rm_next;	/* pointer to next element */
} RM_struct;

typedef struct rm_control {
   RM_struct **list;		/* pointer to array of pointers to arrays of RM_struct's */
   RM_struct *next;		/* next element to use */
   RM_struct *top;		/* first element in the whole chain */
   unsigned long pool_used;	/* total memory used */
   int used;			/* number of entries used in the array */
   int size;			/* number of entries avaiable in array */
   int free;			/* number of free elements in the array */
} RM_control;

extern RM_control *rm_control; 	/* reserved memory info */
extern RM_control *clone_rm_mem(RM_control *rmc);
extern void free_rm_mem(RM_control **rmcp);

extern int token_pool_size;
extern char *token_pool;
extern long token_value;
extern int lc_pass;            /* pass count through lc() */
extern long unix_time;
extern int inp_str_size;
extern int option_input;	/* option file processing flag */
extern int debug;		/* debug options */
extern char *fn_pool;		/* pointer to filename buffer */
extern int fn_pool_size;	/* size remaining in filename buffer */
extern GRP_struct *base_page_grp;
extern GRP_struct *abs_group;
extern char *null_string;
extern long misc_pool_used;
extern long sym_pool_used;
extern FILE *outxabs_fp;
extern int outx_lineno,outx_width,outx_debug;
extern char *eline;
extern long misc_pool_used;

extern struct grp_struct *group_list_top;  /* pointer to top of group list */
extern struct grp_struct *group_list_next; /* pointer to next free space */

extern EXPR_token expr_stack[]; /* expression stack */
extern int expr_stack_ptr;  /* expression stack pointer */

extern void puts_map( char *string, int lines );
extern int get_c( void );
extern int get_text( void );
extern struct fn_struct **get_xref_pool( void );

extern char *err2str( int num );
extern void write_to_tmp( int typ, long itm_cnt, char *itm_ptr, int itm_siz );
extern int exprs( int flag );

extern char def_ob[],def_lb[],def_obj[],def_stb[];
extern void lap_timer( char *str );
extern void object( int fd );
extern void pass1( void );
extern int pass2( void );
extern void outid( FILE *fp, int mode );
extern void mapsym( void );
extern void display_mem( void );
extern void show_timer( void );
extern int lc( void );
extern void outx_init( void );
extern void seg_locate( void );
extern void termsym( long taddr );
extern void flushsym( int mode );
extern int out_dbgod( int mode, FILE *absfp, FN_struct *fnp );
extern void symbol_definitions( void );
extern void flushobj( void );
extern void sort_symbols( void );
extern int getcommand( void );
extern int display_help( void );
#if defined(__GNUC__)
#if !defined(fileno)
extern int fileno(FILE *);
#endif
#endif

extern struct fn_struct *get_fn_struct();
extern void termobj( long traddr );
extern int outtstexp(int typ, char *asc, int alen, EXP_stk *exp);
extern char *outexp(EXP_stk *eptr, char *s, int tag, long tlen, char *wrt, FILE *fp);
extern void outbstr( unsigned char *from, int len );
extern void outorg( unsigned long address, EXP_stk *exp_ptr );
extern char *outxfer( EXP_stk *exp, FILE *fp );
extern void outsym_def(SS_struct *sym_ptr, int mode );

#endif /* _STRUCTS_H_ */


