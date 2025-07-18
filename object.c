/*
    object.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

/*************************************************************************
 *
 *	OBJ.C - RT-11/RSX/VLDA object file handler
 *
 *************************************************************************/

/*************************************************************************
 * The RT11/RSX object file format is as follows:
 *
 * The first byte of the first word in the record contains the record
 * type code. The allowable codes are as follows:
 *	1 - Global Symbol Directory (GSD) 
 *	2 - End of GSD (ignored in this routine)
 *	3 - Text record (TXT, contains binary image data)
 *	4 - Relocation Directory (RLD) (patches bytes/words in TXT)
 *	5 - Internal Symbol Directory (ISD, ignored in this routine)
 *	6 - End of Module record
 * The second byte of the first word is always 0, but it is ignored in
 * this routine anyway.
 *
 * Subsequent bytes/words in the record are interpreted depending on the
 * record type. All GSD items are the same length; RLD items are variable
 * length; TXT is raw binary data which is passed straight through to the
 * output. ISD records are currently ignored; End GSD and End Module
 * records have no additional data in them. See gsdstruct for layout of
 * the GSD item(s) but various fields may not be used in all GSD item
 * types. An RLD item consists of a minimum of 16 bits where the 6 least
 * significant bits express the type, bit 6 indicates Motorola 6800 mode
 * addressing (increasing addresses=decreasing significance), bit 7
 * specifies that the RLD applies to a byte quantity and the remaining
 * 8 bits specify a displacement (+4) from the start of a previous
 * TXT record (hence, backpatching is the name of the game with this
 * scheme).
 */

#include "version.h"
#ifdef VMS
    #include <file.h>
    #define lib_ediv lib$ediv
#else
    #define lib_ediv(a,b,c,d) 1
#endif
#include <stdio.h>		/* get standard I/O definitions */
#include <ctype.h>		/* get standard string type macros */
#include <string.h>
#include <unistd.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get our standard stuff */
#include "vlda_structs.h"

#include "exproper.h"

#ifndef VMS
static uint16_t rsize;
static int even_odd;
#endif
#if 0
extern char emsg[];     /* space to build error messages */
extern char *inp_str;       /* place to hold input text */
extern char *tkn_ptr;       /* pointer to start of token */
extern int32_t token_value;    /* value of converted token */
extern int  token_type;     /* token type */
extern char token_end;      /* terminator char for string tokens */
extern char *token_pool;    /* pointer to free token memory */
extern int token_pool_size; /* size of remaining free token memory */
extern int cmd_code;        /* command code */
extern struct seg_spec_struct *seg_spec_pool;  /* pointer to free segment space */
extern int seg_spec_size;   /* size of segment pool */
extern int debug;       /* debug options */
extern char *null_string;   /* pointer to null ascii string */
extern int32_t misc_pool_used;
#endif

struct
{
    struct ss_struct *ps_ptr;    /* array of pointers to segments */
    uint32_t ps_name;   /* rad50 name of segment */
} psects[256];
static int free_psect;      /* index into psects array */
static int file_fd;     /* file descriptor */
SS_struct *base_page_nam=0;
GRP_struct *base_page_grp=0;
struct ss_struct *abs_group_nam=0;
struct grp_struct *abs_group=0;
static int inp_major,inp_minor;

#if defined(RT11_RSX)		/* obsolete stuff */
/*************************************************
 * gsdstruct is the structure of a GSD entry     *
 *************************************************/

struct gsdstruct
{
    uint16_t gsdnm1;   /* msb of name field */
    uint16_t gsdnm0;   /* lsb of name field */
    unsigned int gflg_weak:1;    /* weak/strong bit */
    unsigned int gflg_shr:1; /* refers to shared region (not used) */
    unsigned int gflg_ovr:1; /* section to be overlaid */
    unsigned int gflg_def:1; /* symbol defined */
    unsigned int gflg_base:1;    /* section is base page */
    unsigned int gflg_rel:1; /* relative symbol/section */
    unsigned int gflg_scp:1; /* global section (not used) */
    unsigned int gflg_dta:1; /* data section */
    uint8_t gsdtyp;    /* gsd type */
    int16_t gsdvalue;      /* gsd value */
};

struct gsdtime
{        /* gsd time/date structure */
    uint16_t gsdtim1;  /* msb of time longword */
    uint16_t gsdtim0;  /* lsb of time longword */
    unsigned fill1:16;       /* skip a short */
    unsigned int gsdyr:5;    /* bits 0:4 is the year since 1972 */
    unsigned int gsday:5;    /* bits 5:9 is the day */
    unsigned int gsdmn:4;    /* bits 10:13 is the month */
    unsigned fill2:2;        /* fill out to 16 bits */
};

struct gsdxlate
{       /* gsd xlator name */
    uint32_t gsdxn;     /* name */
    unsigned filler:16;      /* filler */
    uint8_t gsderrors; /* error counter */
    uint8_t gsdwarns;  /* warnings */
};

/*****************************************************************
 * An alternate GSD entry layout in order to get flags as a byte *
 *****************************************************************/

struct gsdflags
{
    uint32_t gsdlong;
    char flags;
    unsigned fill1:8;
    unsigned fill2:16;
};

struct r50name
{
    uint16_t r50msbs;
    uint16_t r50lsbs;
};

struct rldstruct
{
    unsigned int rldtyp:6;   /* rld type code */
    unsigned int rldmode:2;  /* 6800 mode (6) and byte mode (7) bits */
    uint8_t rlddsp;    /* displacement */
};

/*******************************************************************
 * The following is a union of pointers to various types of object *
 * file structures. This allows for obj to be auto-incremented     *
 * automagically after the correseponding item(s) have been        *
 * processed.							   *
 *******************************************************************/

static union
{
    uint16_t *rectyp;  /* object record type */
    struct gsdstruct *gsd;   /* followed by a GSD */
    struct rldstruct *rld;   /* and an RLD */
    struct r50name *rldnam;  /* pointer to rad50 name */
    int32_t *rldlong;       /* pointer to a long */
    int16_t *rldval;       /* rld value or constant */
    char *complex;       /* pointer to complex relocation string */
    uint16_t *txtlda;  /* text load address */
    char *txt;           /* text */
/*   struct isdstruct *isd;	    there may be an ISD too */
} obj;
#endif

static union
{
    struct vlda_abs *vldaabs;    /* abs and text record */
    struct vlda_id *vldaid;  /* id record */
    struct vlda_sym *vldasym;    /* symbol pointer */
    struct vlda_seg *vldaseg;    /* segment pointer */
    struct vlda_test *vldatst;   /* test expression */
    struct vlda_dbgdfile *vldadbgdfile;
    union vexp vldaexp;      /* expression pointer(s) */
    Sentinel *vldatype;      /* record sentinel */
    char *vldarp;
} vlda;

#define vabs vlda.vldaabs
#define vid  vlda.vldaid
#define vsym vlda.vldasym
#define vseg vlda.vldaseg
#define vexpr vlda.vldaexp
#define vtst vlda.vldatst
#define vtype vlda.vldatype
#define vrp vlda.vldarp
#define vdbgfile vlda.vldadbgdfile
int32_t object_count;

/*******************************************************************
 * get_obj reads a record from the input file. If the input file   *
 * is RT-11 format, it de-blocks a record from a serial byte       *
 * stream using single character file reads. The record is         *
 * in standard LDA file format: 1,0,cl,ch,data...,cs		   *
 * where each item is a byte, cl=low byte of 16 bit byte count,    *
 * ch=high byte of the count, data=data bytes and cs=checksum      *
 * which is the 2's compliment of all the previous bytes including *
 * the 1 and 0.							   *
 */

static int get_obj( void )

/*
 * At entry:
 *	current_fnd points to current input file
 *	file_fd = current file descriptor
 * At exit:
 *	inp_str is filled with record
 *	returns length in bytes of record or EOF
 */
{
    int i=0;
#ifdef VMS
    char c,*d=inp_str;
    int j,cnt,cs;
#endif
    object_count++;
#if !defined(VMS)
    i = read(file_fd,&rsize,sizeof(int16_t));
    even_odd = rsize&1;
#else
    i = read(file_fd,inp_str,MAX_TOKEN);
#endif
    if (i >= MAX_TOKEN)
    {
        sprintf(emsg,"Record size of %d > max of %d in \"%s\"",
                i,MAX_TOKEN,current_fnd->fn_buff);
        err_msg(MSG_ERROR,emsg);
        return(EOF);
    }
    if (i == 0)
        return(EOF);
#if defined(VMS)
    if (i > 0)
        return i;
#else
    if (read(file_fd,inp_str,(int)rsize+even_odd) == (int)rsize+even_odd)
    {
        return((int)rsize);
    }
#endif
    sprintf(emsg,"Error reading input \"%s\"",current_fnd->fn_buff);
    err_msg(MSG_ERROR,emsg);
    return(EOF);
}

