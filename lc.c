/*
    lc.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include <stdio.h>		/* get standard I/O definitions */
#include <ctype.h>
#include <string.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"

#ifdef _toupper
    #undef _toupper
    #undef _tolower
#endif
#define _toupper(c)	((c) >= 'a' && (c) <= 'z' ? (c) & 0x5F:(c))
#define _tolower(c)	((c) >= 'A' && (c) <= 'Z' ? (c) | 0x20:(c))

/*****************************************************************
 * LC - process locator control file
 *
 * LC file is an ASCII file containing one or more of the following
 * constructs:
 *
 *	token ( argument [,][...] );
 *
 * where white space is optional except it will delimit arguments.
 * The comma (,) and semi-colon (;) are considered white space when
 * appearing before any other non-white space otherwise they delimit
 * arguments. The semi-colon (;) doesn't delimit arguments during
 * "file_name" retrieval mode.
 *
 * The 'token' may be one of the following strings:
 *	DECLARE, LOCATE, MEMORY, RESERVE, SEGSIZE,
 *	START, GROUP, FILE
 * The argument(s) may be either symbol, segment or group names or
 * decimal (default) or hexidecimal constants (indicated by # prefixed
 * to the number).
 *
 */

static char upc_token[32];  /* local uppercase token */
static char *tmp_grp_name=0;    /* pointer to temporry group */
static int tmp_grp_name_size;   /* size of temporary area */
static int tmp_grp_number;  /* temporary group number */
static int skipAllDeclare, skipAllLocate, skipAllReserve;

/******************************************************************
 * Display bad string.
 */
static void bad_token(char *ptr, char *msg)
/*
 * At entry:
 * 	ptr - points to character in error in inp_str
 *	msg - pointer to string that is the error message to display
 * At exit:
 *	inp_str is displayed, a line terminated with '^' is displayed
 *	under inp_str to indicate error position and the error message
 * 	is displayed under that. 
 */
{
    char *s=err_str, *is=inp_str, *os;
    FILE *save_fp;
    int lim = err_str_size;
    if (ptr-inp_str > lim)
    {
        is += (ptr-inp_str) - lim;
    }
    os = is;
    while (is < ptr) *s++ = (*is++ != '\t') ? ' ' : '\t';
    *s++ = '^';
    *s   = '\0';
    if (strlen(os) > lim*2)
    {
        sprintf(emsg,"%s\n%-*.*s\n%s",msg, lim*2, lim*2, os, err_str);
    }
    else
    {
        sprintf(emsg,"%s\n%s%s",msg, os, err_str);
    }
    save_fp = map_fp;
    map_fp = 0;
    err_msg(MSG_ERROR,emsg); /* this goes to stderr */
    if ((map_fp = save_fp) != 0)
    {   /* and if there's a map file */
#if defined(VMS) || defined(MS_DOS)
        sprintf(emsg,"%s\n%%LLF-E-ERROR, %s\n",err_str,msg);
#else
        sprintf(emsg,"%s\n%%llf-e-error, %s\n",err_str,msg);
#endif
        puts_map(emsg,0);     /* write shortened message to map */
    }
}

/******************************************************************
 * Get a token from the input
 */
int lc_get_token(int errcode, char *string, int nb)
/*
 * At entry:
 *	errcode - .true. if EOF is ok to get here
 *		  .false. if EOF is not so hot
 *	string  - error message to output if read error (EOF)
 *	nb	- .true. if token is string of any non-blank chars
 * At exit:
 *	returns EOF if input error else
 *	*token_pool - points to token just picked up from input
 *	token_type = 0 if token is nfg
 *		   = 1      "      character
 *		   = 2      "      hexidecimal number
 *		   = 3      "	   decimal number
 *		   = 4	    "	   string
 *		   = 5	    "	   maybe octal or hex
 *		   = 6	    "	   internal mode
 *		   = 7	    "	   token value is percent
 */
