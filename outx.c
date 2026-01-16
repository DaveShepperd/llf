/*
    outx.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

/* 			OUTX
*	A collection of output routines for producing extended TEKHEX and
*	VLDA output.
*	The symbol and object output routines each gather their own type
*	of output until either the column outx_width would be exceeded, a type-
*	specific break occurs (e.g. change of address in object output),
*	or a call is made to flush...(). Symbol and object lines may be
*	intermingled, but only at the line level. These are fairly special
*	purpose routines, intended to be used with CHKFOR, a combination
*	formatter and checksum generator.
*/
#include "version.h"
#ifndef VMS
    #define sp outx_sp
#endif
#include <stdio.h>		/* get standard I/O definitions */
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include "token.h"		/* define compile time constants */

#include "structs.h"		/* define structures */
#include "header.h"
#include "vlda_structs.h"
#include "exproper.h"

#undef NULL
#define NULL 0

#ifdef VMS
    #define NORMAL 1
    #define ERROR 0x10000004
#else
    #define NORMAL 0
    #define ERROR 1
#endif

#define	DEBUG 9

#ifdef lint
static void null()
{
    return;
}
static int inull()
{
    return 1;
}
    #define fputs(str,fp) null()
    #define fwrite(str,cnt,siz,fp) inull()
#endif

/*
            DATA
*/
FILE   *outxabs_fp;
FILE   *outxsym_fp;
int    outx_lineno;
int    outx_width=78;   /* object width */
int    outx_swidth=78;  /* symbol width */
int    outx_debug;

#if 0
extern char *target;
extern char ascii_date[];
extern SS_struct *last_seg_ref;
extern SS_struct *base_page_nam;
#endif

int new_identifier=1;        /* new identifiers */

/* line buffers for Ext-Tek output */
char    *eline;         /* pass2 builds expressions here */
static  char    *sline;
static  char    *oline;

/* pointers into lines and checksums for lines */
static       char   *sp=0,*op=0,*maxop=0;
static  unsigned int    symcs,objcs;
static  uint32_t   addr;
static  uint8_t   varcs;
static  uint8_t   namcs;

static char hexdig[] = "0123456789ABCDEF";

static VLDA_sym *vlda_sym;  /* pointer to line area */
static VLDA_seg *vlda_seg;  /* pointer to segment definition */
static VLDA_abs *vlda_oline;
static Sentinel *vlda_type;
static MY_desc *vid;

static int formvar();

void outx_init( void )
{
    eline = MEM_alloc(MAX_LINE);
    sline = MEM_alloc(MAX_LINE);
    oline = MEM_alloc(MAX_LINE);
    vlda_sym = (VLDA_sym *)sline;
    vlda_seg = (VLDA_seg *)sline;
    vlda_oline = (VLDA_abs *)oline;
    vlda_type = (Sentinel *)oline;
    vid = (struct my_desc *)oline;
    return;
}

char *get_symid(SS_struct *sym_ptr, char *s)
{
    if (sym_ptr->flg_ident)
    {
        sprintf(s," %%%d",sym_ptr->ss_ident);
    }
    else
    {
        sym_ptr->ss_ident = new_identifier++;
        sym_ptr->flg_ident = 1;
        if (sym_ptr->flg_local)
        {
            sprintf(s," {.L%05d}%%%d",sym_ptr->ss_ident,sym_ptr->ss_ident);
        }
        else
        {
            char *ts;
            ts = (char *)0;
            if (sym_ptr->flg_segment)
            {    /* if this is a segment */
                int tmp;
                tmp = strlen(sym_ptr->ss_string);
                if (tmp > 0)
                {
                    ts = sym_ptr->ss_string + tmp - 1;
                    if (*ts == ' ')
                    {
                        *ts = 0;
                    }
                    else
                    {
                        ts = (char *)0;
                    }
                }
            }
	    /* NOTE: This is just to keep gcc from complaining. The size limit is wrong.
	     * DO NOT DEPEND ON IT HERE!! It needs to be completely re-written to pass
	     * in a limit and keep track of it while copying stuff. However, since it
	     * was written several decades ago (in the 1980's), I think before there
	     * was a snprinf() function in libc, and it hasn't blown up in all that time,
	     * it probably won't break. But if it does, this might be a place to look.
	     */
            snprintf(s,MAX_LINE," {%s}%%%d",sym_ptr->ss_string,sym_ptr->ss_ident);
            if (ts != (char *)0) *ts = ' ';
        }
    }
    while (*s++);
    --s;
    return(s);
}

static int16_t rsize,even_odd=0;

