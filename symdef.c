/*
    symdef.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include <stdio.h>		/* get standard I/O stuff */
#include <ctype.h>		/* stuff for isprint */
#include <string.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get normal stuff */

#include "exproper.h"

struct ss_struct *last_seg_ref=0;
struct ss_struct *last_sym_ref=0;
#if 0
extern struct expr_token expr_stack[]; /* expression stack */
extern int expr_stack_ptr;
extern int32_t token_value;
extern void outsym_def();
extern void memcpy(char *, char *, int);
extern FILE *outxsym_fp;
#endif
struct sym_def
{
    int size;            /* size of area in chars */
    struct ss_struct *ptr;   /* pointer to symbol attached to expression */
};

static char *sym_top=0,*sym_next=0;
static struct sym_def *sym_pool=0;
static int sym_pool_size;
int32_t symdef_pool_used;

/************************************************************************
 * Write expression stack to symbol definition file
 */
int write_to_symdef( SS_struct *ptr )
/*
 * At entry:
 * At exit:
 */
{
    char *src,*dst;      /* mem pointers */
    int cnt, tsiz;       /* counter */
    struct sym_def *def_file;
    struct exp_stk *exp;

    cnt = expr_stack_ptr*sizeof(struct expr_token);
    if (sym_pool_size == 0)
    {
        sym_pool_size = MAX_TOKEN*8;
        symdef_pool_used += MAX_TOKEN*8;
        sym_pool = (struct sym_def *)MEM_alloc(sym_pool_size);
        if (sym_top == 0) sym_top = (char *)sym_pool;
    }
    def_file = sym_pool;
    tsiz = cnt + sizeof(struct exp_stk) + 2*sizeof(struct sym_def);
    if (sym_pool_size < tsiz)
    {
        def_file->size = TOKEN_LINK;
        if (tsiz < MAX_TOKEN*8) tsiz = MAX_TOKEN*8;
        sym_pool_size = tsiz;
        symdef_pool_used += tsiz;
        sym_pool = (struct sym_def *)MEM_alloc(sym_pool_size);
        def_file->ptr = (struct ss_struct *)sym_pool; /* reset the pointer */
        def_file = sym_pool;      /* point def_file to new area */
    }
    def_file->size = cnt+sizeof(struct sym_def)+sizeof(struct exp_stk);  /* reset the length field */
    def_file->ptr = ptr;         /* reset the ptr to symbol */
    if (ptr == 0) return TRUE;
    exp = (struct exp_stk *)(def_file+1);
    exp->len = expr_stack_ptr;       /* set the length of the expression */
    exp->ptr = (struct expr_token *)(exp+1);
    ptr->ss_exprs = exp;         /* point to expression stack */
    dst = (char *)exp->ptr;      /* point to area to load expression */
    src = (char *)expr_stack;        /* point to expression */
    memcpy(dst,src,cnt);
    sym_pool_size -= def_file->size;
    dst += cnt;
    sym_pool = (struct sym_def *)dst;    /* remember updated pointer */
    return TRUE;
}

/**********************************************************************
 * Read a bunch of data from sym_def file
 */
SS_struct *read_from_sym( void )
/*
 * At entry:
 * At exit:
 */
{
    register struct sym_def *sdf;
    register struct ss_struct *ptr;
    sdf = (struct sym_def *)sym_next;    /* get pointer to def_file struct */
    if (sdf->size == TOKEN_LINK)
    {
        sdf = (struct sym_def *)sdf->ptr;
        if (!qual_tbl[QUAL_REL].present)
        {
            if (MEM_free(sym_top))
            { /* give back the memory */
                sprintf(emsg,"Error free'ing %d bytes at %p from sym_pool",
                        MAX_TOKEN*8, (void *)sym_top);
                err_msg(MSG_WARN,emsg);
            }
            sym_top = (char *)sdf;
        }
        sym_next = (char *)sdf;
    }
    ptr = sdf->ptr;          /* point to symbol to be defined */
    sym_next += sdf->size;
    return(ptr);
}