#define LC_TOK_NFG	0
#define LC_TOK_CHAR	1
#define LC_TOK_HEX	2
#define LC_TOK_DEC	3
#define LC_TOK_STR	4
#define LC_TOK_OCT	5
#define LC_TOK_OCTNXT	6
#define LC_TOK_PRCNT	7
{
    int j=0,comment=0;
    char *old_tokp=token_pool,*lc_s=token_pool,*upc=upc_token;
    int c;
    token_type = token_value = LC_TOK_NFG;
    while (1)
    {
        if (j == 0) tkn_ptr = inp_ptr;
        c = get_c();          /* get a char from input */
        if (old_tokp != token_pool)
        { /* token pool moved */
            int siz;
            register char *src=old_tokp,*dst=token_pool;
            siz = lc_s - old_tokp;
            for (; siz > 0 ; --siz) *dst++ = *src++;
            old_tokp = token_pool;
            lc_s = dst;
        }
        if (c==EOF)
        {
            bad_token(tkn_ptr,"Premature end-of-file");
            return EOF;
        }
        if (j == 0)
        {
            if (isspace(c)) continue;  /* skip over leading spaces */
            if (comment)
            {
                if (c == '*')
                {
                    if (*inp_ptr == '/')
                    {
                        comment = 0;
                        inp_ptr++;
                        continue;
                    }
                }
            }
            if (c == '!') c = 0;       /* pretend we hit EOL */
            if (c == '/')
            {
                if (*inp_ptr == '*')
                {
                    inp_ptr++;
                    comment++;
                    continue;
                }
            }
            if (c == '-')
            {        /* double '-' is comment delimiter */
                if (*inp_ptr == '-') c = 0; /* pretend we hit eol */
            }
            if (c == ',') continue;    /* , is a leading space */
            if ((nb == 0) && (c == ';')) continue; /* ; is a leading space */
            if (c == 0)
            {
                if (get_text()==EOF)
                {  /* nl is white space */
                    if (errcode)
                    {       /* ok to exit with EOF */
                        bad_token(tkn_ptr,string); /* pre-mature eof */
                    }
                    return EOF;
                }
                if (old_tokp != token_pool)
                {   /* token pool moved */
                    int siz;
                    register char *src=old_tokp,*dst=token_pool;
                    siz = lc_s - old_tokp;
                    for (; siz > 0 ; --siz) *dst++ = *src++;
                    old_tokp = token_pool;
                    lc_s = dst;
                }
                continue;
            }
            else
            {
                *lc_s++ = c;        /* record token */
                if ((c == '(' ) || (c == ')' ) || ((c == ':') && (nb == 0)))
                {
                    *lc_s = 0;       /* null terminator */
                    token_value++;       /* length is 1 */
                    token_type = LC_TOK_CHAR; /* token type is char */
                    return 1;        /* found token */
                }
                if (nb == 0)
                {
                    if ( c == '#' )
                    {
                        token_type=j=LC_TOK_HEX; /* j = 2 if hex number */
                        continue;     /* get next digit */
                    }
                    if (isdigit(c))
                    {
                        token_type=LC_TOK_DEC; /* token type = number */
                        if (c == '0')
                        {   /* maybe octal (0nnnn) or hex (0xnnn) */
                            j = LC_TOK_OCT;    /* octal (or maybe hex, find out later */
                        }
                        else
                        {
                            j = LC_TOK_DEC;    /* j = 3 if decimal number */
                            token_value = c - '0'; /* de-ascify the first digit */
                        }
                        continue;
                    }
                }
                token_value++;      /* count the first character */
                token_type=j=LC_TOK_STR;    /* j = 4 token type is string */
                *upc++ = _toupper(c);   /* record uppercase token */
                continue;
            }
        }
        else
        {
            if ((c == 0) || isspace(c) || (c==',') || ((nb == 0) && (c==';')) )
            {
                *lc_s = 0;      /* terminate string */
                *upc = 0;       /* null terminate the uppercase string */
                return 1;       /* done */
            }
            switch (j)
            {
            case LC_TOK_OCT: {  /* maybe octal or hex */
                    c = _toupper(c); /* upcase the char */
                    if (c == 'X')
                    {
                        token_type=j=LC_TOK_HEX; /* change it to hex */
                        continue; /* eat the char and continue */
                    }
                    j = LC_TOK_OCTNXT; /* switch to mode 6 next time */
                }           /* and fall through to mode 6 */
            case LC_TOK_OCTNXT: {
                    if (!isdigit(c))
                    {   /* if not a digit */
                        inp_ptr--;        /* backup one char */
                        return 1;     /* return success */
                    }
                    c = c-'0';   /* de-ascify the character */
                    if (c > 7)
                    {
                        bad_token(tkn_ptr,"Leading 0 implies octal radix");
                        c = 7;    /* max it out */
                    }
                    token_value = token_value*8 + c;
                    continue;    /* keep going */
                }
            case LC_TOK_HEX: {
                    if (!isxdigit(c))
                    {
                        inp_ptr--; return 1;
                    }
                    c = _toupper(c);
                    if (isdigit(c)) token_value = (token_value << 4) + (c-'0');
                    else token_value = (token_value << 4) + c - 'A' + 10;
                    continue;
                }
            case LC_TOK_DEC: {
                    if (!isdigit(c))
                    {
                        if (c == '%')
                        {
                            token_type = LC_TOK_PRCNT; /* set percent mode */
                        }
                        else
                        {
                            inp_ptr--;     /* else backup */
                        }
                        return 1;     /* return success */
                    }
                    token_value = token_value*10 + (c-'0');
                    continue;
                }
            case LC_TOK_STR: {
                    if ((c == '(') || (c == ')') || ((nb == 0) && (c == ':')))
                    {
                        inp_ptr--;
                        *lc_s = 0;    /* null terminate string */
                        *upc = 0; /* null terminate the uppercase string */
                        return 1;
                    }
                    *lc_s++ = c;
                    token_value++;
                    if (token_value < sizeof(upc_token)-1)
                        *upc++ = _toupper(c);  /* record uppercase token */
                    continue;
                }       /* case */
            }      /* switch */
        }         /* if (!j) */
    }            /* while (1) */
}           /* lc_get_token */