static char *outexp_vlda( EXP_stk *eptr, char *s, int *lp )
{
    SS_struct *sym_ptr;
    EXPR_token *exp;
    VLDA_vexp ve;
    uint32_t cmp;
    int len;

    exp = eptr->ptr;
    len = eptr->len;
    *lp += len;          /* accumulate lengths of expressions */
    ve.vexp_chp = s;     /* point to output array */
    for (;len > 0; --len,++exp)
    {
        switch (exp->expr_code)
        {
        case EXPR_L:   /* or segment lenght.. */
            *ve.vexp_type++ = VLDA_EXPR_L; /* insert type code */
            goto vlda_commexp1;
        case EXPR_B:   /* or segment base */
            *ve.vexp_type++ = VLDA_EXPR_B; /* insert type code */
            vlda_commexp1:
        case EXPR_SYM: {   /* symbol or segment */
                int32_t val;
                sym_ptr = exp->ss_ptr;
                if (sym_ptr->flg_segment && sym_ptr->flg_based)
                {
                    val = exp->expr_value - sym_ptr->seg_spec->seg_base;
                }
                else
                {
                    val = exp->expr_value;
                }
                if (!sym_ptr->flg_ident)
                {
                    sym_ptr->flg_ident = 1;
                    sym_ptr->ss_ident = new_identifier++;
                }
                if (exp->expr_code == EXPR_SYM)
                {
                    if (sym_ptr->ss_ident > 255)
                    {
                        *ve.vexp_type++ = VLDA_EXPR_SYM; /* insert type code */
                        *ve.vexp_ident++ = sym_ptr->ss_ident;
                    }
                    else
                    {
                        *ve.vexp_type++ = VLDA_EXPR_CSYM; /* insert type code */
                        *ve.vexp_byte++ = sym_ptr->ss_ident;
                    }
                }
                else
                {
                    *ve.vexp_ident++ = sym_ptr->ss_ident;
                }
                cmp = (val >= 0) ? val : -val;
                if (cmp == 0)
                {
                    continue;
                }
                else if (cmp < 128)
                {
                    *ve.vexp_type++ = VLDA_EXPR_CVALUE;
                    *ve.vexp_byte++ = val;
                }
                else if (cmp < 32768)
                {
                    *ve.vexp_type++ = VLDA_EXPR_WVALUE;
                    *ve.vexp_word++ = val;
                }
                else
                {
                    *ve.vexp_type++ = VLDA_EXPR_VALUE;
                    *ve.vexp_long++ = val;
                }
                *ve.vexp_type++ = VLDA_EXPR_OPER;
                *ve.vexp_oper++ = EXPROPER_ADD;
                *lp += 2;       /* added 2 terms to expression */
                continue;
            }
        case EXPR_VALUE: { /* constant */
                cmp = (exp->expr_value >= 0) ? exp->expr_value : -exp->expr_value;
                if (cmp == 0)
                {
                    *ve.vexp_type++ = VLDA_EXPR_0VALUE;
                    continue;
                }
                else if (cmp < 128)
                {
                    *ve.vexp_type++ = VLDA_EXPR_CVALUE;
                    *ve.vexp_byte++ = exp->expr_value;
                }
                else if (cmp < 32768)
                {
                    *ve.vexp_type++ = VLDA_EXPR_WVALUE;
                    *ve.vexp_word++ = exp->expr_value;
                }
                else
                {
                    *ve.vexp_type++ = VLDA_EXPR_VALUE;
                    *ve.vexp_long++ = exp->expr_value;
                }
                continue;
            }
        case EXPR_OPER: {  /* operator */
                *ve.vexp_type++ = VLDA_EXPR_OPER; /* insert type code */
                *ve.vexp_oper = exp->expr_value;
                if (*ve.vexp_oper++ == '!')
                {
                    *ve.vexp_oper++ = exp->expr_value >> 8;
                }
                continue;
            }
        case EXPR_LINK: {  /* link to another expression */
                *lp -= 1;       /* the link doesn't count in the total */
                ve.vexp_chp = outexp_vlda((EXP_stk *)exp->ss_ptr,
                                          ve.vexp_chp,lp);
                continue;
            }
        default: {
                sprintf(emsg,"Undefined expression code: %d",exp->expr_code);
                err_msg(MSG_ERROR,emsg);
                continue;
            }          /* --default */
        }             /* --switch */
    }                /* --for */
    return ve.vexp_chp;      /* exit */
}

static char *outexp_ol( EXP_stk *eptr, char *s )
{
	SS_struct *sym_ptr;
	EXPR_token *exp;
	int len;

	exp = eptr->ptr;
	len = eptr->len;
	*s = 0;			 /* null terminate the dst string */
	for ( ;len > 0; --len,++exp )
	{
		switch ( exp->expr_code )
		{
		case EXPR_B:
			{
				*s++ = ' ';	/* prefix a space */
				*s++ = 'B';	/* insert a B */
				goto ol_commexp1;
			}		   /* fall all the way through to EXPR_SYM */
		case EXPR_L:
			{
				*s++ = ' ';	/* prefix a space */
				*s++ = 'L';	/* insert a L */
				goto ol_commexp1;
			}		   /* fall through to EXPR_SYM */
		case EXPR_SYM:	   /* symbol or segment */
			{
				int32_t val;
ol_commexp1:
				sym_ptr = exp->ss_ptr;
				s = get_symid(sym_ptr,s);
				if ( sym_ptr->flg_segment && sym_ptr->flg_based )
				{
					val = exp->expr_value - sym_ptr->seg_spec->seg_base;
				}
				else
				{
					val = exp->expr_value;
				}
				if ( val != 0 )
				{
					sprintf(s," %d +",val);
					while ( *s++ );
					--s;
				}
				break;
			}
		case EXPR_VALUE:
			{
				sprintf(s," %d",exp->expr_value);
				while ( *s++ );
				--s;
				break;
			}
		case EXPR_OPER:
			{
				*s++ = ' ';	/* a space */
				*s = exp->expr_value;
				if ( *s++ == '!' )
				{
					*s++ = exp->expr_value >> 8;
				}
				*s = 0;		/* move the null */
				break;
			}
		case EXPR_LINK:
			{
				s = outexp_ol((EXP_stk *)exp->ss_ptr,s);
				break;
			}
		default:
			{
				sprintf(emsg,"Undefined expression code: %d",exp->expr_code);
				err_msg(MSG_ERROR,emsg);
				break;
			}		   /* --default */
		}			  /* --switch */
	}				 /* --for */
	return s;
}