#if defined(VMS) && defined(RT11_RSX)
static int dividend[2] = {0,0}; /* 64 bit dividend */
static int remainder,quotient;  /* 32 bit quotient and 32 bit remainder */
static char *r50_lc=0;      /* pointer to last non-blank character */
static char *ediv_char=0;
static char r50toa[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.*0123456789";
static int d5050=050*050,d50=050;

/****************************************************************************
 * r50div - convert a short containing rad50 code to an ASCII stream
 */
static void r50div( uint16_t r50 )
/*
 * At entry:
 *	r50 - is the short containing the rad50 code
 *	ediv_char - pointer to ascii buffer
 * At exit:
 *	3 character string located at *ediv_char
 *	ediv_char incremented by 3
 *	r50_lc = position+1 of last non-blank character
 */
{
    dividend[0] = r50;
    if (!lib_ediv(&d5050,&dividend[0],&quotient,&remainder)&1)
    {
        sprintf(emsg,"Error converting rad50 to ASCII in \"%s\"",current_fnd->fn_buff);
        err_msg(MSG_ERROR,emsg);
    }
    dividend[0] = remainder;
    *ediv_char++ = r50toa[quotient]; /* write the character */
    if (quotient) r50_lc = ediv_char;    /* record location + 1 of non-blank */
    if (!lib_ediv(&d50,&dividend[0],&quotient,&remainder)&1)
    {
        sprintf(emsg,"Error converting rad50 to ASCII in \"%s\"",current_fnd->fn_buff);
        err_msg(MSG_ERROR,emsg);
    }
    *ediv_char++ = r50toa[quotient]; /* write the character */
    if (quotient) r50_lc = ediv_char;    /* record location + 1 of non-blank */
    *ediv_char++ = r50toa[remainder];    /* write the character */
    if (remainder) r50_lc = ediv_char;   /* record location + 1 of non-blank */
}

/*********************************************************************
 * rad50_to_ascii - converts a longword rad50 value to an ASCII string
 * trimmed of trailing spaces and null terminated.
 */
static void rad50_to_ascii( struct r50name *ptr )
/*
 * At entry:
 *	ptr - points to gsdstruct containing longword rad50
 * At exit:
 *	ASCII string inserted in *token_pool
 *	token_value contains length of ASCII string
 */
{
    r50_lc = ediv_char = token_pool;
    r50div(ptr->r50msbs);    /* unpack msb rad50 word */
    r50div(ptr->r50lsbs);    /* unpack lsb rad50 word */
    *r50_lc = 0;         /* null terminate */
    token_value = r50_lc - token_pool; /* compute length of string */
    return;
}
#endif

#if defined(VMS) && defined(RT11_RSX)
static struct ss_struct *no_name_seg=0; /* pointer to un-named segment */
static struct ss_struct *last_txt_seg=0; /* segment ptr of last txt rcd */
static int rsx_pass=0;      /* Have to pass through RSX files twice */
static int rsx_fd;      /* file desc for rsx input */
static char *months[] =
{"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
#endif

static struct ss_struct *curr_seg=0;    /* pointer to curent segment */
static int curr_pc;         /* current pc */
static int last_txt_org;        /* org of last txt rcd */
static int last_txt_end;        /* pc after load of txt */

/****************************************************************************
 * new_sym - insert a new symbol into the symbol table and initialise the
 * symbol block.
 */
static struct ss_struct *new_sym( int flag )
/*
 * At entry:
 *	flag = 0 if not to insert symbol into symbol table
 *	 "   = 1 if to insert only if one not already there
 *       "   = 2 if to insert whether one is already there or not
 *	*token_pool points to ASCII string to insert
 *	token_value has length of string
 * At exit:
 *	returns pointer to symbol block
 *	new_symbol contains status about symbol insert
 *	token_pool may have been updated in order to keep symbol
 */
{
    struct ss_struct *sym_ptr;
    sym_ptr = sym_lookup(token_pool,++token_value,flag);
    switch (new_symbol)
    {
    case 5: {
            sym_ptr->ss_string = first_symbol->ss_string;
            sym_ptr->ss_fnd = current_fnd;
            break;
        }
    case 1:
    case 3: {
            sym_ptr->ss_fnd = current_fnd;
            if (token_value > 1)
            {
                token_pool += token_value;  /* update the free mem pointer */
                token_pool_size -= token_value; /* and size */
            }
        }
    case 0: break;
    default: {
            sprintf(emsg,
                    "Internal error. {new_symbol} invalid (%d)\n\t%s%s%s\"%s\"",
                    new_symbol,"while processing {",sym_ptr->ss_string,
                    "} in file ",current_fnd->fn_buff);
            err_msg(MSG_FATAL,emsg);
        }
    }
    return(sym_ptr);
}

#if defined(VMS) && defined(RT11_RSX)
/*********************************************************************
 * do_gsd - process all the GSD items found in the object record. Note
 * that the record will contain all the same type of items.
 */
static void do_gsd( int len, struct gsdstruct *gsdptr )
/*
 * At entry:
 *	len - number of bytes remaining in record
 *	gsdptr - pointer to first byte in record (as a gsdstruct)
 * At exit:
 *	all items in the record will have been processed.
 *	The definition file may have been updated if there were
 *	definitions in the GSD. Symbols and sections may have been
 *	inserted into the symbol table.
 */
{
    char *s;
    int noname,i;
    unsigned int mn,sec,mon;
    struct ss_struct *sym_ptr;
    struct seg_spec_struct *seg_ptr,*fseg_ptr;
    len = len/sizeof(struct gsdstruct);
    if (token_pool_size < len*7)
    {
        int tsiz = MAX_TOKEN*8;
        if (len*7 > tsiz) tsiz += len*7;
        token_pool_size = tsiz;
        token_pool = MEM_alloc(tsiz);
        misc_pool_used += token_pool_size;
    }
    for (; len > 0; --len,gsdptr++)
    {
        switch (gsdptr->gsdtyp)
        {
        case 0: {      /* module name */
                rad50_to_ascii((struct r50name *)gsdptr); /* put ascii in token_pool */
                current_fnd->fn_target = token_pool;
                if (target == 0) target = token_pool;
                token_pool += ++token_value;
                token_pool_size -= token_value;
                continue;
            }
        case 1: {       /* CSECT name */
                s = &((struct gsdflags *)gsdptr)->flags;
                if (gsdptr->gsdnm0)
                {
                    if (((struct gsdflags *)gsdptr)->gsdlong == 0x0F94AF01)
                    {
                        *s = 0x04;    /* . ABS. section */
                    }
                    else
                    {
                        *s = 0x64;    /* normal named .CSECT */
                    }
                }
                else
                {
                    *s = 0x20;   /* unnamed .CSECT */
                }
            }          /* fall through to case 5 */
        case 5: {      /* PSECT name */
                if ((noname = ((struct gsdflags *)gsdptr)->gsdlong) == 0)
                {
                    strcpy(token_pool,"unnamed_");
                    token_value = 9;
                }
                else
                {
                    rad50_to_ascii((struct r50name *)gsdptr); /* put ascii in token_pool */
                    *r50_lc++ = '_'; /* psect names are followed with an _ */
                    *r50_lc++ = ' '; /* and a trailing space */
                    *r50_lc = 0; /* so move the null */
                    token_value += 2; /* and add 2 to length */
                    if (gsdptr->gsdnm1 - gsdptr->gsdnm1 % 050 == 0xAF00)
                    {
                        *(token_pool+1) = '_';    /* ". x" changes to "._x" */
                        gsdptr->gflg_base = gsdptr->gflg_rel;
                    }
                }
                if (rsx_fd || output_mode == OUTPUT_OL) gsdptr->gflg_base = 0;
                curr_seg = sym_ptr = new_sym(2);
                curr_pc = 0;
                if (noname == 0) no_name_seg = sym_ptr;
                last_txt_org = last_txt_end = 0;    /* reset the offset */
                if (!chk_mdf(0,sym_ptr,0))
				{
					 continue;
				}
                if ((seg_ptr=sym_ptr->seg_spec) == 0)
                {
                    if ((seg_ptr=get_seg_spec_mem(sym_ptr)) == 0) continue;
                }
                sym_ptr->flg_defined = 1;       /* .seg defines the segment */
                sym_ptr->flg_segment = 1;       /* signal its a segment */
                sym_ptr->flg_ovr = gsdptr->gflg_ovr;
                sym_ptr->flg_abs = ~gsdptr->gflg_rel;
                seg_ptr->seg_salign = !gsdptr->gflg_dta; /* set the alignment factor */
                seg_ptr->seg_dalign = 0;        /* set data alignment factor */
                seg_ptr->seg_len = (uint16_t)gsdptr->gsdvalue;
                if (!(new_symbol & 4) &&        /* if not a duplicate symbol */
                    !sym_ptr->flg_member)
                {   /* and not already a group member */
                    if (!gsdptr->gflg_rel)
                    {
                        if (abs_group == 0)
                        {
                            abs_group_nam = get_symbol_block(1);
                            abs_group_nam->ss_string = "Absolute_sections";
                            abs_group = get_grp_ptr(abs_group_nam,(int32_t)0,(int32_t)0);
                            abs_group_nam->ss_fnd = current_fnd;
                            abs_group_nam->flg_based = 1;
                            abs_group_nam->seg_spec->sflg_absolute = 1;
                        }
                        insert_intogroup(abs_group,sym_ptr,abs_group_nam);
                    }
                    if (gsdptr->gflg_base)
                    {
                        if (base_page_grp == 0)
                        {
                            base_page_nam = get_symbol_block(1);
                            base_page_nam->ss_string = "Zero_page_sections";
                            base_page_nam->ss_fnd = current_fnd;
                            base_page_grp = get_grp_ptr(base_page_nam,(int32_t)0,(int32_t)0);
                            base_page_nam->flg_based = 1;
                            base_page_nam->seg_spec->seg_maxlen = 256;
                            base_page_nam->seg_spec->sflg_zeropage = 1;
                        }
                        insert_intogroup(base_page_grp,sym_ptr,base_page_nam);
                    }
                    if (gsdptr->gflg_rel && !gsdptr->gflg_base)
                    { /* else default */
                        insert_intogroup(group_list_top,sym_ptr,group_list_default);
                    }
                }
                if (new_symbol & 4)
                {       /* if duplicate symbol */
                    fseg_ptr = first_symbol->seg_spec;
#if 0
                    if (!current_fnd->fn_rt11 &&
                        (!gsdptr->gflg_dta != fseg_ptr->seg_salign))
                    {
                        sprintf(emsg,
                                "Segment {%s} declared in %s with alignment of %d\n\t%s%s%s%d",
                                sym_ptr->ss_string,current_fnd->fn_buff,!gsdptr->gflg_dta,
                                "and is declared in ",first_symbol->ss_fnd->fn_buff,
                                " with alignment of ",fseg_ptr->seg_salign);
                        err_msg(MSG_WARN,emsg);
                    }
                    if (seg_ptr->seg_dalign != 0)
                    {
                        sprintf(emsg,
                                "Segment {%s} declared in %s with combin of %d\n\t%s%s%s%d",
                                sym_ptr->ss_string,current_fnd->fn_buff,0,"and is declared in ",
                                first_symbol->ss_fnd->fn_buff," with combin of ",
                                fseg_ptr->seg_dalign);
                        err_msg(MSG_WARN,emsg);
                    }
#endif
                    seg_ptr->seg_group = fseg_ptr->seg_group; /* same group */
                }
                psects[free_psect].ps_name = ((struct gsdflags *)gsdptr)->gsdlong;
                psects[free_psect++].ps_ptr = sym_ptr;
                continue;
            }
        case 2: {      /* ISD is actually... */
                if (current_fnd->fn_credate == null_string)
                { /* time/date if not already in */
                    i = ((((struct gsdtime *)gsdptr)->gsdtim1 << 16) + 
                         ((struct gsdtime *)gsdptr)->gsdtim0)/60;
                    sec = i % 60;    /* get the seconds */
                    i /= 60;     /* toss the seconds */
                    mn = i % 60; /* get the minutes */
                    mon = ((struct gsdtime *)gsdptr)->gsdmn-1;
                    sprintf(token_pool,"%02d-%s-%04d %02d:%02d:%02d",
                            ((struct gsdtime *)gsdptr)->gsday,
                            months[mon],
                            ((struct gsdtime *)gsdptr)->gsdyr+1972,
                            i/60,mn,sec);
                    current_fnd->fn_credate = token_pool;
                    i = strlen(token_pool) + 1;
                    token_pool += i;
                    token_pool_size -= i;
                    continue;
                }
                if (current_fnd->fn_xlator == null_string)
                { /* or xlator if not already in */
                    rad50_to_ascii((struct r50name *)gsdptr);
                    if ((i=((struct gsdxlate *)gsdptr)->gsderrors) != 0)
                    {
                        sprintf(token_pool+token_value," err=%d",i);
                    }
                    if ((i=((struct gsdxlate *)gsdptr)->gsdwarns) != 0)
                    {
                        sprintf(token_pool+token_value," wrn=%d",i);
                    }
                    i = strlen(token_pool) + 1;
                    current_fnd->fn_xlator = token_pool;
                    token_pool += i;
                    token_pool_size -= i;
                    continue;
                }
                continue;
            }
        case 3: continue;  /* transfer address,ignore for now */
        case 4: {      /* Global symbol name */
                rad50_to_ascii((struct r50name *)gsdptr); /* put ascii in token_pool */
                sym_ptr = new_sym(1);
                insert_id((int32_t)current_fnd->fn_max_id++,sym_ptr);
                if (qual_tbl[QUAL_CROSS].preesnt)
					do_xref_symbol(sym_ptr,gsdptr->gflg_def);
                if (gsdptr->gflg_def)
                {     /* if symbol being defined */
                    if (current_fnd->fn_stb)
                    {   /* symbol file? */
                        if (sym_ptr->flg_defined &&   /* old symbol must be defined */
                            sym_ptr->flg_abs && /* and absolute */
                            !gsdptr->gflg_rel &&    /* and new symbol must be abs */
                            sym_ptr->ss_value == gsdptr->gsdvalue)
                        { /* and equal */
                            continue;      /* ignore multiple defs if values == */
                        }
                    }
                    if (!chk_mdf(1,sym_ptr,0))
					{
						continue; /* nfg */
					}
                    sym_ptr->flg_symbol = 1;     /* its a symbol */
                    expr_stack[0].expr_code = EXPR_SYM;  /* build an expression */
                    expr_stack[0].expr_value = gsdptr->gsdvalue;
                    if ((expr_stack[0].expr_ptr = curr_seg) == 0)
                    { /* curr_seg offset + */
                        sprintf(emsg,"Bad object format. Unable to define {%s} in \"%s\"",
                                sym_ptr->ss_string,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        continue;         /* don't define the symbol */
                    }
                    expr_stack_ptr = 1;      /* stack is 1 item deep */
                    sym_ptr->flg_exprs = 1;   /* defined with an expression */
                    write_to_symdef(sym_ptr); /* write symbol stuff */
                    sym_ptr->flg_defined = 1;    /* set defined bit */
                    sym_ptr->flg_nosym = current_fnd->fn_nosym;
                }
                continue;
            }
        case 6: continue;  /* GSD ident */
        case 7: continue;  /* Mapped array declaration */
        case 8: continue;  /* completion routine declaration */
        default: {
                sprintf(emsg,"Illegal GSD type: %d in file \"%s\"",
                        gsdptr->gsdtyp,current_fnd->fn_buff);
                err_msg(MSG_ERROR,emsg);
            }          /* default */
        }             /* switch  */
    }                /* for 	   */
}               /* do_gsd  */

struct ss_struct *get_psect( char *strng )
{
    uint32_t psname;
    struct ss_struct *sym_ptr;
    int i;
    if ((psname = *obj.rldlong++) == 0)
    { /* pick up the rad50 name */
        sym_ptr = no_name_seg;
    }
    else
    {
        sym_ptr = 0;  /* assume failure */
        for (i=0;i<free_psect;i++)
        {
            if (psname == psects[i].ps_name)
            {
                sym_ptr = psects[i].ps_ptr;
                break;
            }
        }
        if (sym_ptr == 0)
        {
            rad50_to_ascii(obj.rldnam-1);
            sprintf(emsg,"%s to undefined segment {%s} in file \"%s\"",
                    strng,token_pool,current_fnd->fn_buff);
            err_msg(MSG_ERROR,emsg);
        }
    }
    return(sym_ptr);
}

static void back_patch( int disp )
{
    expr_stack[0].expr_code = EXPR_SYM;  /* insert expression "curr_seg const +" */
    expr_stack[0].expr_ptr = curr_seg;
    expr_stack[0].expr_value = curr_pc = disp + last_txt_org;
    write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
    return;
}

static char mode_chars[] = "wWbb";  /* storage modes */

static void do_rld( int len )
{
    int mode = 0,disp,type,cnst;
    struct ss_struct *sym_ptr;
    struct expr_token *exp;
    for (;len > 0;len -= 2)
    {
        exp = expr_stack;
        type = obj.rld->rldtyp;
        mode = obj.rld->rldmode;      /* 0:tag w; 1=tag W; 2=tag b; 3=tag b */
        disp = (obj.rld++)->rlddsp - 4;
        if (type < 7 || type > 9)
        {
            back_patch(disp);
            curr_pc += (mode > 1)? 1 : 2;
        }
        switch (type)
        {
        case 1: {      /* internal relocation (.word addr) */
                if ((mode & 1) != 0)
                {
                    char t;
                    t = *obj.complex; /* bug in MACxx, swap the bytes in value */
                    *obj.complex = *(obj.complex+1);
                    *(obj.complex+1) = t;
                }
                if (mode & 2) *obj.rldval = *obj.complex;   /* sign extend */
                expr_stack[0].expr_code = EXPR_SYM;
                expr_stack[0].expr_ptr = curr_seg; /* compute "curr_seg constant +" */
                expr_stack[0].expr_value = *obj.rldval++;
                write_to_tmp(TMP_EXPR,1l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 2;       /* adjust for constant */
                break;      /* done */
            }
        case 10: {     /* psect relocation (same as global) */
                if ((sym_ptr = get_psect("Psect relocation")) == 0)
                {
                    len -= 4;
                    break;
                }
            }
        case 2: {      /* Global relocation ".word global" */
                if (type == 2)
                {
                    rad50_to_ascii(obj.rldnam++);
                    if ((sym_ptr = sym_lookup(token_pool,++token_value,0)) == 0)
                    {
                        sprintf(emsg,"Undefined symbol {%s} found in file \"%s\"",
                                token_pool,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        len -= 4;     /* adjust for symbol */
                        break;        /* done */
                    }
                }
                expr_stack[0].expr_code = EXPR_SYM;
                expr_stack[0].expr_ptr = sym_ptr;   /* compute "symbol 0 +" */
                expr_stack[0].expr_value = 0;
                write_to_tmp(TMP_EXPR,1l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 4;       /* adjust for symbol */
                break;      /* done */
            }
        case 3: {      /* internal displaced "clr abs_address" */
                if ((mode & 1) != 0)
                {
                    char t;
                    t = *obj.complex; /* bug in MACxx, swap the bytes in value */
                    *obj.complex = *(obj.complex+1);
                    *(obj.complex+1) = t;
                }
                if (mode & 2) *obj.rldval = *obj.complex;   /* sign extend */
                expr_stack[0].expr_code = EXPR_SYM;
                expr_stack[0].expr_ptr = curr_seg; /* compute "constant curr_seg 2 + -" */
                expr_stack[0].expr_value = -(*obj.rldval++ - 2); /* absolute address */ 
                expr_stack[1].expr_code = EXPR_OPER;
                expr_stack[1].expr_value = EXPROPER_NEG;
                write_to_tmp(TMP_EXPR,2l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 2;       /* adjust for constant */
                break;      /* done */
            }
        case 12: {     /* psect displaced (same as global) */
                if ((sym_ptr = get_psect("Psect displaced relocation")) == 0)
                {
                    len -= 4;
                    break;
                }
            }
        case 4: {      /* global displaced "clr global" */
                if (type == 4)
                {
                    rad50_to_ascii(obj.rldnam++);
                    if ((sym_ptr = sym_lookup(token_pool,++token_value,0)) == 0)
                    {
                        sprintf(emsg,"Undefined symbol {%s} found in file \"%s\"",
                                token_pool,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        len -= 4;     /* adjust for symbol */
                        break;        /* done */
                    }
                }
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = sym_ptr;   /* compute "symbol curr_seg n + -" */
                exp->expr_value = 0;
                (++exp)->expr_code = EXPR_SYM;
                exp->expr_ptr = curr_seg;
                exp->expr_value = 1 + (mode < 2);   
                (++exp)->expr_code = EXPR_OPER;
                exp->expr_value = '-';
                write_to_tmp(TMP_EXPR,3l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 4;       /* adjust for symbol */
                break;      /* done */
            }
        case 13: {     /* psect additive (same as global) */
                if ((sym_ptr = get_psect("Psect additive relocation")) == 0)
                {
                    len -= 6;
                    obj.rldval++;    /* skip the value */
                    break;
                }
            }
        case 5: {      /* global additive ".word global+n" */
                if (type == 5)
                {
                    rad50_to_ascii(obj.rldnam++);
                    if ((sym_ptr = sym_lookup(token_pool,++token_value,0)) == 0)
                    {
                        sprintf(emsg,"Undefined symbol {%s} found in file \"%s\"",
                                token_pool,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        obj.rldval++;        /* skip the value */
                        len -= 6;        /* adjust for symbol */
                        break;           /* done */
                    }
                }
                if ((mode & 1) != 0)
                {
                    char t;
                    t = *obj.complex; /* bug in MACxx, swap the bytes in value */
                    *obj.complex = *(obj.complex+1);
                    *(obj.complex+1) = t;
                }
                if (mode & 2) *obj.rldval = *obj.complex;   /* sign extend */
                cnst = *obj.rldval++;   /* pick up the constant */
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = sym_ptr;   /* compute "symbol const +" */
                exp->expr_value = cnst; 
                write_to_tmp(TMP_EXPR,1l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 6;       /* adjust for symbol */
                break;      /* done */
            }
        case 14: {     /* psect additive displaced (same as global) */
                if ((sym_ptr = get_psect("Psect additive displaced relocation")) == 0)
                {
                    len -= 6;
                    obj.rldval++;    /* skip the value */
                    break;
                }
            }
        case 6: {      /* global additive displaced "clr global+n" */
                if (type == 6)
                {
                    rad50_to_ascii(obj.rldnam++);
                    if ((sym_ptr = sym_lookup(token_pool,++token_value,0)) == 0)
                    {
                        sprintf(emsg,"Undefined symbol {%s} found in file \"%s\"",
                                token_pool,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        obj.rldval++;    /* skip the value */
                        len -= 6;        /* adjust for symbol and constant */
                        break;           /* done */
                    }
                }
                if ((mode & 1) != 0)
                {
                    char t;
                    t = *obj.complex; /* bug in MACxx, swap the bytes in value */
                    *obj.complex = *(obj.complex+1);
                    *(obj.complex+1) = t;
                }
                if (mode & 2) *obj.rldval = *obj.complex; /* sign extend */
                cnst = *obj.rldval++;   /* pick up the constant */
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = sym_ptr;   /* compute "symbol curr_seg 2 + -" */
                exp->expr_value = cnst - ((mode > 2) ? 2 : 1);  
                (++exp)->expr_code = EXPR_SYM;
                exp->expr_ptr = curr_seg;
                exp->expr_value = 0;
                (++exp)->expr_code = EXPR_OPER;
                exp->expr_value = '-';
                write_to_tmp(TMP_EXPR,3l,(char *)expr_stack,sizeof(struct expr_token));
                write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                len -= 6;       /* adjust for symbol and constant */
                break;      /* done */
            }
        case 7: {          /* location counter definition */
                if ((sym_ptr = get_psect("Attempt to set PC")) == 0)
                {
                    len -= 6;
                    obj.rldval++;    /* skip the value */
                    break;
                }
                cnst = *obj.rldval++;   /* pick up the constant */
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = curr_seg = sym_ptr; /* compute "seg const +" */
                exp->expr_value = curr_pc = cnst & 65535;   
                write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
                len -= 6;       /* adjust for symbol */
                break;      /* done */
            }
        case 8: {      /* pc modification ".blkb n" */
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = curr_seg; /* compute "curr_seg constant +" */
                exp->expr_value = curr_pc = *obj.rldval++ & 65535;
                write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
                len -= 2;       /* adjust for constant */
                break;
            }
        case 9: break;     /* program limits ".LIMIT" */
        case 11: {     /* .vctrs */
                if ((sym_ptr = get_psect(".VCTRS set PC")) == 0)
                {
                    len -= 6;
                    obj.rldval++;    /* skip the value */
                    break;
                }
                if ((mode & 1) != 0)
                {
                    char t;
                    t = *obj.complex; /* bug in MACxx, swap the bytes in value */
                    *obj.complex = *(obj.complex+1);
                    *(obj.complex+1) = t;
                }
                cnst = *obj.rldval++;   /* pick up the constant */
                exp->expr_code = EXPR_SYM;
                exp->expr_ptr = curr_seg = sym_ptr; /* compute "seg const +" */
                exp->expr_value = curr_pc = cnst & 65535;   
                write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
                len -= 6;       /* adjust for symbol */
                break;      /* done */
            }
        case 15: {     /* complex */
                for (type=0;--len >= 2;)
                {
                    int tc;
                    tc = *obj.complex;
                    switch (tc)
                    {
                    case 0: continue; /* nop */
                    case 1:       /* + (addition) */
                        tc = EXPROPER_ADD;
                        goto common_complex;
                    case 2:       /* - (subtraction) */
                        tc = EXPROPER_SUB;
                        goto common_complex;
                    case 3:       /* * (multiplication) */
                        tc = EXPROPER_MUL;
                        goto common_complex;
                    case 4:       /* / (division) */
                        tc = EXPROPER_DIV;
                        goto common_complex;
                    case 5:       /* & (logical and) */
                        tc = EXPROPER_AND;
                        goto common_complex;
                    case 6:       /* | (logical or) */
                        tc = EXPROPER_OR;
                        goto common_complex;
                    case 7:       /* ^ (exclusive or) */
                        tc = EXPROPER_XOR;
                        goto common_complex;
                    case 8:       /* _ (negate) */
                        tc = EXPROPER_NEG;
                        goto common_complex;
                    case 9:       /* ~ (compliment) */
                        tc = EXPROPER_COM;
                        goto common_complex;
                    case 12:      /* ? (unsigned divide */
                        tc = EXPROPER_USD;
                        goto common_complex;
                    case 13:      /* = (swap bytes) */
                        tc = EXPROPER_SWAP;
                        goto common_complex;
                    case 19:      /* \ (modulo) */
                        tc = EXPROPER_MOD;
                        goto common_complex;
                    case 21:      /* < (shift left) */
                        tc = EXPROPER_SHL;
                        goto common_complex;
                    case 22: {        /* > (shift right) */
                            tc = EXPROPER_SHR;
                            goto common_complex;
                            common_complex:
                            type++;        /* record 1 item on the stack */
                            exp->expr_value = tc;
                            (exp++)->expr_code = EXPR_OPER;
                            continue;
                        }
                    case 10: {        /* store immediate */
                            if (!type) break;  /* nothing to write */
                            write_to_tmp(TMP_EXPR,(int32_t)type,(char *)expr_stack,sizeof(struct expr_token));
                            write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                            exp = expr_stack;      /* reset the stack pointer */
                            break;     /* break out of switch */
                        }
                    case 11: {        /* store displaced */
                            if (!type) break;  /* nothing to write */
                            exp->expr_code = EXPR_SYM;
                            exp->expr_ptr = curr_seg; /* compute "value curr_seg 2 + -" */
                            (exp++)->expr_value = 1 + (mode < 2);  
                            exp->expr_code = EXPR_OPER;
                            exp->expr_value = '-';
                            write_to_tmp(TMP_EXPR,(int32_t)(type+2),(char *)expr_stack,sizeof(struct expr_token));
                            write_to_tmp(TMP_TAG,1l,&mode_chars[mode],1);
                            exp = expr_stack;      /* reset the stack pointer */
                            break;     /* break out of switch */
                        }
                    case 14: {        /* get a global symbol */
                            rad50_to_ascii(obj.rldnam++);
                            if ((sym_ptr = sym_lookup(token_pool,++token_value,0)) == 0)
                            {
                                sprintf(emsg,"Undefined symbol {%s} found in file \"%s\"",
                                        token_pool,current_fnd->fn_buff);
                                err_msg(MSG_ERROR,emsg);
                            }
                            else
                            {
                                exp->expr_code = EXPR_SYM;
                                exp->expr_value = 0;
                                (exp++)->expr_ptr = sym_ptr;
                            }
                            type++;        /* add 1 item to stack */
                            len -= 4;      /* adjust for symbol */
                            continue;
                        }
                    case 15: {        /* relocatable value */
                            if ((cnst = *obj.complex++ & 255) >= free_psect)
                            {
                                sprintf(emsg,"Illegal psect number in complex string in file \"%s\"",
                                        current_fnd->fn_buff);
                                err_msg(MSG_ERROR,emsg);
                                obj.complex += 2;   /* eat the constant */
                            }
                            else
                            {
                                exp->expr_code = EXPR_SYM;  
                                exp->expr_ptr = psects[cnst].ps_ptr;
                                (exp++)->expr_value = *obj.rldval++;
                                ++type;     /* 1 more item on stack */
                            }
                            len -= 3;      /* 3 extra bytes removed from complex string */
                            continue;      /* proceed */
                        }
                    case 16: {        /* constant */
                            exp->expr_code = EXPR_VALUE;   
                            (exp++)->expr_value = *obj.rldval++;
                            len -= 2;      /* take two more bytes from total */
                            type++;        /* 1 more item on the stack */
                            continue;
                        }
                    case 17: {        /* use high byte only */
                            exp->expr_code = EXPR_VALUE;   
                            (exp++)->expr_value = 8;
                            exp->expr_code = EXPR_OPER;    
                            (exp++)->expr_value = EXPROPER_SHR; /* shift right 8 bits */
                            exp->expr_code = EXPR_VALUE;   
                            (exp++)->expr_value = 255;
                            exp->expr_code = EXPR_OPER;    
                            (exp++)->expr_value = EXPROPER_AND; /* mask off all but low 8 bits */
                            type += 4;             /* 4 items on the stack */
                            continue;
                        }
                    case 18: {        /* use low byte only */
                            exp->expr_code = EXPR_VALUE;   
                            (exp++)->expr_value = 255;
                            exp->expr_code = EXPR_OPER;    
                            (exp++)->expr_value = EXPROPER_AND; /* mask off all but low 8 bits */
                            type += 2;             /* 2 items on the stack */
                            continue;
                        }
                    default: {
                            sprintf (emsg,"Bad complex code byte in file \"%s\"",
                                     current_fnd->fn_buff);
                            err_msg(MSG_ERROR,emsg);
                        }
                    }        /* complex switch */
                    break;       /* fall out of do if fallen out of switch */
                }           /* complex do loop */
                if (exp != expr_stack)
                {
                    sprintf(emsg,"Imbalanced stack after complex string in file: %s",
                            current_fnd->fn_buff);
                }
                continue;       /* get next RLD item */
            }          /* case complex: */
        case 16: {     /* .dpage (MAC69 only) */
                obj.rldval += 2;    /* skip the constant for now */
                len -= 2;       /* and adjust the length too */
                continue;       /* and proceed */
            }
        default: {     /* who knows */
                sprintf (emsg,"Illegal RLD item code (%d) in file: %s",
                         type,current_fnd->fn_buff);
                return;     /* no sense trying for more in this rcd */
            }          /* 		default */
        }             /* 		RLD switch */
    }                /*		do loop */
    return;
}               /*		do_rld routine */
#endif

static char tag;
static int32_t taglen;

/************************************************************************
 * inp_vldaexp - input an expression from a vlda record
 */
static void inp_vldaexp( char *ptr )
/*
 * Input an expression from the vlda file to the expression stack
 * At entry:
 *	ptr - address of first byte of expression string
 * At exit:
 *	expr_stack loaded with expression
 */
{
    union vexp ve;
    struct expr_token *expr;
    int cnt;
    unsigned int ve_code;

    ve.vexp_chp = ptr;
    tag = taglen = 0;
    cnt = expr_stack_ptr = *ve.vexp_len++; /* get the number of items */
    expr = expr_stack;
    for (; cnt>0; ++expr,--cnt)
    {
        switch (ve_code = *ve.vexp_type++)
        {
        case VLDA_EXPR_TAG: {
                tag = *ve.vexp_type++;  /* remember the tag code */
                taglen = *ve.vexp_const++;  /* remember the tag length */
                --expr_stack_ptr;       /* wack off the last one from tos */
                return;         /* done */
            }
        case VLDA_EXPR_CTAG: {
                tag = *ve.vexp_type++;  /* remember the tag code */
                taglen = *ve.vexp_byte++;   /* remember the tag length */
                --expr_stack_ptr;       /* wack off the last one from tos */
                return;         /* done */
            }
        case VLDA_EXPR_WTAG: {
                tag = *ve.vexp_type++;  /* remember the tag code */
                taglen = *ve.vexp_word++;   /* remember the tag length */
                --expr_stack_ptr;       /* wack off the last one from tos */
                return;         /* done */
            }
        case VLDA_EXPR_1TAG: {
                tag = *ve.vexp_type++;  /* remember the tag code */
                taglen = 1;         /* remember the tag length */
                --expr_stack_ptr;       /* wack off the last one from tos */
                return;         /* done */
            }
        case VLDA_EXPR_L:
        case VLDA_EXPR_B:
            expr->expr_code = (ve_code == VLDA_EXPR_L) ? EXPR_L : EXPR_B;
            expr->ss_ptr = id_table[id_table_base+ *ve.vexp_ident++];
            goto vldainp_comm1;
        case VLDA_EXPR_CSYM:   /* symbol or segment */
        case VLDA_EXPR_SYM:    /* symbol or segment */
            expr->expr_code = EXPR_IDENT;
            expr->ss_id = (ve_code == VLDA_EXPR_CSYM) ? 
                          (id_table_base+ (*ve.vexp_byte++ & 0xFF)):
                          (id_table_base+ *ve.vexp_ident++);
vldainp_comm1:
            expr->expr_value = 0;
            if ( !expr->ss_ptr && !expr->ss_id )
            {
                sprintf(emsg,"Ident %d not defined but used in expression in \"%s\"",
                        *(ve.vexp_ident-1),current_fnd->fn_buff);
                err_msg(MSG_ERROR,emsg);
                expr->expr_code = EXPR_VALUE; /* so it won't accvio */
            }
            continue;
        case VLDA_EXPR_VALUE: {
                expr->expr_code = EXPR_VALUE;
                expr->expr_value = *ve.vexp_const++;
                continue;
            }
        case VLDA_EXPR_CVALUE: {
                expr->expr_code = EXPR_VALUE;
                expr->expr_value = *ve.vexp_byte++;
                continue;
            }
        case VLDA_EXPR_WVALUE: {
                expr->expr_code = EXPR_VALUE;
                expr->expr_value = *ve.vexp_word++;
                continue;
            }
        case VLDA_EXPR_0VALUE: {
                expr->expr_code = EXPR_VALUE;
                expr->expr_value = 0;
                continue;
            }
        case VLDA_EXPR_OPER: {
                int tok;
                expr->expr_code = EXPR_OPER;
                tok = expr->expr_value = *ve.vexp_oper++;
                if (tok == EXPROPER_TST)
                {
                    expr->expr_value |= *ve.vexp_oper++ << 8;
                }
                else if (tok == EXPROPER_ADD || tok == EXPROPER_SUB)
                {
                    int t=0;
                    struct expr_token *tos,*sos;
                    tos = expr-1;
                    sos = expr-2;
                    if (tos->expr_code == EXPR_VALUE) t = 1;
                    else if (tos->expr_code == EXPR_SYM) t = 2;
                    if (sos->expr_code == EXPR_VALUE) t |= 4*1;
                    else if (sos->expr_code == EXPR_SYM) t |= 4*2;
                    if (t == 10)
                    {       /* sos is SYM, tos is SYM */
                        if (tok == EXPROPER_ADD)
                        {
                            sos->expr_value += tos->expr_value;
                        }
                        else
                        {
                            sos->expr_value -= tos->expr_value;
                        }
                        tos->expr_value = 0;
                    }
                    else if (t == 6)
                    { /* sos is VALUE, tos is SYM */
                        sos->ss_ptr = tos->ss_ptr;
						sos->ss_id = tos->ss_id;
                        sos->expr_code = EXPR_SYM;
                        if (tok == EXPROPER_ADD)
                        {
                            sos->expr_value += tos->expr_value;
                            expr_stack_ptr -= 2;
                            expr = sos;
                        }
                        else
                        {
                            sos->expr_value = tos->expr_value - sos->expr_value;
                            tos->expr_code = EXPR_OPER;
                            tos->expr_value = EXPROPER_NEG;
                            expr_stack_ptr -= 1;
                            expr = tos;
                        }
                        continue;
                    }
                    else if (t == 9 ||     /* sos is SYM, tos is VALUE */
                             t == 5)
                    {     /* sos is VALUE, tos is VALUE */
                        if (tok == EXPROPER_ADD)
                        {
                            sos->expr_value += tos->expr_value;
                        }
                        else
                        {
                            sos->expr_value -= tos->expr_value;
                        }
                        expr = sos;
                        expr_stack_ptr -= 2;
                        continue;
                    }
                }
                continue;
            }
        default: {
                sprintf(emsg,"Undefined VLDA expression code of 0x%02X (%d) in \"%s\"",
                        ve_code,ve_code,current_fnd->fn_buff);
                err_msg(MSG_ERROR,emsg);
                continue;
            }          /* --default	*/
        }             /* --switch	*/
    }                /* --for	*/
}               /* --inp_vldaexp */

static int vlda_inp;

void object( int fd )
{
    int length;
    char *s,*d;
    struct ss_struct *sym_ptr;
    struct seg_spec_struct *seg_ptr,*fseg_ptr;
    vlda_inp = 0;        /* assume not vlda input */
    current_fnd->fn_max_id = 1;  /* offset into ID table for globals */
    free_psect = 0;      /* start at top of segment chain */
    file_fd = fd;        /* keep file descriptor in static mem */
#if defined(VMS) && defined(RT11_RSX)
    rsx_fd = !current_fnd->fn_rt11;
#endif
    while (1)
    {
        while ((length=get_obj()) != EOF)
        {
            int rectyp;
#if defined(VMS) && defined(RT11_RSX)
            obj.rectyp = (uint16_t *)inp_str; /* point to the string */
#endif
            vrp = inp_str;
            if (token_pool_size <= MAX_TOKEN )
            {
                token_pool_size = MAX_TOKEN*8;
                token_pool = MEM_alloc(token_pool_size);
                misc_pool_used += token_pool_size;
            }
#if defined(VMS) && defined(RT11_RSX)
            rectyp = *obj.rectyp++ & 255;
#else
            rectyp = *vtype;
#endif
            switch (rectyp)
            {
            default: {
                    sprintf(emsg,"Undefined (and ignored) record type: 0x%02X (%d. len=%d) in file \"%s\"",
                            rectyp,rectyp,length,current_fnd->fn_buff);
                    err_msg(MSG_WARN,emsg);
                    continue;
                }
            case 0: continue;       /* Absolute text load (ignored) */
#if defined(VMS) && defined(RT11_RSX)
            case 1: {           /* GSD */
                    if (rsx_pass != 0) continue;
                    do_gsd(length-2,obj.gsd);    /* go do the GSD stuff */
                    continue;
                }
            case 2: continue;       /* end of GSD (is a don't care) */
            case 3: {           /* TXT */
                    /* if we back patched, then move PC again */
                    if (rsx_fd && rsx_pass == 0) continue;
                    if (curr_seg != last_txt_seg || curr_pc != last_txt_end)
                    {
                        expr_stack[0].expr_code = EXPR_SYM;   /* insert expression "curr_seg const +" */
                        expr_stack[0].expr_ptr = curr_seg;
                        expr_stack[0].expr_value = *obj.rldval & 65535; /* put PC at start of TXT rcd */
                        write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
                    }
                    last_txt_org = *obj.rldval++ & 65535; /* remember load address of txt */
                    last_txt_end = curr_pc = last_txt_org + length-4;
                    last_txt_seg = curr_seg; /* remember the segment too */
                    write_to_tmp(TMP_BSTNG,(int32_t)(length-4),(char *)obj.txt,1);
                    continue;
                }
            case 4: {           /* RLD */
                    if (rsx_fd && rsx_pass == 0) continue;
                    do_rld(length-2);    /* go do the RLD stuff */
                    continue;
                }
            case 5: continue;       /* ISD */
            case 6: {           /* end of module */
                    if (current_fnd->fn_rt11 != 0)
                    {
                        if (current_fnd->fn_xlator != 0)
                        {
                            if (strncmp(current_fnd->fn_xlator,"MAC11",5) != 0)
                            {
                                for (i=0;i<free_psect;i++)
                                {
                                    psects[(int16_t)i].ps_ptr->seg_spec->seg_salign = 0;
                                }
                            }
                        }
                    }
                    break;
                }
#endif
            case VLDA_TXT: {    /* text */
                    vlda_inp = 1;    /* vlda input */
                    write_to_tmp(TMP_BSTNG,(int32_t)(length-sizeof(struct vlda_abs)),
                                 inp_str+sizeof(struct vlda_abs),1);
                    continue;
                }
            case VLDA_GSD: {    /* global symbol */
                    vlda_inp = 1;    /* vlda input */
                    if (current_fnd->fn_max_id < vsym->vsym_ident)
                        current_fnd->fn_max_id = vsym->vsym_ident;
                    s = inp_str+vsym->vsym_noff;
                    d = token_pool;
                    while ((*d++ = *s++) !=0);
                    if ((vsym->vsym_flags&VSYM_SYM) == 0)
                    {  /* if segment */
                        --d;          /* append a space to tell it */
                        *d++ = ' ';       /* from a like named symbol */
                        *d++ = 0;
                    }
                    token_value = d - token_pool - 1;
                    if ((vsym->vsym_flags&VSYM_SYM) == 0)
                    {  /* segment */
                        curr_seg = sym_ptr = new_sym(2);
                        curr_pc = 0;
                        last_txt_org = last_txt_end = 0;  /* reset the offset */
                        if (!chk_mdf(0,sym_ptr,0))
						{
							continue;
						}
                        if ((seg_ptr=sym_ptr->seg_spec) == 0)
                        {
                            if ((seg_ptr=get_seg_spec_mem(sym_ptr)) == 0) continue;
                        }
                        insert_id((int32_t)vseg->vseg_ident,sym_ptr);
                        sym_ptr->flg_defined = 1;     /* .seg defines the segment */
                        sym_ptr->flg_segment = 1;     /* signal its a segment */
                        sym_ptr->flg_ovr = (vseg->vseg_flags&VSEG_OVR) != 0;
                        seg_ptr->seg_salign = vseg->vseg_salign; /* set the alignment factor */
                        seg_ptr->seg_dalign = vseg->vseg_dalign; /* set data alignment factor */
                        sym_ptr->flg_abs = (vseg->vseg_flags&(VSEG_ABS|VSEG_BASED)) != 0;
                        sym_ptr->flg_based = (vseg->vseg_flags&VSEG_BASED) != 0;
                        seg_ptr->seg_base = vseg->vseg_base;
                        seg_ptr->seg_offset = vseg->vseg_offset;
                        seg_ptr->sflg_data = (vseg->vseg_flags&VSEG_DATA) != 0;
                        seg_ptr->sflg_noref = (vseg->vseg_flags&VSEG_REFERENCE) == 0;
                        seg_ptr->sflg_literal = (vseg->vseg_flags&VSEG_LITERAL) == 0;
                        if (!(new_symbol & 4) &&  /* if not a duplicate symbol */
                            !sym_ptr->flg_member)
                        { /* and not already a group member */
                            if (sym_ptr->flg_abs)
                            {
                                if (abs_group == 0)
                                {
                                    abs_group_nam = get_symbol_block(1);
                                    abs_group_nam->ss_string = "Absolute_sections";
                                    abs_group = get_grp_ptr(abs_group_nam,(int32_t)0,(int32_t)0);
                                    abs_group_nam->ss_fnd = current_fnd;
                                    abs_group_nam->flg_based = 1;
                                    abs_group_nam->seg_spec->sflg_absolute = 1;
                                }
                                insert_intogroup(abs_group,sym_ptr,abs_group_nam);
                            }
                            else
                            {
                                if (inp_minor >= VLDA_MINOR && (vseg->vseg_flags&VSEG_BPAGE))
                                {
                                    if (base_page_grp == 0)
                                    {
                                        base_page_nam = get_symbol_block(1);
                                        base_page_nam->ss_string = "Zero_page_sections";
                                        base_page_nam->ss_fnd = current_fnd;
                                        base_page_grp = get_grp_ptr(base_page_nam,(int32_t)0,(int32_t)0);
                                        base_page_nam->flg_based = 1;
                                        base_page_nam->seg_spec->seg_maxlen = 256;
                                        base_page_nam->seg_spec->sflg_zeropage = 1;
                                    }
                                    insert_intogroup(base_page_grp,sym_ptr,base_page_nam);
                                }
                                else
                                {
                                    insert_intogroup(group_list_top,sym_ptr,group_list_default);
                                }
                            }
                        }
                        if (new_symbol & 4)
                        {     /* if duplicate symbol */
                            fseg_ptr = first_symbol->seg_spec;
#if 0
                            if (seg_ptr->seg_salign != fseg_ptr->seg_salign)
                            {
                                sprintf(emsg,
                                        "Segment {%s} declared in %s with alignment of %d\n\t%s%s%s%d",
                                        sym_ptr->ss_string,current_fnd->fn_buff,seg_ptr->seg_salign,
                                        "and is declared in ",first_symbol->ss_fnd->fn_buff,
                                        " with alignment of ",fseg_ptr->seg_salign);
                                err_msg(MSG_WARN,emsg);
                            }
                            if (seg_ptr->seg_dalign != fseg_ptr->seg_dalign)
                            {
                                sprintf(emsg,
                                        "Segment {%s} declared in %s with combin of %d\n\t%s%s%s%d",
                                        sym_ptr->ss_string,current_fnd->fn_buff,0,"and is declared in ",
                                        first_symbol->ss_fnd->fn_buff," with combin of ",
                                        fseg_ptr->seg_dalign);
                                err_msg(MSG_WARN,emsg);
                            }
#endif
                            seg_ptr->seg_group = fseg_ptr->seg_group; /* same group */
                        }
                        if (vseg->vseg_noff >= sizeof(struct vlda_seg))
                        {
                            struct seg_spec_struct *sgp;
                            sym_ptr->seg_spec->seg_maxlen = vseg->vseg_maxlen;
                            if (sym_ptr->seg_spec->seg_group != 0)
                            {
                                sgp = sym_ptr->seg_spec->seg_group->seg_spec;
                                if (sgp->seg_maxlen > vseg->vseg_maxlen) sgp->seg_maxlen = vseg->vseg_maxlen;
                            }
                        }
                    }
                    else
                    {             /* symbol */
                        if (id_table_base+vsym->vsym_ident < id_table_size && 
                            (sym_ptr = *(id_table+id_table_base+vsym->vsym_ident)) != 0)
                        {
                            if (strcmp(sym_ptr->ss_string,token_pool) != 0 )
                            {
                                sprintf(emsg,"ID {%s}%%%d redefined in %s\n\tWas defined to {%s}%%%d in file %s",
                                        token_pool,vsym->vsym_ident,current_fnd->fn_buff,
                                        sym_ptr->ss_string,vsym->vsym_ident,sym_ptr->ss_fnd->fn_buff);
                                err_msg(MSG_WARN,emsg);
                            }
                            if (vsym->vsym_flags&VSYM_LCL)
                            {
                                if (sym_ptr->ss_fnd == current_fnd)
                                {
                                    sym_ptr = sym_delete(sym_ptr);
                                }
                                else
                                {
                                    SS_struct *osp;
                                    osp = sym_ptr;
                                    sym_ptr = (SS_struct *)get_symbol_block(1);
                                    sym_ptr->ss_fnd = current_fnd;
                                    sym_ptr->ss_string = osp->ss_string;
                                    *(id_table+id_table_base+vsym->vsym_ident) = sym_ptr;
                                }
                            }
                        }
                        else
                        {
                            if (vsym->vsym_flags&VSYM_LCL)
                            {
                                sym_ptr = (struct ss_struct *)get_symbol_block(1);
                                sym_ptr->ss_fnd = current_fnd;
                                sym_ptr->ss_string = token_pool;
                                token_pool += token_value+1;    /* update the free mem pointer */
                                token_pool_size -= token_value+1; /* and size */
                            }
                            else
                            {
                                sym_ptr = new_sym(1);
                            }
                            insert_id((int32_t)vsym->vsym_ident,sym_ptr);
                        }
                        if (qual_tbl[QUAL_CROSS].present && !(vsym->vsym_flags&VSYM_LCL) )
                            do_xref_symbol(sym_ptr,(vsym->vsym_flags&VSYM_DEF) != 0);
                        if (vsym->vsym_flags&VSYM_DEF)
                        {  /* if symbol being defined */
							if ( (vsym->vsym_flags&VSYM_EXP) )
							{
								inp_vldaexp(inp_str+vsym->vsym_eoff); /* unpack expression into expr_stack[0] */
							}
							else
							{
								expr_stack_ptr = 1;
								expr_stack[0].expr_code = EXPR_VALUE;
								expr_stack[0].expr_value = vsym->vsym_value;
							}
                            if (current_fnd->fn_stb)
                            { /* if from symbol file */
								if ( sym_ptr->flg_defined && (vsym->vsym_flags&VSYM_ABS) )
								{
									/* symbol is already defined and new symbol is absolute */
									if (   sym_ptr->flg_abs 					/* old symbol is absolute */
										&& !sym_ptr->flg_exprs					/* and there is no expression */
										&& sym_ptr->ss_value == vsym->vsym_value	/* and values match */
									   )
									{
										continue;	/* values the same, so okay as is */
									}
									if (   sym_ptr->ss_exprs					/* old symbol is defined via an expression */
										&& sym_ptr->ss_exprs->len == 1			/* with one term */
										&& sym_ptr->ss_exprs->ptr->expr_code == EXPR_VALUE	/* which is absolute */
										&& expr_stack_ptr == 1					/* and expression stack has one term */
										&& expr_stack[0].expr_code == EXPR_VALUE /* and it's absolute */
										&& sym_ptr->ss_exprs->ptr->expr_value == expr_stack[0].expr_value   /* and values match */
									   )
									{
										continue;	/* values the same, so not an error */
									}
								}
                            }
                            if (!chk_mdf(1,sym_ptr,qual_tbl[QUAL_QUIET].present))
							{
								continue; /* nfg */
							}
                            sym_ptr->flg_symbol = 1;       /* its a symbol */
                            sym_ptr->ss_fnd = current_fnd; /* remember definition */
                            sym_ptr->flg_abs = (vsym->vsym_flags&VSYM_ABS) != 0;
                            sym_ptr->ss_value = vsym->vsym_value; /* in case its abs */
                            sym_ptr->flg_local = (vsym->vsym_flags&VSYM_LCL) != 0;
                            sym_ptr->flg_exprs = (vsym->vsym_flags&VSYM_EXP) != 0;
                            write_to_symdef(sym_ptr);
                            sym_ptr->flg_defined = 1;  /* set defined bit */
                            sym_ptr->flg_nosym = current_fnd->fn_nosym;
                        }     /* -- defined */
                    }        /* -- sym/seg */
                    continue;
                }           /* -- case VLDA_GSD */
            case VLDA_SLEN: {   /* segment length */
                    sym_ptr = *(id_table+id_table_base+
                                ((struct vlda_slen *)vsym)->vslen_ident);
                    if (sym_ptr == 0)
                    {
                        sprintf(emsg,"Undefined segment indentifier %d used in \"%s\"",
                                ((struct vlda_slen *)vsym)->vslen_ident,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        continue;
                    }
                    if (!sym_ptr->flg_segment)
                    {
                        sprintf(emsg,"VLDA_SLEN record references a non-segment id of %d in \"%s\"",
                                ((struct vlda_slen *)vsym)->vslen_ident,current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        continue;
                    }
                    sym_ptr->seg_spec->seg_len = ((struct vlda_slen *)vsym)->vslen_len;
                    continue;
                }
            case VLDA_ORG: {
                    vlda_inp = 1;    /* vlda input */
                    inp_vldaexp((char *)(vtype+1)); /* unpack the expression */
                    write_to_tmp(TMP_ORG,(int32_t)expr_stack_ptr,(char *)expr_stack,sizeof(struct expr_token));
                    continue;
                }
            case VLDA_XFER: {
                    vlda_inp = 1;    /* vlda input */
                    inp_vldaexp((char *)(vtype+1)); /* unpack the expression */
                    if ( expr_stack_ptr == 1 && expr_stack[0].expr_code == EXPR_VALUE &&
                         (expr_stack[0].expr_value&~1) == 0) continue;
                    if (xfer_fnd != 0)
                    {
                        sprintf(emsg,"Transfer address spec'd in %s will not override one spec'd in %s\n",
                                current_fnd->fn_buff,xfer_fnd->fn_buff);
                        err_msg(MSG_WARN,emsg);
                        continue;
                    }
                    xfer_fnd = current_fnd;
                    write_to_tmp(TMP_START,(int32_t)expr_stack_ptr,(char *)expr_stack,sizeof(struct expr_token));
                    continue;
                }
            case VLDA_ID: {
                    vlda_inp = 1;    /* vlda input */
                    inp_major = vid->vid_maj;
                    inp_minor = vid->vid_min;
                    if (inp_major != VLDA_MAJOR)
                    {
                        sprintf(emsg,"Object format in file \"%s\" is incompatible. You must reassemble/recompile it.",
                                current_fnd->fn_buff);
                        err_msg(MSG_ERROR,emsg);
                        break;
                    }
                    if (inp_minor < VLDA_MINOR)
                    {
                        if (qual_tbl[QUAL_ERR].present)
                        {
                            sprintf(emsg,"Object file format in \"%s\" is obsolete. Suggest you remake it.",
                                    current_fnd->fn_buff);
                            err_msg(MSG_WARN,emsg);
                        }
                    }
                    else
                    {
                        if (vid->vid_errors != 0)
                        {
                            sprintf(emsg,"%d compilation/assembly error(s) detected during make of \"%s\"",
                                    vid->vid_errors,current_fnd->fn_buff);
                            err_msg(MSG_WARN,emsg);
                        }
                        if (vid->vid_warns != 0)
                        {
                            sprintf(emsg,"%d compilation/assembly warning(s) detected during make of \"%s\"",
                                    vid->vid_warns,current_fnd->fn_buff);
                            err_msg(MSG_WARN,emsg);
                        }
                    }
                    d = token_pool;
                    current_fnd->fn_xlator = d;
                    s = inp_str+vid->vid_image;
                    while ((*d++ = *s++) != 0);
                    current_fnd->fn_target = d;
                    if (target == 0) target = d;
                    s = inp_str+vid->vid_target;
                    if (*s == '"') ++s;      /* skip leading double quotes */
                    while ((*d++ = *s++) != 0);
                    if (*(d-2) == '"')
                    {
                        --d; *(d-1) = 0;
                    } /* skip trailing quotes */
                    current_fnd->fn_credate = d;
                    s = inp_str+vid->vid_time;
                    while ((*d++ = *s++) != 0);
                    token_pool_size -= d-token_pool;
                    token_pool = d;
                    continue;
                }
            case VLDA_OOR:
            case VLDA_BOFF:
            case VLDA_TEST: {   /* test expression */
                    char *strng;
                    int ii;
                    static int tmp_opr[3] = {TMP_TEST, TMP_BOFF, TMP_OOR};
                    vlda_inp = 1;
                    strng = (char *)vtst;
                    inp_vldaexp(strng+vtst->vtest_eoff);
                    ii = 0;
                    if (rectyp == VLDA_BOFF) ii = 1;
                    else if (rectyp == VLDA_OOR) ii = 2;
                    write_to_tmp(tmp_opr[ii], (int32_t)expr_stack_ptr,(char *)expr_stack,
                                 sizeof(struct expr_token));
                    strng += vtst->vtest_soff;
                    write_to_tmp(TMP_ASTNG,(int32_t)strlen(strng),strng,sizeof(char));
                    continue;
                }
            case VLDA_EXPR: {
                    vlda_inp = 1;    /* vlda input */
                    inp_vldaexp((char *)(vtype+1));
                    write_to_tmp(TMP_EXPR,(int32_t)expr_stack_ptr,(char *)expr_stack,
                                 sizeof(struct expr_token));
                    if (taglen == 0) taglen = 1;
                    write_to_tmp(TMP_TAG,taglen,&tag,1);
                    continue;
                }       /* -- VLDA_EXPR */
            case VLDA_DBGDFILE: {
                    int siz;
                    siz = strlen(inp_str+vdbgfile->name)+strlen(inp_str+vdbgfile->version)+2;
                    if (token_pool_size < siz)
                    {
                        int tsiz = MAX_TOKEN*8;
                        if (siz > tsiz) tsiz += siz;
                        token_pool_size = tsiz;
                        token_pool = MEM_alloc(token_pool_size);
                        misc_pool_used += token_pool_size;
                    }
                    strcpy(token_pool,inp_str+vdbgfile->name);
                    current_fnd->od_name = token_pool;
                    token_pool += strlen(token_pool)+1;
                    strcpy(token_pool,inp_str+vdbgfile->version);
                    current_fnd->od_version = token_pool;
                    token_pool += strlen(token_pool)+1;
                    token_pool_size -= siz;
                    continue;
                }
            }      /* -- switch */
            break;     /* fell out of switch, so fall out of while */
        }         /* -- while !EOF */
#if defined(VMS) && defined(RT11_RSX)
        if (rsx_fd && !vlda_inp)
        {
            if (++rsx_pass < 2)
            {
                lseek(file_fd,0,0);
                continue;    /* read the file again */
            }
        }
#endif
        break;        /* fall out of while(1) */
    }            /* while(1) */
    return ;     /* done here */
}