/******************************************************************
 * chk_token - checks for valid token
 */
int chk_token(int get_flg, int typ, int value, char *msg)
/*
 * At entry:
 *	get_flg - .ne. if to get a token before checking
 *	typ - token type to verify against
 *	value - value to match type on
 *	msg - string pointer to message if token nfg
 * At exit:
 *	the next token has been fetched from the input file
 *	returns TRUE/FALSE/EOL or EOF
 *
 * Note that typ 2 and 3 are equivalent. If a type 1 (char) of ")"
 * is detected, EOL will be returned and if not expecting type 4 (string)
 * the error message is output. If EOF is detected while fetching the
 * next token, EOF is returned. TRUE is returned only if token_type
 * matches typ otherwise (unless EOL or EOF) FALSE is returned.
 */

{
    if (get_flg != 0)
    {
        if (lc_get_token(1,msg,0) == EOF ) return EOF;   /* always return EOF */
    }
    switch (token_type)
    {
    case LC_TOK_CHAR: {
            if (*token_pool == ')')
            {          /* stop on ")"? */
                if (typ != LC_TOK_STR) bad_token(tkn_ptr,msg);   /* message too if not 4 */
                return EOL;                 /* EOL on ")" */
            }
            if (*token_pool == value ) return TRUE;    /* 'tis good */
            bad_token(tkn_ptr,msg);
            return FALSE;
        }
    case LC_TOK_HEX:
    case LC_TOK_DEC: {
            if (typ == LC_TOK_HEX || typ == LC_TOK_DEC ) return TRUE;   /* ok */
        }
    }
    if (typ == token_type) return TRUE;      /* happiness prevails */
    bad_token(tkn_ptr,msg);          /* message, token nfg */
    return FALSE;                /* tell caller too */
}

#define CHK_TOKEN(gt,type,value,string) \
   {\
   if ((err_typ=chk_token(gt,type,value,string)) == EOF) return EOF; \
   if (err_typ == EOL) return EOL;\
   if (!err_typ) continue;\
   }

/********************************************************************
 * DECLARE ( symbol : address [,...]);
 *
 *	The declare command-
 *		defines an existing unresolved external symbol to
 *		have the specified value.
 *
 *		has no effect on the output object module if the
 *		declared symbol is not referenced in the input object.
 *	I.e.
 *		DECLARE (x : 000);
 *		DECLARE (var1 : 1, var2 : #FFEE);
 *		DECLARE (var : TIME);	--assigns the int32_t value returned from
 *			the C RTL function time() to var.
 */

int lc_declare( char **opt )
{
    int err_typ;
    struct ss_struct *sym_ptr;
	if ( !skipAllDeclare && qual_tbl[QUAL_REL].present )
	{
		sprintf(emsg,"Declare commands are ignored when -relative mode selected");
		err_msg(MSG_WARN,emsg);
		skipAllDeclare = 1;
	}
	while ( 1 )
    {
        CHK_TOKEN(1,LC_TOK_STR,0,"Expected symbol name here");
		if ( (sym_ptr = sym_lookup(token_pool, token_value, 0)) == 0 )
		{
			if ( !skipAllDeclare && !qual_tbl[QUAL_QUIET].present )
			{
				sprintf(emsg,"DECLARED symbol {%s} not present in object code",
						token_pool);
				err_msg(MSG_WARN,emsg);
			}
		}
        lc_get_token(0,"%LLF- Premature EOF",0);
        if ((err_typ=chk_token(0,LC_TOK_CHAR,':',"Expected ':' here, assumed")) == EOF)
			return EOF;
        if (err_typ == EOL)
			return EOL;
        if (err_typ)
			lc_get_token(0,"%LLF- Premature EOF",0);
        if (token_type == LC_TOK_STR)
        {
            if (strcmp(upc_token,"TIME") == 0)
            {
                token_value = unix_time;
            }
        }
        else
        {
            CHK_TOKEN(0,LC_TOK_HEX,0,"Expected an absolute number or string TIME here");
        }
        if ( !skipAllDeclare && sym_ptr )
        {
            if (sym_ptr->flg_segment)
            {
                sprintf(emsg,"DECLARED symbol {%s} is a segment name",
                        sym_ptr->ss_string);
                err_msg(MSG_WARN,emsg);
            }
            else
            {
				int doIt=1;
				if ( sym_ptr->flg_defined )
				{
					int complex=0;
					
					if ( sym_ptr->flg_exprs && sym_ptr->ss_exprs && (sym_ptr->ss_exprs->len != 1) )
						complex = 1;
					if ( (sym_ptr->ss_value != token_value) || complex )
					{
						if ( complex )
							sprintf(emsg, "DECLARED symbol {%s} attempted to re-define from <complex> to absolute 0x%08X. Ignored DECLARE.",
									sym_ptr->ss_string, token_value);
						else
							sprintf(emsg, "DECLARED symbol {%s} attempted to re-define from 0x%08X to 0x%08X. Ignored DECLARE.",
									sym_ptr->ss_string, sym_ptr->ss_value, token_value );
						err_msg(MSG_WARN,emsg);
						doIt = 0;
					}
				}
				if ( doIt )
				{
					sym_ptr->ss_value = token_value;    /* set the new value */
					sym_ptr->flg_defined = sym_ptr->flg_exprs = 1; /* set flags */
					expr_stack[0].expr_code = EXPR_VALUE;
					expr_stack[0].expr_value = token_value;
					expr_stack_ptr = 1; /* one item on the stack */
					write_to_symdef(sym_ptr);
				}
            }          /* -- if */
        }             /* -- if */
    }                /* -- while */
}               /* -- lc_declare */