char *outexp(EXP_stk *eptr, char *s, int tag, int32_t tlen, char *wrt, FILE *fp)
{
    VLDA_vexp ve,len_ptr;
    int len;
    uint32_t cmp;

    if (qual_tbl[QUAL_VLDA].present)
    {
        len = 0;
        len_ptr.vexp_chp = ve.vexp_chp = s;   /* point to output array */
        ++ve.vexp_len;
        ve.vexp_chp = outexp_vlda(eptr,ve.vexp_chp,&len);
        if (tag != 0)
        {
            ++len;
            cmp = (tlen >= 0) ? tlen : -tlen;
            if (cmp < 2)
            {
                *ve.vexp_type++ = VLDA_EXPR_1TAG;   /* insert tag code */
                *ve.vexp_type++ = tag;      /* insert tag code */
            }
            else if (cmp < 128)
            {
                *ve.vexp_type++ = VLDA_EXPR_CTAG;   /* insert tag code */
                *ve.vexp_type++ = tag;      /* insert tag code */
                *ve.vexp_byte++ = tlen;
            }
            else if (cmp < 32768)
            {
                *ve.vexp_type++ = VLDA_EXPR_WTAG;   /* insert tag code */
                *ve.vexp_type++ = tag;      /* insert tag code */
                *ve.vexp_word++ = tlen;
            }
            else
            {
                *ve.vexp_type++ = VLDA_EXPR_TAG;    /* insert tag code */
                *ve.vexp_type++ = tag;      /* insert tag code */
                *ve.vexp_long++ = tlen;
            }
        }
        *len_ptr.vexp_len = len;          /* expression size (in items) */
        if (wrt==0) return(ve.vexp_chp); /* exit */
#ifndef VMS
        rsize = ve.vexp_chp-wrt;
        fwrite((char *)&rsize,sizeof(int16_t),1,fp);
        even_odd = rsize&1;
#endif
        fwrite(wrt,(int)(ve.vexp_chp-wrt)+even_odd,1,fp); /* write it */
        return ve.vexp_chp;       /* exit */
    }
    else
    {         /* -+ vlda */
        s = outexp_ol(eptr,s);
        if (tag)
        {
            if (tlen < 2)
            {
                *s++ = ':';     /* follow with tag code */
                *s++ = tag;     /* and tag */
            }
            else
            {
                sprintf(s,":%c %d",tag,tlen); /* follow with tag and length */
                while (*s++);
                --s;            /* backup to NULL */
            }
        }
        if (wrt)
        {
            *s++ = '\n';
            *s = 0;
            fputs(wrt,fp);
        }
    }                /* --vlda */
    return(s);
}

char *outxfer( EXP_stk *exp, FILE *fp )
{
    if (output_mode == OUTPUT_OBJ || output_mode == OUTPUT_VLDA)
    {
        flushobj();       /* flush the buffer */
        *vlda_type = VLDA_XFER;
        outexp(exp,(char *)(vlda_type+1),0,0l,oline,outxabs_fp);
    }
    else if (output_mode == OUTPUT_OL)
    {
        char *s;
        strcpy(eline,".start");
        s = eline+6;      /* null terminate the dst string */
        s = outexp_ol(exp,s); /* output the expression in .ol format */
        *s++ = '\n';      /* terminate the string */
        *s = 0;
        fputs(eline,fp);
    }                /* --vlda or ol */
    return(0);
}

/*			outbstr(from,len)
*	Output a byte string, accumulating the checksums for the output record
*	and both the even and odd ROMs. This routine converts each byte to two
*	hex digits, in contrast to outhstr() which assumes you already have.
*/
void outbstr( uint8_t *from, int len )
{
    register char *rop;
    uint8_t c;
    int  k,limit,toeol;
    if (!qual_tbl[QUAL_VLDA].present)
    {
        len += len;       /* double the input length if ASCII output */
    }
    else
    {
        toeol = oline+ outx_width - op;
        if (len >= toeol-2) flushobj();
    }
    while (len > 0)
    {
        if ( op != 0 )
        {
            toeol = oline + outx_width - op;
            if (toeol  < 2 )
            {
                flushobj();
            }
        }
        if ( op == NULL )
        {       /* After flushing, or when first... */
            switch (output_mode)
            {     /* dispatch on output mode */
            case OUTPUT_HEX: {
                    strcpy(oline,"%ll6kk");  /* ...started, set up the buffer */
                    op = oline + 6;      /* with header and address */
                    op += formvar(addr,op);
                    objcs = varcs + 6;
                    break;           /* out of switch */
                }
            case OUTPUT_OL: {
                    op = oline;      /* point to start of buffer */
                    *op++ = '\'';        /* start with a single quote */
                    break;
                }
            case OUTPUT_OBJ:
                vlda_oline->vlda_type = VLDA_TXT;
                vlda_oline->vlda_addr = addr; /* load address */
                op = oline + sizeof(VLDA_abs);
                break;
            case OUTPUT_VLDA: {
                    vlda_oline->vlda_type = VLDA_ABS;
                    vlda_oline->vlda_addr = addr; /* load address */
                    op = oline + sizeof(VLDA_abs);
                    break;
                }
            }
            toeol = oline + outx_width - op;
            maxop = op;
        }
        limit = (toeol < len) ? toeol : len ;
        if ((limit &= ~1) == 0) limit = 1;
        if (outx_debug > 2)
        {
            fprintf(stderr,"Loading %d bytes at addr %08x in rcd %08X\n",
                    limit,addr,vlda_oline->vlda_addr);
        }
        rop = op;
        len -= limit;
        if (!qual_tbl[QUAL_VLDA].present)
        {
            addr += limit/2;       /* update address */
            for (  ; limit > 0 ; limit -= 2)
            {  /* first transfer the high nibble */
                k = *from++;
                c = k >> 4;
                objcs += c;
                *rop++ = hexdig[c];
                c = k & 0xF;        /* now transfer the low one */
                objcs += c;
                *rop++ = hexdig[c];
            }              /* end for */    
        }
        else
        {
            addr += limit;
            for (  ; limit > 0 ; --limit)
            {
                *rop++ = *from++;
            }
        }
        op = rop;
        if (maxop < op) maxop = op;
    }        /* end while */
}