static void rewind_sym( void )
{
    sym_next = sym_top;      /* point to beginning of area */
    return;
}

static int err_cnt;

int dump_expr( struct exp_stk *exp );

/********************************************************************
 * Evaluate expression.
 */
int ev_exp( struct exp_stk *eptr )
/*
 * At entry:
 *	eptr - points to exp_stk struct containg expression stats
 * At exit:
 *	returns TRUE if expression is ok and deposits result in token_value
 *	else returns FALSE.
 */
{
    int i,k;
    int oper;
    struct ss_struct *sym_ptr;
    struct expr_token *ctos,*tos,*sos;

    tos = ctos = eptr->ptr;
    k = eptr->len;
    for (; k > 0; --k,++ctos)
    {
        if (ctos != tos)
        {
            tos->expr_code = ctos->expr_code;
            tos->expr_value = ctos->expr_value;
			tos->ss_ptr = ctos->ss_ptr;
        }
        switch (tos->expr_code)
        {
        case EXPR_IDENT:
			{         /* 1 level of indirection */
                tos->expr_code = EXPR_SYM;      /* signal its now a sym */
				tos->ss_ptr = id_table[tos->ss_id]; /* * ((int)tos->expr_ptr + id_table); */
            }
            /* fall through to EXPR_SYM */
        case EXPR_SYM:
			{
                sym_ptr = tos->ss_ptr;        /* get pointer to symbol */
                if (!sym_ptr->flg_defined)
                {
                    if (!qual_tbl[QUAL_REL].present)
                    {
                        sprintf(emsg,
                                "Reference to undefined symbol {%s}",
                                sym_ptr->ss_string);
                        err_msg(MSG_WARN,emsg);
                        tos->expr_code = EXPR_VALUE;
                        tos->expr_value = 0;
                        ++err_cnt;
                    }
                    last_sym_ref = sym_ptr;      /* remember the last symbol */
                    ++tos;
                    break;               /* and exit */
                }                   /* -- defined */
                if (sym_ptr->flg_segment)
                {
                    last_seg_ref = sym_ptr;
                    if (qual_tbl[QUAL_REL].present)
                    {
                        if (sym_ptr->seg_spec->seg_first != 0)
                        {
                            tos->ss_ptr = sym_ptr->seg_spec->seg_first;
                        }
                    }
                    tos->expr_value += sym_ptr->ss_value;
                    if (!qual_tbl[QUAL_REL].present || sym_ptr->flg_abs)
						tos->expr_code = EXPR_VALUE;
                }
                else
                {                /* -+ segment/symbol */
                    last_sym_ref = sym_ptr;
                    if (sym_ptr->flg_exprs)
                    {
                        struct exp_stk *lnk;
                        lnk = sym_ptr->ss_exprs;
                        if (!qual_tbl[QUAL_REL].present || sym_ptr->flg_local)
                        {
                            if (ev_exp(lnk) == 0)
                            {    /* collapse expression */
                                sprintf(emsg,
                                        "Nested expression error in definition of symbol {%s}",
                                        sym_ptr->ss_string);
                                err_msg(MSG_WARN,emsg);
                                tos->expr_code = EXPR_VALUE;
                                tos->expr_value = 0;
                                ++err_cnt;
                                ++tos;
                                break;
                            }
                        }
                        if (lnk->len == 1 && lnk->ptr->expr_code == EXPR_VALUE)
                        {
                            tos->expr_code = EXPR_VALUE;
                            tos->expr_value += lnk->ptr->expr_value;
                        }
                        else
                        {
                            if (!qual_tbl[QUAL_REL].present)
                            {
                                sprintf(emsg,
                                        "Definition of symbol {%s} is unresolved",
                                        sym_ptr->ss_string);
                                err_msg(MSG_WARN,emsg);
                                tos->expr_code = EXPR_VALUE;
                                tos->expr_value = 0;
                                ++err_cnt;
                                ++tos;
                                break;
                            }
                            if (sym_ptr->flg_local)
                            {
                                if (lnk->len == 1)
                                {
                                    tos->expr_code = lnk->ptr->expr_code;
                                    tos->expr_value += lnk->ptr->expr_value;
                                    tos->ss_ptr = lnk->ptr->ss_ptr;
                                }
                                else
                                {
                                    tos->expr_code = EXPR_LINK;
                                    tos->ss_ptr = (SS_struct *)lnk;
                                }
                            }
                        }
                    }
                    else
                    {
                        tos->expr_code = EXPR_VALUE;
                        tos->expr_value += sym_ptr->ss_value;
                    }
                }               /* -- symbol/segment */
                ++tos;
                break;          /* exit switch */
            }
        case EXPR_VALUE:
			{
                ++tos;
                break;          /* exit switch */
            }
        case EXPR_B:
        case EXPR_L:
			{
                sym_ptr = tos->ss_ptr;        /* get pointer to segment */
                if (!sym_ptr->flg_segment)
                {
                    sprintf(emsg,"{%s} from file %s is not a segment",
                            sym_ptr->ss_string,sym_ptr->ss_fnd->fn_name_only);
                    err_msg(MSG_WARN,emsg);
                    ++err_cnt;
                    tos->expr_code = EXPR_VALUE;
                    tos->expr_value = 0;
                }
                else
                {
                    last_seg_ref = sym_ptr;
                    if (!qual_tbl[QUAL_REL].present)
                    {
                        tos->expr_code = EXPR_VALUE;
                        tos->expr_value = (tos->expr_code == EXPR_L) ? 
                                          sym_ptr->seg_spec->seg_len : sym_ptr->ss_value;
                    }
                }
                ++tos;
                break;          /* exit switch */
            }
        case EXPR_OPER:
			{
                oper = tos->expr_value; /* pick up the operator */  
                i = 2;          /* assume 2 items on stack */
                if (oper == EXPROPER_COM ||
                    oper == EXPROPER_NEG ||
                    oper == EXPROPER_SWAP ||
                    oper == ((EXPROPER_TST_NOT<<8) | EXPROPER_TST))
                {
                    i = 1;           /* operating on only 1 item */
                }
                if (eptr->len-1 < i)
                {  /* item(s) present? */
                    err_msg(MSG_WARN,"Expression stack underflow.");
                    ++err_cnt;
                }
                else
                {
                    struct expr_token *fos;
                    int rel_flg;
                    fos = tos - 1;       /* point to first item on stack */
                    sos = fos - 1;       /* point to second item on stack */
                    rel_flg = 0;
                    if (fos->expr_code == EXPR_VALUE) rel_flg = 1;
                    if (i != 2 || sos->expr_code == EXPR_VALUE) rel_flg |= 2;
                    if (!qual_tbl[QUAL_REL].present)
                    {
                        if (rel_flg != 3)
                        {
                            err_msg(MSG_WARN,"Non-absolute terms in expression");
                            ++err_cnt;
                            if ((rel_flg&1) == 0)
                            {
                                if (oper == EXPROPER_DIV || oper == EXPROPER_MOD)
                                {
                                    fos->expr_value = 1;
                                }
                                else
                                {
                                    fos->expr_value = 0;
                                }
                                fos->expr_code = EXPR_VALUE;
                            }
                            if ((rel_flg&2) == 0)
                            {
                                sos->expr_value = 0;
                                sos->expr_code = EXPR_VALUE;
                            }
                            rel_flg = 3;
                        }
                    }
                    if (rel_flg != 3)
                    {
#if 0
                        if (oper == EXPROPER_ADD)
                        {
                            if (rel_flg == 2)
                            {    /* value sym + */
                                sos->expr_value += fos->expr_value;
                                sos->expr_ptr = fos->expr_ptr;
                                sos->expr_code = fos->expr_code;
                                tos = fos;      /* eat fos and the oper */
                                eptr->len -= 2;     /* remove 2 terms from total */
                                break;
                            }
                            else if (rel_flg == 1)
                            { /* sym value + */
                                if (sos->expr_code == EXPR_SYM)
                                {
                                    sos->expr_value += fos->expr_value;
                                    tos = fos;       /* eat fos and the oper */
                                    eptr->len -= 2;  /* remove 2 terms from total */
                                    break;
                                }
                            }
                        }
                        else if (oper == EXPROPER_SUB)
                        {
                            if (rel_flg == 0)
                            {    /* sym sym - */
                                if (sos->expr_code == EXPR_SYM &&  /* if both syms */
                                    fos->expr_code == EXPR_SYM)
                                {
                                    sos->expr_value -= fos->expr_value;
                                    fos->expr_value = 0; /* clear old value */
                                    if (sos->expr_ptr == fos->expr_ptr)
                                    {
                                        sos->expr_code = EXPR_VALUE;
                                        tos = fos;    /* eat fos and the oper */
                                        eptr->len -= 2;   /* remove 2 terms from total */
                                        break;
                                    }
                                }
                            }
                            else if (rel_flg == 1)
                            { /* sym val - */
                                if (sos->expr_code == EXPR_SYM)
                                {
                                    sos->expr_value -= fos->expr_value;
                                    tos = fos;       /* eat fos and the oper */
                                    eptr->len -= 2;  /* remove 2 terms from total */
                                    break;
                                }
                            }
                            else
                            { /* val sym - */
                                if (fos->expr_code == EXPR_SYM)
                                {
                                    sos->expr_value -= fos->expr_value;
                                    fos->expr_value = 0; /* val sym - */
                                }
                            }
                        }
#endif
                        ++tos;            /* keep both terms and oper */
                        break;
                    }
					/* rel_flg == 3 which means both terms are absolute */
                    switch (oper&255)
                    {
                    case EXPROPER_ADD: {
                            sos->expr_value += fos->expr_value;
                            break;
                        }
                    case EXPROPER_SUB: {
                            sos->expr_value -= fos->expr_value;
                            break;
                        }
                    case EXPROPER_SHR: {
                            if (fos->expr_value > 31 || fos->expr_value < 0)
                            {
                                sos->expr_value = 0;
                            }
                            else
                            {
                                sos->expr_value =
                                (uint32_t)sos->expr_value >> fos->expr_value;
                            }
                            break;
                        }
                    case EXPROPER_SHL: {
                            if (fos->expr_value > 31 || fos->expr_value < 0)
                            {
                                sos->expr_value = 0;
                            }
                            else
                            {
                                sos->expr_value <<= fos->expr_value;
                            }
                            break;
                        }         
                    case EXPROPER_MUL: {  /* multiply */
                            sos->expr_value *= fos->expr_value;
                            break;
                        }
                    case EXPROPER_USD: {      /* unsigned divide */
                            if (fos->expr_value == 0)
                            {
                                err_msg(MSG_WARN,"Divide by zero in expression");
                                ++err_cnt;
                                sos->expr_value = 0;
                                break;
                            }
                            sos->expr_value = (uint32_t)sos->expr_value / (uint32_t)fos->expr_value;
                            break;
                        }
                    case EXPROPER_DIV: {      /* signed divide */
                            if (fos->expr_value == 0)
                            {
                                err_msg(MSG_WARN,"Divide by zero in expression");
                                ++err_cnt;
                                sos->expr_value = 0;
                                break;
                            }
                            sos->expr_value /= fos->expr_value;
                            break;
                        }
                    case EXPROPER_MOD: {      /* modulo */
                            if (fos->expr_value == 0)
                            {
                                err_msg(MSG_WARN,"Modulo by zero in expression");
                                ++err_cnt;
                                sos->expr_value = 0;
                                break;
                            }
                            sos->expr_value %= fos->expr_value;
                            break;
                        }
                    case EXPROPER_OR: {   /* logical or */
                            sos->expr_value |= fos->expr_value;
                            break;
                        }
                    case EXPROPER_AND: {  /* logical and */
                            sos->expr_value &= fos->expr_value;
                            break;
                        }
                    case EXPROPER_XOR: {  /* logical xor */
                            sos->expr_value ^= fos->expr_value;
                            break;
                        }
                    case EXPROPER_COM: {  /* logical not */
                            fos->expr_value = ~fos->expr_value;
                            break;
                        }
                    case EXPROPER_NEG: {  /* arithmetic negate */
                            fos->expr_value = -fos->expr_value;
                            break;
                        }
                    case EXPROPER_SWAP: { /* swap bytes */
                            fos->expr_value =  ((fos->expr_value >> 8)&0x00FF00FF) |
                                               ((fos->expr_value&0x00FF00FF) << 8);
                            break;
                        }
                    case EXPROPER_TST:
						{      /* relational */
                            oper >>= 8;
                            switch (oper)
                            {
                            case EXPROPER_TST_NOT:
								{
                                    if (fos->expr_value != 0)
                                    {
                                        fos->expr_value = 0;
                                    }
                                    else
                                    {
                                        fos->expr_value = 1;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_AND:
								{
                                    if (fos->expr_value != 0) fos->expr_value = 1;
                                    if (sos->expr_value != 0) sos->expr_value = 1;
                                    sos->expr_value &= fos->expr_value;
                                    break;
                                }
                            case EXPROPER_TST_OR:
								{
                                    if (fos->expr_value != 0) fos->expr_value = 1;
                                    if (sos->expr_value != 0) sos->expr_value = 1;
                                    sos->expr_value |= fos->expr_value;
                                    break;
                                }
                            case EXPROPER_TST_LT:
								{
                                    if (sos->expr_value < fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_EQ:
								{
                                    if (sos->expr_value == fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_GT:
								{
                                    if (sos->expr_value > fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_NE:
								{
                                    if (sos->expr_value != fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_LE:
								{
                                    if (sos->expr_value <= fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            case EXPROPER_TST_GE:
								{
                                    if (sos->expr_value >= fos->expr_value)
                                    {
                                        sos->expr_value = 1;
                                    }
                                    else
                                    {
                                        sos->expr_value = 0;
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    case EXPROPER_XCHG: {
                            if (fos->expr_code == EXPR_OPER || 
                                sos->expr_code == EXPR_OPER)
                            {
                                err_msg(MSG_WARN,"Object file error: Tried to XCHG two operators in expression");
                                ++err_cnt;
                                i = 0;      /* don't eat anything */
                            }
                            else
                            {
                                int32_t t;
                                SS_struct *tp;
                                t = fos->expr_value;
                                tp = fos->ss_ptr;
                                fos->expr_value = sos->expr_value;
                                fos->ss_ptr = sos->ss_ptr;
                                sos->expr_value = t;
                                sos->ss_ptr = tp;          
                                i = 1;      /* eat only the operator */
                            }
                            break;
                        }
                    case EXPROPER_PICK: {
                            int pick;
                            struct expr_token *pck;
                            pick = sos-eptr->ptr;
                            if (pick < fos->expr_value)
                            {
                                sprintf(emsg,"Object file error: Tried to PICK %d'th item on a stack of %d items",
                                        fos->expr_value,pick);
                                err_msg(MSG_WARN,emsg);
                                ++err_cnt;
                                fos->expr_value = 0;
                                fos->expr_code = EXPR_VALUE;
                            }
                            else
                            {
                                pck = sos - fos->expr_value;
                                if (pck->expr_code == EXPR_OPER)
                                {
                                    err_msg(MSG_WARN,"Object file error: Tried to PICK an operator term");
                                    ++err_cnt;
                                    fos->expr_code = EXPR_VALUE;
                                    fos->expr_value = 0;
                                }
                                else
                                {
                                    fos->expr_code = pck->expr_code;
                                    fos->expr_value = pck->expr_value;
                                    fos->ss_ptr = pck->ss_ptr;
                                }
                            }
                            i = 1;     /* eat only the operator */
                            break;
                        }
                    default: {
                            if (!isprint(oper)) oper = '.';
                            sprintf(emsg,"Undefined expression char %c (%o)",
                                    oper,(int)tos->expr_value);
                            err_msg(MSG_WARN,emsg);
                            ++err_cnt;
                            i = 0; /* don't eat any of it */
                        }     /* -- default	           */
                    }        /* -- switch on oper char  */
                    tos = tos - i + 1; /* remove items from stack */
                    eptr->len -= i;  /* remove from expression size */
                }           /* -- if (stack underflow) */
            }          /* -- case on type oper	   */
        }             /* -- switch on expr_type  */
    }                /* -- for all expr items   */
    if (!qual_tbl[QUAL_REL].present)
    {
        if (eptr->len != 1 || eptr->ptr->expr_code != EXPR_VALUE)
        {
            err_msg(MSG_WARN,"Expression stack not balanced.");
            ++err_cnt;
            eptr->len = 1;
            eptr->ptr->expr_code = EXPR_VALUE;
            eptr->ptr->expr_value = 0;
        }
    }
    if (err_cnt)
    {
        token_value = 0;      /* return a 0 */
        return FALSE;     /* and return with error */
    }
    token_value = eptr->ptr->expr_value;
    return TRUE;         /* and success */
}

/********************************************************************
 * Evaluate expression.
 */
int evaluate_expression(struct exp_stk *eptr)
/*
 * At entry:
 *	eptr - points to exp_stk containing expression stats
 * At exit:
 *	returns TRUE if expression is ok and deposits result in token_value
 *	else returns FALSE.
 */
{
    err_cnt = 0;
    ev_exp(eptr);
    expr_stack_ptr = eptr->len;
    if (err_cnt == 0) return TRUE;
    return FALSE;
}

/********************************************************************
 * Do symbol definitions.
 */
void symbol_definitions( void )
/*
 * Called after all segments have been positioned.
 * At entry:
 *	nothing special
 * At exit:
 *	symdef file processed
 */
{
    struct ss_struct *sym_ptr;   /* pointer to symbol to define */
    expr_stack_ptr = 0;      /* signal EOF */
    write_to_symdef((struct ss_struct *)0);  /* write an EOF */
    rewind_sym();        /* rewind the symbol file */
    while (1)
    {
        struct exp_stk *exp;
        int sts;
        sts = 1;          /* assume we're gonna make stb */
        sym_ptr = read_from_sym();
        if (sym_ptr == (struct ss_struct *)0)
        {
            if (!qual_tbl[QUAL_REL].present)
            {
                if (MEM_free(sym_top))
                { /* give back the memory */
                    sprintf(emsg,"Error free'ing %d bytes at %p from sym_pool",
                            MAX_TOKEN*8, (void *)sym_top);
                    err_msg(MSG_WARN,emsg);
                }
                sym_top = 0;
            }
            return;        /* return TRUE */
        }
        if ((exp=sym_ptr->ss_exprs) != 0 && exp->len != 0)
        {
            if (!evaluate_expression(exp))
            {
                sts = 0;        /* no stb */
                sym_ptr->flg_defined = 0;   /* say symbol isn't defined */
                sprintf(emsg,"\twhile defining symbol {%s%s%s\n",
                        sym_ptr->ss_string,"} from file ",
                        sym_ptr->ss_fnd->fn_name_only);
                err_msg(MSG_CONT,emsg);
            }
            else
            {
                if (!qual_tbl[QUAL_REL].present)
                {
                    sym_ptr->flg_exprs = 0;  /* reset the expression flag */
                    sym_ptr->flg_abs = 1;    /* signal symbol is resolved */
                    sym_ptr->ss_value = token_value;
                }
            }
        }
        if (sts && !sym_ptr->flg_local && outxsym_fp != 0 && !sym_ptr->ss_fnd->fn_nostb)
        {
            outsym_def(sym_ptr,output_mode);   /* output the definition expression */
        }
    }                /* -- while	*/
}               /* -- sym_def	*/

int dump_expr( EXP_stk *exp )
{
    int len;
    EXPR_token *ex;
    SS_struct *sym_ptr;
    len = exp->len;
    ex = exp->ptr;
    printf("Expression stack with %d terms\n",exp->len);
    for (;len > 0;++ex,--len)
    {
        switch (ex->expr_code)
        {
        case EXPR_IDENT:
			sym_ptr = id_table[ex->ss_id]; /* * ((int)ex->expr_ptr + id_table); */
			if (sym_ptr != 0 && sym_ptr->ss_string != 0)
			{
				printf("\tEXPR_IDENT %d. Value %d. Points to symbol %s\n",
					   ex->ss_id,ex->expr_value,sym_ptr->ss_string);
			}
			else
			{
				printf("\tEXPR_IDENT %d. Value %d. NO SYMBOL FOR IT\n",
					   ex->ss_id,ex->expr_value);
			}
			continue;
        case EXPR_SYM:
			sym_ptr = ex->ss_ptr; /* ex->expr_ptr; */     /* get pointer to symbol */
			if (sym_ptr != 0 && sym_ptr->ss_string != 0)
			{
				printf("\tEXPR_SYM. Ptr %p, Value %d. Points to symbol %s\n",
					   (void *)sym_ptr, ex->expr_value, sym_ptr->ss_string);
				if (sym_ptr->flg_segment)
					printf("\t\tIs a segment.\n");
				if (sym_ptr->flg_defined)
				{
					if (sym_ptr->flg_exprs)
					{
						printf("\t\tSymbol is defined via an expression:\n****\n");
						dump_expr(sym_ptr->ss_exprs);
						printf("****\n");
					}
					else
					{
						printf("\t\tSymbol is defined with value of: %d\n",
							   sym_ptr->ss_value);
					}
				}
			}
			else
			{
				printf("\tEXPR_SYM: expr=%p. \"%s\"=%d. NON-EXISTANT SYMBOL\n",
					   (void *)sym_ptr, sym_ptr ? sym_ptr->ss_string : NULL, ex->expr_value);
			}
			continue;
        case EXPR_VALUE:
			printf("\tEXPR_VALUE. Value %d\n",ex->expr_value);
			continue;
        case EXPR_B:
        case EXPR_L:
			sym_ptr = ex->ss_ptr;     /* get pointer to segment */
			if (sym_ptr == 0 || !sym_ptr->flg_segment)
			{
				printf("\tEXPR_L or EXPR_B. ptr does not point to segment\n");
				continue;
			}
			else
			{
				printf("\tEXPR_L or EXPR_B. Value %d. Points to %s\n",
					   ex->expr_value,sym_ptr->ss_string);
			}
			continue;
        case EXPR_OPER:
			if ((char)ex->expr_value == EXPROPER_TST)
			{
				printf("\tEXPR_OPER. Value is %c%c (%03o %03o)\n",
					   (char)ex->expr_value,(int)((ex->expr_value>>8)&0xff),
					   (char)ex->expr_value,(int)((ex->expr_value>>8)&0xff));
			}
			else
			{
				printf("\tEXPR_OPER. Value is %c (%03o)\n",
					   (char)ex->expr_value,(char)ex->expr_value);
			}
			continue;
        default:
			printf("\tUnknown expression code: %X, value = %d, id=%d, ptr = %p\n",
				   ex->expr_code,
				   ex->expr_value,
				   ex->ss_id,
				   (void *)ex->ss_ptr);
			continue;
        }             /* -- switch on expr_type  */
    }                /* -- for all expr items   */
    return TRUE;         /* and success */
}