/***********************************************************************
 * LOCATE ( id [,id...] : address-option; [,...]);
 *
 *	id - segment_name or group_name
 *
 * The LOCATE command will locate a segment or group of segments
 * starting at a specific address. If multiple id's are specified,
 * a dummy group consisting of those segments is constructed and
 * the dummy group is located.
 *
 *	id - a segment name or group name (only 1 group name allowed)
 *	address_option - 
 *		address (absolute number)
 *		address TO address
 *		address OUTPUT address
 *		address NOOUTPUT
 *		FIT
 *		STABLE
 *		NOOUTPUT
 *		NAME name
 */
enum states
{
    LOCATE_SEG_GRP,      /* a segment or group name follows */
    LOCATE_SEG_TO,       /* a "TO" or segment address */
    LOCATE_EATIT,        /* eat to EOF or ')' */
    LOCATE_SEG_EOL,      /* a segment name or EOL */
    LOCATE_ABS_BASE,     /* next thing must be an absolute BASE addr */
    LOCATE_ABS_TO,       /* next thing must be an absolute TO addr */
    LOCATE_ABS_OUT,      /* next thing must be an absolute OUTPUT addr */
    LOCATE_GRP_NAME,     /* next thing must be the group name */
    LOCATE_SEP,          /* next thing must be a ':' */
    LOCATE_DUMMY         /* put segment into dummy group */
};

int check_4_lckeyword(int *state, struct ss_struct *grp_nam)
{
    if (strcmp(upc_token,"TO") == 0)
    {
        *state = LOCATE_ABS_TO;   /* next thing must be absolute number */
        return 1;
    }
    if (strcmp(upc_token,"OUTPUT") == 0)
    {
        *state = LOCATE_ABS_OUT;  /* next thing must be absolute number */
        return 1;
    }
    if (strcmp(upc_token,"NOOUTPUT") == 0)
    {
        grp_nam->flg_noout = 1;
        *state = LOCATE_SEG_EOL; /* next thing may be a ')' or segment name */
        return 1;
    }
    if (strcmp(upc_token,"FIT") == 0)
    {
        grp_nam->seg_spec->sflg_fit = 1;
        grp_nam->seg_spec->sflg_stable = 0;       /* fit implies not stable */
        return 1;     /* next could be ')', "TO", "OUTPUT", etc. */
    }
    if (strcmp(upc_token,"STABLE") == 0)
    {
        grp_nam->seg_spec->sflg_stable = 1;
        return 1;     /* next could be ')', "TO", "OUTPUT", etc. */
    }
    if (strcmp(upc_token,"NAME") == 0)
    {
        *state = LOCATE_GRP_NAME; /* next thing must be a name */
        return 1;
    }
    return 0;
}