/*			outorg(address,exp_ptr)
*	Sets the origin for following outbstr or outhstr calls. If address
*	supplied is different from current address (in addr), flushes object
*	buffer.
*/

void outorg( uint32_t address, EXP_stk *exp_ptr )
{
    uint32_t taddr,laddr;       /* address of end of txt + 1 */
    switch (output_mode)
    {       /* how to encode it */
    case OUTPUT_HEX: {        /* absolute tekhex mode */
            if ( address == addr) return;  /* nothing to do */
            if ( op != 0 )
            {       /* if there's something in the buffer */
                flushobj();         /* flush out tekhex files */
            }
            break;
        }  
    case OUTPUT_OL: {         /* relative .OL output */
            char *s;
            flushobj();            /* flush the buffer */
            if (exp_ptr->len < 1)
            {
                err_msg(MSG_ERROR,"Too few terms in expression setting .org");
                exp_ptr->len = 1;
            }
            strcpy(oline,".org");
            s = outexp_ol(exp_ptr,oline + 4);
            if (exp_ptr->len == 1 && exp_ptr->ptr->expr_value == 0)
            {
                *s++ = ' ';
                *s++ = '0';
            }
            else
            {
                --s;
                if (*s != '+') ++s;
            }
            *s++ = '\n';
            *s = 0;
            fputs(oline,outxabs_fp);
            break;
        }
    case OUTPUT_VLDA: {       /* absolute VLDA mode */
            if ( address == addr) return;  /* nothing to do */
            if ( op != 0 )
            {       /* if there's something in the buffer */
                taddr = maxop - (oline + sizeof(VLDA_abs)) +
                        (laddr=vlda_oline->vlda_addr);
                if ( address > taddr ||     /* if off the end of the txt rcd */
                     address < laddr)
                {    /* or he's backpatching too far */
                    flushobj();      /* flush out the current txt record */
                }
                else
                {
                    op = oline + address-laddr + sizeof(VLDA_abs);
                    /* otherwise, just backup into txt */
                }
            }
            break;             /* fall out of switch */
        }                 /* -- case 0,1 */
    case OUTPUT_OBJ: {        /* relative VLDA output */
            flushobj();            /* flush the buffer */
            *vlda_type = VLDA_ORG;
            outexp(exp_ptr,(char *)(vlda_type+1),0,0l,oline,outxabs_fp);
            break;
        }
    }
    if (outx_debug > 2) fprintf(stderr,"Changing ORG from %08X to %08X\n",addr,address);
    addr = address;          /* set the new address */
    return;
}

/*			outbyt(num,where)
*	"Output" a byte as two hex digits in buffer at where. return the
*	checksum.
*/

int outbyt( unsigned int num, char    *where )
{
    int chk;
    chk = (num >> 4) &0xF;
    *where++ = hexdig[chk];
    *where = hexdig[num &= 0xF];
    return(chk+num);
}


/*			formvar(num,where)
*	Format a variable length number in buffer pointed to by where,
*	returning the string length (number length + 1 for count digit).
*	This preliminary is required to check if the number will fit on
*	the current line. The variable varcs will contain the contribution
*	of this string to the checksum.
*/
int formvar( uint32_t num, char *where )
{
    int count;
    register char *vp;
    register char c;
    uint32_t msk;
    vp = where;

/*	First, predict where the string will end    */

    for (count = 0,msk = ~0 ; msk & num ; msk <<= 4) ++count;
    if ( count == 0 ) ++count;
    vp = &where[count+1];
    *vp = 0;
    varcs = 0;

/*	Now, output characters from lsd to msd    */
    do
    {
        c = num & 0xF;
        varcs += c;
        *--vp = hexdig[(int)c];
    } while ( (num >>= 4) != 0);
    c = (count &= 0xF);
    varcs += c;
    *--vp = hexdig[(int)c];
    if ( vp != where )
    {
        fprintf(stderr,"OUTX:formvar- bad estimate, %p != %p\n", (void *)vp, (void *)where);
    }
    return(count+1);
}

/*			formsym(name,type,address,where)
*	Format a symbol entry in buffer at where, returning the string length
*	(name length + 2 for type and count digit + length of address from
*	formvar(). This preliminary is required to check if the symbol will
*	fit on the current line.
*/
int formsym( char *name, int type, uint32_t address, char *where )
{
    int i;
    char *np,c;
    namcs = type;
    *where = type + '0';
    np = &where[2];
    for ( i = 0 ; i < 16 ; ++i)
    {
        if ( (c = name[i]) == NULL ) break;
        *np++ = c;
#ifndef isdigit
        if ( c >= '0' && c <= '9' ) c -= '0';
        else if ( c >= 'A' && c <= 'Z' ) c -= ('A' - 10);
        else if ( c >= 'a' && c <= 'z' ) c -= ('a' - 40);
#else
        if ( isdigit(c) ) c -= '0';
        else if ( isupper(c) ) c -= ('A' - 10);
        else if ( islower(c) ) c -= ('a' - 40);
#endif
        else if ( c == '.' )  c = 38;
        else if ( c >= '_' )  c = 39;
        namcs += c;
    }
    if ( i == 0 )
    {
        fprintf(stderr,"OUTX:formsym- line %d: Bad symbol %s\n",outx_lineno,name);
        return(0);
    }
    namcs += ( i &= 0xF );
    where[1] = hexdig[i];
    i = formvar(address,np);
    namcs += varcs;
    np[i] = NULL;
    return((np-where) + i);
}

static char *sline_ptr;