int lc_locate( char **opt )
{
    int state = skipAllLocate ? LOCATE_EATIT : LOCATE_SEG_GRP;
    int err = 0;
    struct ss_struct *sym_ptr,*grp_nam=0;
    struct grp_struct *grp_ptr=0;
    struct seg_spec_struct *seg_ptr;
    int info_save;
    int salign = 0;
    int dalign = 0;
    info_save = info_enable;
    info_enable = 1;
	if ( !skipAllLocate && qual_tbl[QUAL_REL].present )
	{
		snprintf(emsg,sizeof(emsg)-1,"Locate commands are ignored when -relative mode selected");
		err_msg(MSG_WARN,emsg);
		state = LOCATE_EATIT;
		skipAllLocate = 1;
	}
	while ( 1 )
    {
        if (lc_get_token(0,"%LLF- Premature EOF",0) == EOF )
        {
            info_enable = info_save;
            return EOF;        /* always return EOF */
        }
        switch (state)
        {
        default: {     /* unused state(s) */
                sprintf(emsg,"Fatal internal error. LOCATE state = %d",state);
                err_msg(MSG_ERROR,emsg);
                ++err;
                state = LOCATE_EATIT;
            }
        case LOCATE_EATIT: {
                if ((token_type == LC_TOK_CHAR) && (*token_pool == ')'))
                {
                    info_enable = info_save;
                    return EOL;
                }
                continue;
            }      
        case LOCATE_SEG_TO: {      /* maybe a "TO" or segment name */
                if (token_type == LC_TOK_STR)
                {
                    if (check_4_lckeyword(&state,grp_nam)) continue;
                }
                state = LOCATE_SEG_EOL; /* it's a segment name, could have ")" next */
            }
        case LOCATE_SEG_EOL:       /* maybe a ")" or segment name */
            if ((token_type == LC_TOK_CHAR) && (*token_pool == ')'))
            {
                info_enable = info_save;
                return EOL;
            }
        case LOCATE_SEG_GRP: {     /* must be a segment or group name */
                if (token_type != LC_TOK_STR)
                {
                    if (err++ == 0) bad_token(tkn_ptr,"Expected a segment or group name here");
                    state = LOCATE_EATIT;
                    continue;
                }
                *(token_pool+token_value) = ' ';
                ++token_value;
                *(token_pool+token_value) = 0;
                if ((grp_nam = sym_ptr = sym_lookup(token_pool,token_value,0)) == 0)
                {
                    sprintf(emsg,"Segment \"%s\" is not present in object code",token_pool);
                    err_msg(MSG_INFO,emsg);
                    ++err;
                    continue;
                }
                if (sym_ptr->flg_group)
                {
                    state = LOCATE_SEP;  /* expect a ':' next */
                    continue;
                }
                if (!sym_ptr->flg_segment)
                {
                    sprintf(emsg,"\"%s\" is a global symbol name not a segment name",token_pool);
                    err_msg(MSG_WARN,emsg);
                    ++err;
                    continue;
                }
                if (sym_ptr->seg_spec->seg_salign > salign) salign = sym_ptr->seg_spec->seg_salign;
                if (sym_ptr->seg_spec->seg_dalign > dalign) dalign = sym_ptr->seg_spec->seg_dalign;
                grp_nam = get_symbol_block(1);  /* get a symbol block */
                if (tmp_grp_name_size < 13)
                {
                    tmp_grp_name_size = 15*13;   /* get some memory */
                    tmp_grp_name = MEM_alloc(tmp_grp_name_size);
                    grp_pool_used += tmp_grp_name_size;
                }
                sprintf(tmp_grp_name,"(noname_%03d) ",tmp_grp_number);
                grp_nam->ss_string = tmp_grp_name;
                grp_nam->ss_fnd = current_fnd;  /* say who made this group */
                state = strlen(tmp_grp_name)+1;
                tmp_grp_name += state;
                tmp_grp_name_size -= state;
                tmp_grp_number++;
                grp_ptr = get_grp_ptr(grp_nam,(int32_t)0,(int32_t)0); /* get a group list */
                add_to_group(sym_ptr, grp_nam, grp_ptr); /* add first seg to list */
                state = LOCATE_DUMMY;       /* add to dummy group list */
                continue;
            }
        case LOCATE_DUMMY: {       /* add to dummy group */
                if (token_type == LC_TOK_STR)
                {
                    *(token_pool+token_value) = ' ';
                    ++token_value;
                    *(token_pool+token_value) = 0;
                    if ((sym_ptr = sym_lookup(token_pool,token_value,0)) != 0)
                    {
                        if (sym_ptr->flg_segment)
                        {
                            add_to_group(sym_ptr,grp_nam,grp_ptr);
                        }
                        else
                        {
                            sprintf(emsg,"\"%s\" is a global symbol name not a segment name",token_pool);
                            err_msg(MSG_WARN,emsg);
                            ++err;
                        }
                    }
                    else
                    {
                        sprintf(emsg,"Segment \"%s\" is not present in object code",token_pool);
                        err_msg(MSG_INFO,emsg);
                        ++err;
                    }
                    continue;
                }
                if ((token_type == LC_TOK_CHAR) && (*token_pool == ':'))
                {
                    state = LOCATE_ABS_BASE; /* next thing must be an address */
                    continue;
                }
                bad_token(tkn_ptr,"Expected a ':' or segment name here");
                ++err;
                state = LOCATE_EATIT;
            }
        case LOCATE_SEP: {     /* must be a ":" */
                if ((token_type != LC_TOK_CHAR) || (*token_pool != ':'))
                {
                    bad_token(tkn_ptr,"Expected a ':' here");
                    ++err;
                    state = LOCATE_EATIT;
                    continue;
                }
                state = LOCATE_ABS_BASE;    /* next thing's gotta be an address */
                continue;
            }
        case LOCATE_GRP_NAME: {        /* group name expected here */
                if (token_type != LC_TOK_STR)
                { /* didn't get a name */
                    bad_token(tkn_ptr,"Expected a group name here");
                    ++err;
                }
                else
                {
                    *(token_pool+token_value) = ' ';
                    ++token_value;
                    *(token_pool+token_value) = 0;
                    grp_nam->ss_string = token_pool; /* point to name */
                    token_pool += token_value+1; /* keep the name */
                }
                state = LOCATE_SEG_TO;      /* look for base, TO, OUTPUT or NAME */
                continue;
            }
        case LOCATE_ABS_OUT:           /* offset address is expected here */
            if (token_type == LC_TOK_STR)
            { /* this must be a section name */
                SS_struct *sym;
                struct seg_spec_struct *seg;
                *(token_pool+token_value) = ' ';
                ++token_value;
                *(token_pool+token_value) = 0;
                sym = sym_lookup(token_pool, ++token_value, 2);
                if (new_symbol == 5)
                {   /* symbol added and is a duplicate */
                    sym->ss_string = first_symbol->ss_string;
                }
                else if (new_symbol == 1 || new_symbol == 3)
                { /* added or added and first in hash */
                    sym->ss_fnd = 0;  /* actually pointed to by ourself */
                    token_pool += token_value;
                    token_pool_size -= token_value;
                }
                else
                {
                    bad_token(tkn_ptr, "Unable to create new section");
                    ++err;
                    state = LOCATE_SEG_TO;
                    continue;
                }
                if ((seg=sym->seg_spec) == 0)
                {  /* if this is not a segment yet */
                    sym->flg_defined = 1;     /* flag it as defined */
                    seg = get_seg_spec_mem(sym);  /* make it a segment */
                    if (new_symbol != 5)
                    {
                        seg->seg_salign = salign;
                        seg->seg_dalign = dalign;
                        insert_intogroup(group_list_top, sym, group_list_default);
                    }
                    else
                    {
                        seg->seg_salign = first_symbol->seg_spec->seg_salign;
                        seg->seg_dalign = first_symbol->seg_spec->seg_dalign;
                    }
                }
                else
                {
                    SS_struct **osym;
                    osym = find_seg_in_group(sym, group_list_top->grp_top);
                    if (!osym)
                    {
                        bad_token(tkn_ptr, "Cannot place group in an already located segment");
                        ++err;
                    }
                }
                seg_ptr = grp_nam->seg_spec;
                seg_ptr->seg_reloffset = sym;
                seg_ptr->sflg_reloffset = 1;
                state = LOCATE_SEG_TO;
                continue;           
            }
        case LOCATE_ABS_TO:    /* high limit address is expected here */
        case LOCATE_ABS_BASE:
            {                   /* base address is expected here */
                sym_ptr = NULL;
                if ((token_type != LC_TOK_HEX) && (token_type != LC_TOK_DEC))
                {
                    if (token_type == LC_TOK_STR)
                    {
                        if (check_4_lckeyword(&state,grp_nam))
                            continue;
                        sym_ptr = sym_lookup(token_pool,token_value,0);
                        if ( sym_ptr && sym_ptr->flg_defined )
                        {
                            int lerr = 1;
                            if ( sym_ptr->flg_exprs )
                            {
                                lerr =  evaluate_expression( sym_ptr->ss_exprs );
                            }
                            else
                            {
                                token_value = sym_ptr->ss_value;
                            }
/*                            printf( "Found symbol %s. flags: grp %d, seg: %d, def: %d, expr: %d, val: %08lX, tv: %08lX\n",
                                    token_pool, sym_ptr->flg_group, sym_ptr->flg_segment,
                                    sym_ptr->flg_defined, sym_ptr->flg_exprs,
                                    sym_ptr->ss_value, token_value );
*/
                            if ( !lerr || sym_ptr->flg_group || sym_ptr->flg_segment
                                 || !sym_ptr->flg_defined )
                            {
                                sym_ptr = NULL;
                            }
                        }
                    }
                    if ( !sym_ptr )
                    {
                        bad_token(tkn_ptr,"Expected an absolute value, keyword or defined global symbol name here");
                        ++err;
                        state = LOCATE_SEG_TO;
                        continue;
                    }
                }
                seg_ptr = grp_nam->seg_spec;
                switch (state)
                {
                case LOCATE_ABS_BASE: {
                        seg_ptr->seg_base = token_value;
                        grp_nam->flg_based = 1;   /* signal that group is based */
                        seg_ptr->sflg_stable = 1; /* based implies stable */
                        break;
                    }
                case LOCATE_ABS_TO: {
                        seg_ptr->seg_maxlen = token_value - seg_ptr->seg_base + 1;
                        break;
                    }
                case LOCATE_ABS_OUT: {
                        seg_ptr->seg_offset = token_value - seg_ptr->seg_base ;
                        break;
                    }
                }           /* --switch	*/
                state = LOCATE_SEG_TO;  /* next thing may be a ")" or segment name */
                continue;
            }          /* --case	*/
        }             /* --switch	*/
    }                /* --while	*/
}               /* --lc_locate_common	*/