void outsym( SS_struct *sym_ptr, int mode )
{
    int len;
    char save1,save2,*ssp;
    char *ts;

/*	If the first time through, set up the head of a block, and init
 *	checksum and length
 */
    if ( sp == NULL )
    {
        sline_ptr = sline;
        if (mode == OUTPUT_VLDA) *sline_ptr++ = VLDA_TPR;
        strcpy(sline_ptr,"%ll3kk2S_");    /* length will overwrite ll, checksum kk */
        symcs = 0x48;
        sp = sline_ptr+9;
    }                    /* --sp == null */
    ts = (char *)0;
    if (sym_ptr->flg_segment)
    {  /* if this is a segment */
        int tmp;
        tmp = strlen(sym_ptr->ss_string);
        if (tmp > 0)
        {
            ts = sym_ptr->ss_string + tmp - 1;
            if (*ts == ' ')
            {
                *ts = 0;
            }
            else
            {
                ts = (char *)0;
            }
        }
    }
    len = formsym(sym_ptr->ss_string,1,(uint32_t)sym_ptr->ss_value,sp);
    if (ts != (char *)0) *ts = ' ';
    if ( sp - sline + len >= outx_swidth)
    {

/*	We can't fit the current symbol on the line, so we stop here and output
*	the line, then reset to initial conditions plus new symbol.
*/
        symcs += outbyt((unsigned int)(sp - (sline_ptr+1)),sline_ptr+1);
        outbyt(symcs,sline_ptr+4);
        ssp = sp;
        save1 = *ssp++;
        save2 = *ssp;
        if (mode == OUTPUT_VLDA)
        {
            *sp = '\r';
            *ssp = '\n';
#ifndef VMS
            rsize = ssp-sline+1;
            fwrite((char *)&rsize,sizeof(int16_t),1,outxsym_fp);
            even_odd = rsize&1;
#endif
            fwrite(sline,(int)(ssp-sline+1)+even_odd,1,outxsym_fp); /* write a transparent rcd */
        }
        else
        {
            *sp = '\n';
            *ssp = NULL;
            fputs(sline,outxsym_fp);
        }
        *sp = save1;      /* replace our NULL (repair the patch)*/    
        *ssp = save2;
        strcpy(sline_ptr+9,sp);
        symcs = 0x48 + namcs;
        sp = sline_ptr + 9 + len;
    }
    else
    {         /* --sp < outx_swidth */
        symcs += namcs;
        sp += len;
    }                /* --sp < outx_swidth */
    return;          /* done */
}

void outsym_def(SS_struct *sym_ptr, int mode )
{
    register char *s,*name=sym_ptr->ss_string;

    switch (mode)
    {
    case OUTPUT_VLDA:     /* vlda absolute */
    case OUTPUT_HEX: {    /* absolute output (tekhex/non-relative) */
            if (sym_ptr->flg_nosym == 0) outsym(sym_ptr,mode);
            return;        /* easy out */
        }
    case OUTPUT_OL: {     /* relative output (.OL format) */
            if (!sym_ptr->flg_defined)
            {
                strcpy(sline,".ext ");
            }
            else
            {
                strcpy(sline,".def");
                *(sline+4) = sym_ptr->flg_local ? 'l' : 'g';
            }
            s = get_symid(sym_ptr,sline+5);    /* get an ASCII ID */
            if (!sym_ptr->flg_defined)
            {
                strcpy(s,"\n");
            }
            else
            {
                if (sym_ptr->flg_exprs)
                {
                    outexp(sym_ptr->ss_exprs,s,0,0l,sline,outxsym_fp); /* output the expression */
                    return;
                }
                else
                {
                    sprintf(s," %d\n",sym_ptr->ss_value);
                }
            }
            fputs(sline,outxsym_fp);
            return;
        }
    case OUTPUT_OBJ: {    /* vlda relative */
            int flg;
            vlda_sym->vsym_rectyp = VLDA_GSD; /* signal its a GSD record */
            flg = VSYM_SYM;        /* it's a symbol */
            if (sym_ptr->flg_defined) flg |= VSYM_DEF; /* symbols may be defined */
            if (mode == OUTPUT_OBJ && sym_ptr->flg_exprs) flg |= VSYM_EXP;
            if (sym_ptr->flg_abs) flg |= VSYM_ABS; /* may-be abs */
            if (sym_ptr->flg_local) flg |= VSYM_LCL; /* may be local */
            vlda_sym->vsym_flags = flg;
            if (!sym_ptr->flg_ident)
            {
                sym_ptr->ss_ident = new_identifier++;
                sym_ptr->flg_ident = 1;
            }
            vlda_sym->vsym_ident = sym_ptr->ss_ident;
            vlda_sym->vsym_value = sym_ptr->ss_value;
            vlda_sym->vsym_noff = sizeof(VLDA_sym);
            s = sline + sizeof(VLDA_sym);
            while ((*s++ = *name++) != 0); /* copy in the symbol name string */
            if (vlda_sym->vsym_flags&VSYM_EXP)
            {
                vlda_sym->vsym_eoff = s - sline;
                s = outexp(sym_ptr->ss_exprs,s,0,0l,(char *)0,(FILE *)0);   /* output the expression */
            }
#ifndef VMS
            rsize = s-sline;
            fwrite((char *)&rsize,sizeof(int16_t),1,outxsym_fp);
            even_odd = rsize&1;
#endif
            fwrite(sline,(int)(s-sline)+even_odd,1,outxsym_fp); /* write it */
            return;        /* done */
        }             /* --case 2,3 */
    }                /* --switch */
    return;          /* done */
}