/****************************************************************************
 * RESERVE (address-option ...)
 */
int lc_reserve( char **opt )
/*
 * Reserves a memory location or range of locations such that no segments
 * will be placed in the region.
 *
 *	address-option -
 *		absolute number
 *		BEFORE number 
 *		AFTER  number
 *		number TO number
 *		
 */
{
    int state=0,cn=0;
    uint32_t begin=0;
	if ( !skipAllReserve && qual_tbl[QUAL_REL].present )
	{
		snprintf(emsg,sizeof(emsg)-1,"Reserve commands are ignored when -relative mode selected");
		err_msg(MSG_WARN,emsg);
		skipAllReserve = 1;
	}
	while ( 1 )
    {
        if (lc_get_token(0,"%LLF- Premature EOF",0) == EOF )
            return EOF; /* always return EOF */
        if (token_type == LC_TOK_STR)
			cn = token_value;
        switch (state)
        {
        case 1: {      /* may be an address or "TO" */
                if (token_type == LC_TOK_STR && strncmp(upc_token,"TO",cn) == 0)
                {
                    state = 2;   /* next thing must be an 'end' address */
                    continue;
                }
				if ( !skipAllReserve )
					add_to_reserve(begin, 1l);   /* stick in reserved mem location */
                state = 0;
				/* intentionally fall through to state 0 */
            }
        case 0: {      /* maybe a ")", number or string */
                if ((token_type == LC_TOK_CHAR) && (*token_pool == ')')) return EOL;
                if (token_type == LC_TOK_HEX || token_type == LC_TOK_DEC)
                {
                    begin = token_value;
                    state = 1;   /* next thing might be a TO or another address */
                    continue;
                }
                if (token_type == LC_TOK_STR )
                {
                    if (strncmp(upc_token,"BEFORE",cn) == 0)
                    {
                        begin = 0;    /* all addresses to "end" */
                        state = 2;    /* next thing must be "end" address */
                        continue;
                    }
                    if (strncmp(upc_token,"AFTER",cn) == 0)
                    {
                        state = 3;    /* next thing must be a "begin" address */
                        continue;
                    }
                }
                bad_token(tkn_ptr,"Expected one of 'BEFORE', 'AFTER' or address");
                state = 0;
                continue;
            }
        case 2: {      /* this one must be an "end" address */
                if (token_type == LC_TOK_HEX || token_type == LC_TOK_DEC)
                {
                    if (token_value < begin)
                    {
                        bad_token(tkn_ptr,
                                  "END address must be greater than START address");
                        token_value = begin;
                    }
					if ( !skipAllReserve )
						add_to_reserve(begin, (uint32_t)token_value - begin + 1l);
                    state = 0;           /* back to normal */
                    continue;
                }
                bad_token(tkn_ptr,"Expected an END address");
                state = 0;
                continue;
            }
        case 3: {          /* this one must be a begin address */
                if (token_type == LC_TOK_HEX || token_type == LC_TOK_DEC)
                {
                    if (!token_value)
                    {
                        bad_token(tkn_ptr,
                                  "Ah come on, you can't reserve all of memory!");
                    }
					if ( !skipAllReserve )
						add_to_reserve((uint32_t)token_value,
									   (uint32_t)(-token_value));
                    state = 0;           /* back to normal */
                    continue;
                }
                bad_token(tkn_ptr,"Expected a BEGIN address");
                state = 0;
                continue;
            }          /* --case	*/
        }             /* --switch	*/
    }                /* --while	*/
}               /* --lc_reserve	*/

static struct fn_struct **last_inp;

/*************************************************************************
 * lc_common_file() - picks up filenames and adds them to then end of fn list
 */
int lc_common_file(int lib, char **opt)
/*
 * At entry:
 *	lib = 0 if files are not library, 1 = files are library
 *	opt = ptr to array of char ptrs pointing to option(s)
 * At exit:
 *	returns EOL or EOF
 */
{
    int cmdopt=0;
    struct fn_struct *next_inp;
    while (opt != (char **)0 && *opt != (char *)0)
    {
        if (strncmp("NOSYMBOL",*opt,strlen(*opt)) == 0) cmdopt |= 1;
        else if (strncmp("NOSTB",*opt,strlen(*opt)) == 0) cmdopt |= 2;
        else
        {
            sprintf(emsg,"Unknown command option: %s",*opt);
            bad_token((char *)0,emsg);
        }
        ++opt;
    }
    while (1)
    {
        if (lc_get_token(0,"%LLF- Premature EOF",1) == EOF )
            return EOF;        /* always return EOF */
        if ((token_type == LC_TOK_CHAR) && (*token_pool == ')')) return EOL;
        next_inp = get_fn_struct();   /* get a new next */
        next_inp->r_length = token_value;
#ifdef VMS
        next_inp->d_length = token_value + 1;
#endif
        next_inp->fn_buff = token_pool;   /* point to filename */
        next_inp->fn_nosym = (cmdopt&1) != 0; /* signal quiet symbols */
        next_inp->fn_nostb = (cmdopt&2) != 0; /* signal quiet symbols */
        token_pool += token_value+1;  /* keep the string */
        token_pool_size -= token_value+1; /* take from total */
        if ((next_inp->fn_library = lib) != 0)
        {  /* signal its a library file */
            add_defs(next_inp->fn_buff,def_lib_ptr,(char **)0,0,&next_inp->fn_nam);
        }
        next_inp->fn_present = 1;
        if (first_inp == 0)
        {     /* first file in the list? */
            first_inp = next_inp;      /* record the first structure addr */
        }
        next_inp->fn_next = *last_inp;    /* insert filename after us */
        *last_inp = next_inp;
        last_inp = &next_inp->fn_next;
    }
}

/*************************************************************************
 * lc_file() - picks up filenames and adds them to then end of fn list
 */