void outseg_def(SS_struct *sym_ptr, uint32_t len, int based )
{
    SEG_spec_struct *seg_ptr;
    register char *s,*name=sym_ptr->ss_string;

    seg_ptr = sym_ptr->seg_spec;
    if (seg_ptr->seg_first != 0) sym_ptr = seg_ptr->seg_first;
    seg_ptr = sym_ptr->seg_spec;
    switch (output_mode)
    {
    case OUTPUT_VLDA:     /* absolute VLDA output */
    case OUTPUT_HEX: {    /* absolute output (tekhex/non-relative) */
            outsym(sym_ptr,output_mode);
            return;        /* easy out */
        }
    case OUTPUT_OL: {     /* relative output (.OL format) */
            strcpy(sline,".seg");
            s = get_symid(sym_ptr,sline+4);        /* get an ASCII ID */
            if (seg_ptr->seg_dalign != 0)
            {
                sprintf(s," %d %d {",
                        seg_ptr->seg_salign,seg_ptr->seg_dalign);
            }
            else
            {
                sprintf(s," %d %c {",
                        seg_ptr->seg_salign,sym_ptr->flg_ovr?'c':'u');
            }
            while (*s++);
            --s;
            if (sym_ptr->flg_abs)  *s++ = 'a';
            if (sym_ptr->flg_based) *s++ = 'b';
            if (sym_ptr->flg_ovr)  *s++ = 'c';
            if (seg_ptr->sflg_data) *s++ = 'd';
            if (sym_ptr->flg_noout) *s++ = 'o';
            if (seg_ptr->sflg_ro)  *s++ = 'r';
			if (seg_ptr->sflg_literal) *s++ = 'l';
            if (seg_ptr->sflg_noref) *s++ = 'u';
            if (seg_ptr->seg_group == base_page_nam)
            { /* if in BSECT group */
                *s++ = 'z';
            }
            *s++ = '}';
            *s++ = '\n';
            *s++ = 0;
            fputs(sline,outxsym_fp);
            fprintf(outxsym_fp,".len %%%d %d\n",
                    sym_ptr->ss_ident,len);
            if (based)
            {
                fprintf(outxsym_fp,".abs %%%d %d\n",
                        sym_ptr->ss_ident,seg_ptr->seg_base);
            }              /* --based */
            return;            /* done */
        }
    case OUTPUT_OBJ: {        /* vlda relative */
            int flg;
            vlda_seg->vseg_rectyp=VLDA_GSD; /* signal its a GSD record */
            flg = VSEG_DEF;        /* VSEG_SYM=0, and VSEG_DEF=1 */
            if (sym_ptr->flg_based) flg |= VSEG_BASED; /* may be based */
            if (sym_ptr->flg_abs | based) flg |= VSEG_ABS; /* may-be abs */
            if (sym_ptr->flg_ovr) flg |= VSEG_OVR; /* may be overlaid */
            if (sym_ptr->flg_noout) flg |= VSEG_NOOUT; /* if no output */
            if (seg_ptr->sflg_data) flg |= VSEG_DATA; /* segment is data/code */
            if (seg_ptr->sflg_ro) flg |= VSEG_RO; /* segment is read only */
			if (seg_ptr->sflg_literal) flg |= VSEG_LITERAL; /* segment is literal pool */
            if (!seg_ptr->sflg_noref) flg |= VSEG_REFERENCE; /* segment is referenced */
            if (seg_ptr->seg_group == base_page_nam)
            { /* if in BSECT group */
                flg |= VSEG_BPAGE;
            }
            vlda_seg->vseg_flags = flg;
            vlda_seg->vseg_ident = sym_ptr->ss_ident = new_identifier++;
            sym_ptr->flg_ident = 1;        /* signal an ID attached */
            vlda_seg->vseg_base = seg_ptr->seg_base; /* if based */
            vlda_seg->vseg_offset = seg_ptr->seg_offset;
            vlda_seg->vseg_salign = seg_ptr->seg_salign; /* copy alignments */
            vlda_seg->vseg_dalign = seg_ptr->seg_dalign;
            vlda_seg->vseg_noff = sizeof(VLDA_seg);
            s = sline + sizeof(VLDA_seg);
            while ((*s++ = *name++))
            {
                ;
            } /* copy in the segment name string */
            if (*(s-2) == ' ')
            {       /* if there's a trailing space */
                s -= 2;         /* eat it */
                *s++ = 0;
            }
#ifndef VMS
            rsize = s-sline;
            fwrite((char *)&rsize,sizeof(int16_t),1,outxsym_fp);
            even_odd = rsize&1;
#endif
            fwrite(sline,(int)(s-sline)+even_odd,1,outxsym_fp); /* write it */
            ((VLDA_slen *)vlda_seg)->vslen_rectyp=VLDA_SLEN; /* signal its a GSD record */
            ((VLDA_slen *)vlda_seg)->vslen_ident = sym_ptr->ss_ident;
            ((VLDA_slen *)vlda_seg)->vslen_len = len; /* copy the length */
#ifndef VMS
            rsize = sizeof(VLDA_slen);
            fwrite((char *)&rsize,sizeof(int16_t),1,outxsym_fp);
            even_odd = rsize&1;
#endif
            fwrite(sline,sizeof(VLDA_slen)+even_odd,1,outxsym_fp); /* write it */
        }             /* --case 2,3 */
    }                /* --switch */
    return;          /* done */
}

void flushobj( void )
{
    if (op != NULL)
    {        /* ok to flush? */
        switch (output_mode)
        {
        case OUTPUT_OL: {  /* ol format */
                strcpy(op,"'\n");   /* terminate the line with '\n */
                fputs(oline,outxabs_fp);
                break;
            }
        case OUTPUT_HEX: {
                *op++ = '\n';   /* terminate with a nl. */
                *op = NULL;     /* then a null */
                objcs += outbyt((unsigned int)(op - (oline+2)),oline + 1);
                outbyt(objcs,oline+4);
                fputs(oline,outxabs_fp);
                break;
            }
        case OUTPUT_VLDA:
        case OUTPUT_OBJ: {
#ifndef VMS
                rsize = maxop-oline;
                fwrite((char *)&rsize,sizeof(int16_t),1,outxabs_fp);
                even_odd = rsize&1;
#endif
                fwrite(oline,(int)(maxop-oline)+even_odd,1,outxabs_fp);
                break;
            }
        }
        op = NULL;
    }
}