int lc_file( char **opt)
/*
 * At entry:
 *	no requirements
 * At exit:
 *	returns EOL or EOF
 */
{
    return(lc_common_file(0,opt));
}

/*************************************************************************
 * lc_library() - picks up filenames and adds them to then end of fn list
 */
int lc_library(char **opt)
/*
 * At entry:
 *	no requirements
 * At exit:
 *	returns EOL or EOF
 */
{
    return(lc_common_file(1,opt));
}

/*************************************************************************
 * lc_eatit() - justs eats text until EOF or EOL
 */
int lc_eatit( char **opt )
/*
 * At entry:
 *	no requirements
 * At exit:
 *	returns EOL or EOF
 */
{
    while (1)
    {
        if (lc_get_token(1,"Premature EOF",0) == EOF )
			return EOF;   /* always return EOF */
        if (token_type == LC_TOK_CHAR && *token_pool == ')')
			return EOL;
    }
}

static int noSuchFunction(char **opt)
{
	bad_token(tkn_ptr,"Sorry, function not implemented yet");
	lc_eatit(opt);
	return 0;
}

static int lc_memory(char **opt)
{
	return noSuchFunction(opt);
}

static int lc_segsize(char **opt)
{
	return noSuchFunction(opt);
}

static int lc_start(char **opt)
{
	return noSuchFunction(opt);
}

static int lc_group(char **opt)
{
	return noSuchFunction(opt);
}

struct
{
    int (*lc_rout)(char **opt);
    char *lc_str;
    uint8_t lc_len;
    unsigned int lc_flag:1;
    unsigned int lc_opt:1;
}  lc_dispatch[] = {
    {lc_file,   "FILE",   4,0,1},
    {lc_library,"LIBRARY",7,0,1},
    {lc_declare,"DECLARE",7,1,0},
    {lc_locate, "LOCATE", 6,1,0},
    {lc_memory, "MEMORY", 6,1,0},
    {lc_reserve,"RESERVE",7,1,0},
    {lc_segsize,"SEGSIZE",7,1,0},
    {lc_start,  "START",  5,1,0},
    {lc_group  ,"GROUP",  5,1,0},
    {0,0,0,0,0}
};

#define MAX_OPTS 2		/* maximum # of command options */

/*******************************************************************
 * Main entry:
 *	This procedure is called by LLF after all .ol files have
 *	been read (including all librarys).
 */

int lc( void )
{
    int i,j,k;
    int (*f)(char **opt);
    char **opt;
    char *opt_array[MAX_OPTS+1];     /* ptrs to option strings */
    opt_array[MAX_OPTS] = (char *)0; /* always 0 */
    last_inp = &current_fnd->fn_next;    /* so FILE and LIBRARY commands work properly */
    j = 0;
    while (1)
    {
        if (!j++)
        {
            if (get_text() == EOF)
				return EOF; /* prime the pump */
        }
        if (lc_get_token(0,"",0) == EOF )
			return EOF;
        if (token_type != LC_TOK_STR)
        {
            if (lc_pass != 0)
				bad_token(tkn_ptr,"Unrecognised construct");
            j = 0;             /* break to next line */
            continue;
        }
        for (i=0;lc_dispatch[i].lc_len;i++)
        {
            if (token_value > lc_dispatch[i].lc_len) continue;
            k = strncmp(upc_token,lc_dispatch[i].lc_str,(int)token_value);
            if (k != 0)
				continue;
            if (lc_pass == lc_dispatch[i].lc_flag)
                f = lc_dispatch[i].lc_rout; 
            else
                f = lc_eatit;
            if (lc_get_token(1,"Premature EOF.",0) == EOF)
				return EOF;
            opt = NULL;
            if (lc_dispatch[i].lc_opt)
            {
                int cnt;
                opt = opt_array;
                for (cnt= 0; cnt < MAX_OPTS; ++cnt)
                {
                    if (token_type == LC_TOK_STR)
                    {
                        opt_array[cnt] = token_pool;  /* remember we have an option */
                        token_pool += token_value+1;
                        token_pool_size -= token_value+1;
                        if (lc_get_token(1,"Premature EOF.",0) == EOF) return EOF;
                    }
                    else
                    {
                        opt_array[cnt] = (char *)0;
                    }
                }
            }
            if (token_type != LC_TOK_CHAR || *token_pool != '(' )
            {
                if (lc_pass == 1)
                {
                    bad_token(tkn_ptr,"Expected a '(' here; one is assumed.");
                }
                inp_ptr = tkn_ptr;      /* re-process last token */
            }
            if (lc_dispatch[i].lc_opt)
                (*f)(opt);          /* do the function */
            else
                (*f)(NULL);
            j = 0;             /* break to next line */
            break;
        }         /* -- for	*/
        if (j != 0)
        { /* normal line break? */
            if (lc_pass != 0)
				bad_token(tkn_ptr,"Unrecognized command");
            j = 0;     /* break to next line */
        }
    }            /* -- while   	*/
}           /* -- lc      	*/