void flushsym( int mode)
{
    int i=2;
    if (sp)
    {            /* ok to flush? */
        if (mode == OUTPUT_VLDA)
        {
            *sp++ = '\r';
            i++;
        }
        *sp++ = '\n';     /* terminate line with new-line */
        *sp = NULL;
        symcs += outbyt((unsigned int)(sp - (sline_ptr+i)),sline_ptr+1);
        outbyt(symcs,sline_ptr+4);
        switch (mode)
        {
        case OUTPUT_VLDA: {
#ifndef VMS
                rsize = sp-sline;
                fwrite((char *)&rsize,sizeof(int16_t),1,outxsym_fp);
                even_odd = rsize&1;
#endif
                fwrite(sline,(int)(sp-sline)+even_odd,1,outxsym_fp); /* write it */
                break;
            }
        case OUTPUT_HEX: {
                fputs(sline,outxsym_fp);
                break;
            }
        }
        sp = NULL;
    }
    return;
}

/*	termobj(traddr) & termsym(traddr)
*	Outputs terminating record (including transfer address- traddr) to
*	output.
*/

void termobj( int32_t traddr )
{
    if ( op ) flushobj();
    if (output_mode == OUTPUT_HEX)
    {
        strcpy(oline,"%ll8kk");
        op = oline+6;
        op += formvar((uint32_t)traddr,op);
        objcs = varcs + 8;
        if ( ( oline+outx_width - op ) < 2 )
        {
            fprintf(stderr,"OUTX:termobj- Width (=%d) too small\n",outx_width);
        }
        *op++ = '\n';
        *op = NULL;
        objcs += outbyt((unsigned int)(op - (oline+2)),oline+1);
        outbyt(objcs,oline+4);
        fputs(oline,outxabs_fp);
    }
    else if (output_mode == OUTPUT_VLDA)
    {
        vlda_oline->vlda_type = VLDA_ABS;
        vlda_oline->vlda_addr = traddr;
#ifndef VMS
        rsize = sizeof(VLDA_abs);
        fwrite((char *)&rsize,sizeof(int16_t),1,outxabs_fp);
        even_odd = rsize&1;
#endif
        fwrite(oline,sizeof(VLDA_abs)+even_odd,1,outxabs_fp);
#if 0
    }
    else if (qual_tbl[QUAL_REL].present)
    {
        fputs(".eof\n", outxabs_fp);
#endif
    }
    op = NULL;
    return;
}

void termsym( int32_t traddr )
{
    if ( sp != 0 ) flushsym(output_mode);
    if (output_mode == OUTPUT_HEX )
    {
        strcpy(sline,"%ll8kk");
        sp = sline+6;
        sp += formvar((uint32_t)traddr,sp);
        symcs = varcs + 8;
        if ( ( sline+outx_swidth - sp ) < 2 )
        {
            fprintf(stderr,"OUTX:termsym- Width (=%d) too small\n",outx_swidth);
        }
        *sp++ = '\n';
        *sp = NULL;
        symcs += outbyt((unsigned int)(sp - (sline+2)),sline+1);
        outbyt(symcs,sline+4);
        fputs(sline,outxsym_fp);
    }
    sp = NULL;
    return;
}

static int major_version,minor_version;

void outid( FILE *fp, int mode )
{
    char *s;
    VLDA_id *vldaid = (VLDA_id *)oline;

    major_version = VLDA_MAJOR;
    minor_version = VLDA_MINOR;
    switch (mode)
    {
    case OUTPUT_HEX:
    case OUTPUT_VLDA: {
            return;            /* nothing to do on abs output */
        }
    case OUTPUT_OL: {
            fprintf(fp,".id \"translator\" \"LLF %s\"\n",
                    REVISION);
            fprintf(fp,".id \"mod\" \"%s\"\n",
                    output_files[OUT_FN_ABS].fn_name_only);
            vid->md_len = 30;
#ifdef VMS
            vid->md_type = vid->md_class = 0;
#endif
            fprintf(fp,".id \"date\" %s\n",ascii_date);
            if (target != 0)
            {
                if (*target == '"')
                {
                    fprintf(fp,".id \"target\" %s\n",target);
                }
                else
                {
                    fprintf(fp,".id \"target\" \"%s\"\n",target);
                }
            }
            return;
        }
    case OUTPUT_OBJ: {
            vldaid->vid_rectyp = VLDA_ID;
            vldaid->vid_siz = sizeof(VLDA_id);
            vldaid->vid_maj = major_version;
            vldaid->vid_min = minor_version;
            vldaid->vid_symsiz = sizeof(VLDA_sym);
            vldaid->vid_segsiz = sizeof(VLDA_seg);
            vldaid->vid_image = sizeof(VLDA_id);
            if (error_count[2] > 255)
            {
                vldaid->vid_errors = 255;
            }
            else
            {
                vldaid->vid_errors = error_count[2];
            }
            if (error_count[0] > 255)
            {
                vldaid->vid_warns = 255;
            }
            else
            {
                vldaid->vid_warns = error_count[2];
            }
            s = oline + sizeof(VLDA_id);
            sprintf(s,"\"LLF %s\"",REVISION);
            while (*s++);
            vldaid->vid_target = s - oline;
            if (target != 0)
            {
                if (*target == '"')
                {
                    strcpy(s,target);
                }
                else
                {
                    *s++ = '"';
                    strcpy(s,target);
                    s += strlen(s);
                    *s++ = '"';
                    *s = 0;
                }
            }
            else
            {
                strcpy(s,"\"unknown\"");
            }
            s += strlen(s)+1;
            vldaid->vid_time = s - oline;
            strcpy(s,ascii_date);
            s += strlen(s)+1;
#ifndef VMS
            rsize = s-oline;
            fwrite((char *)&rsize,sizeof(int16_t),1,fp);
            even_odd = rsize&1;
#endif
            fwrite(oline,(int)(s-oline)+even_odd,1,fp);
            return;
        }
    }
}

static int outtstcommon(char *asc, int alen, EXP_stk *exp, char *olcmd, int vldacmd) {
    char *s;   
    switch (output_mode)
    {
    case OUTPUT_HEX:
    case OUTPUT_VLDA:
        break;
    case OUTPUT_OBJ: {
            VLDA_test *vt;
            flushobj();
            vt = (VLDA_test *)eline;
            vt->vtest_rectyp = vldacmd;
            vt->vtest_eoff = sizeof(VLDA_test);
            s = outexp(exp,eline+vt->vtest_eoff,0,0l,(char *)0,(FILE *)0);
            vt->vtest_soff = s-eline;
            strncpy(s,asc,alen);
            s += alen;
            *s++ = 0;
#ifndef VMS
            rsize = s-eline;
            fwrite(&rsize,sizeof(int16_t),1,outxabs_fp);
            even_odd = rsize&1;
#endif
            fwrite(eline,(int)(s-eline)+even_odd,1,outxabs_fp); /* write it */
            break;
        }
    case OUTPUT_OL: {
            strcpy(eline,olcmd);
            strncat(eline,asc,alen);
            strcat(eline,"\"");
            s = outexp_ol(exp,eline+strlen(eline));
            *s++ = '\n';
            *s++ = 0;
            fputs(eline,outxabs_fp);
            break;
        }
    }
    return 1;
}

int outtstexp(int typ, char *asc, int alen, EXP_stk *exp)
{
    static struct
    {
        int vlda;
        char *cmd;
    } tmp_opr[3] = {{VLDA_TEST,".test \""},{VLDA_BOFF,".bofftest \""},{VLDA_OOR,".oortest \""}};
    int ii;
    ii = 0;
    if (typ == TMP_BOFF) ii = 1;
    else if (typ == TMP_OOR) ii = 2;
    return outtstcommon(asc, alen, exp, tmp_opr[ii].cmd, tmp_opr[ii].vlda);
}

int out_dbgod( int mode, FILE *absfp, FN_struct *fnp )
{
    SS_struct *ss;
    SEG_spec_struct *segp;
    DBG_seclist *slp;
    if (mode == OUTPUT_OL)
    {
        fprintf(absfp,".dbgod \"%s\" \"%s\"\n",
                fnp->od_name,fnp->od_version?fnp->od_version:"");
        if ((slp=fnp->od_seclist_top) != 0)
        {
            do
            {
                ss = slp->segp;
                segp = ss->seg_spec;
                if (segp->seg_first != 0) ss = segp->seg_first;
                fprintf(absfp,".dbgsec %%%d %d %d %d\n",
                        ss->ss_ident,segp->seg_base,segp->seg_len,
                        segp->seg_offset);
            } while ((slp=slp->next) != 0);
        }
    }
    else if (mode == OUTPUT_OBJ || mode == OUTPUT_VLDA)
    {
        char *s;
        VLDA_dbgseg *dbsp;
        VLDA_dbgdfile *dbfp;
        dbfp = (VLDA_dbgdfile *)eline;
        dbfp->type = VLDA_DBGDFILE;
        dbfp->name = sizeof(VLDA_dbgdfile);
        s = eline+sizeof(VLDA_dbgdfile);
        strcpy(s,fnp->od_name);
        s += strlen(fnp->od_name)+1;
        dbfp->version = s-eline;
        strcpy(s,fnp->od_version?fnp->od_version:"");
        s += strlen(s)+1;
#ifndef VMS
        rsize = s-eline;
        fwrite(&rsize,sizeof(int16_t),1,absfp);
        even_odd = rsize&1;
#endif
        fwrite(eline,(int)(s-eline)+even_odd,1,absfp); /* write it */
        if ((slp=fnp->od_seclist_top) != 0)
        {
            dbsp = (VLDA_dbgseg *)eline;
            dbsp->type = VLDA_DBGSEG;
            do
            {
                int flg;
                ss = slp->segp;
                segp = ss->seg_spec;
                if (segp->seg_first != 0) ss = segp->seg_first;
                dbsp->base = segp->seg_base;
                dbsp->length = segp->seg_len;
                dbsp->offset = segp->seg_offset;
                dbsp->name = sizeof(VLDA_dbgseg);
                flg = VSEG_DEF;     /* VSEG_SYM=0, and VSEG_DEF=1 */
                if (ss->flg_based) flg |= VSEG_BASED;   /* may be based */
                if (ss->flg_abs | ss->flg_based) flg |= VSEG_ABS; /* may-be abs */
                if (ss->flg_ovr) flg |= VSEG_OVR; /* may be overlaid */
                if (ss->flg_noout) flg |= VSEG_NOOUT; /* if no output */
                if (segp->sflg_data) flg |= VSEG_DATA; /* segment is data/code */
                if (segp->sflg_ro) flg |= VSEG_RO; /* segment is read only */
                if (segp->seg_group == base_page_nam)
                { /* if in BSECT group */
                    flg |= VSEG_BPAGE;
                }
                dbsp->flags = flg;
                s = (char *)(dbsp+1);
                strcpy(s,ss->ss_string);
                s += strlen(s)+1;
                rsize = s-eline;
#ifndef VMS
                fwrite(&rsize,sizeof(int16_t),1,absfp);
                even_odd = rsize&1;
#endif
                fwrite(eline,(int)rsize+even_odd,1,absfp); /* write it */
            } while ((slp=slp->next) != 0);
        }
    }
    return 0;
}
