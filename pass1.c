/*
    pass1.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include <ctype.h>		/* get standard string type macros */
#include <string.h>
#include <errno.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get our standard stuff */
#include "exproper.h"		/* expression operators */
#include "add_defs.h"

#undef NULL
#define NULL 0

#if 0
extern int32_t id_table_size;
extern FN_struct *xfer_fnd;
extern struct fn_struct *get_fn_pool();
#endif

char *target;           /* pointer to target string */
char emsg[EMSG_SIZE];         /* space to build error messages */
char *inp_str;          /* place to hold input text */
int  inp_str_size;
char *inp_ptr=0;        /* pointer to next place to get token */
char *tkn_ptr=0;        /* pointer to start of token */
int32_t token_value;       /* value of converted token */
int  token_type;        /* token type */
char token_end;         /* terminator char for string tokens */
char token_curchr;      /* current token character being processed */
int token_minus;        /* a minus as been detected */
char *token_pool=0;     /* pointer to free token memory */
int token_pool_size;        /* size of remaining free token memory */
struct expr_token expr_stack[32]; /* expression stack */
int expr_stack_ptr;     /* size of current expression stack */
int cmd_code;           /* command code */
struct seg_spec_struct *seg_spec_pool=0;  /* pointer to free segment space */
DBG_seclist *dbg_sec_pool=0;
int seg_spec_size;      /* size of segment pool */
int haveLiteralPool;

/******************************************************************
 * Pick up memory for special segment block storage
 */
SEG_spec_struct *get_seg_spec_mem( SS_struct *seg_ptr)
/* At entry:
 * 	seg_ptr - pointer to segment block to extend
 * At exit:
 *	seg_spec_pool is updated to point to new free space
 *	seg_spec_size is updated to reflect the size of seg_spec_pool
 */
{
    if (--seg_spec_size < 0)
    {
        int t = sizeof(struct seg_spec_struct)*32;
        sym_pool_used += t;
        seg_spec_pool = (struct seg_spec_struct *)MEM_alloc(t);
        dbg_sec_pool = (DBG_seclist *)MEM_alloc(t);
        seg_spec_size = 31;
    }
    seg_ptr->flg_segment = 1;
    if (!seg_ptr->flg_group)
    {
        if (current_fnd->od_seclist_curr != 0)
        {
            current_fnd->od_seclist_curr->next = dbg_sec_pool;
        }
        current_fnd->od_seclist_curr = dbg_sec_pool++;
        if (current_fnd->od_seclist_top == 0)
        {
            current_fnd->od_seclist_top = current_fnd->od_seclist_curr;
        }
        current_fnd->od_seclist_curr->segp = seg_ptr;
    }
    return(seg_ptr->seg_spec = seg_spec_pool++);
}
/******************************************************************/

int32_t record_count;

/******************************************************************
 * Pick up a line of text from the input file
 */
int get_text( void )
/*
 * At entry:
 * 	No requirements. 
 * At exit:
 * 	inp_str is loaded with next input line.
 *	if EOF detected, routine returns with EOF else
 *	routine returns TRUE
 */
{
    char *s;
    int len;
    if (token_pool_size <= MAX_TOKEN)
    {
        token_pool_size = MAX_TOKEN*8;     /* get a bunch of memory */
        token_pool = MEM_alloc(token_pool_size); /* pick up some garbage area */
        misc_pool_used += token_pool_size;
    }
    s = inp_str;
    if (!fgets(inp_ptr=s,inp_str_size-3,current_fnd->fn_file))
    {
        *inp_ptr = '\0';
        if (fgetc(current_fnd->fn_file) == EOF) return(EOF);
        sprintf(emsg,"Error reading input from \"%s\": %s",
                current_fnd->fn_buff,err2str(errno));
        err_msg(MSG_FATAL,emsg);
        EXIT_FALSE;
    }
    len = strlen(s);
    s += len;
    while (*(s-1) != '\n')
    {
        inp_str_size += inp_str_size/2;
        inp_ptr = inp_str = (char *)MEM_realloc(inp_str, inp_str_size);
        s = inp_str+len;
        if (!fgets(s, inp_str_size-len-3, current_fnd->fn_file))
        {
            *s++ = '\\';
            *s++ = '\n';
            *s = 0;
            break;
        }
        len += strlen(s);
        s += len;
    }
    record_count++;
    if (option_input) puts_map(inp_str,-1);
    return(1);
}
/******************************************************************/

/*****************************************************************
 * Get a character from inp_str.
 */
int get_c( void )
/*
 * At entry:
 *	inp_str is assumed to contain the source line
 *	inp_ptr is assumed to point to the next source character
 * At exit:
 *	routine returns with next source character. inp_ptr is
 *	updated to reflect change. A new source line may have been
 *	read in if the previous line ended with '\'. If EOF is
 *	detected, routine returns with EOF instead of char.
 */
{
    char c;
    do
    {
        if ((c= *inp_ptr++) != '\\')
        {
            if (c) return c;
            inp_ptr--;
            return(c);
        }
        if (*inp_ptr != '\n') return(c);
    } while (get_text() != EOF);
    return(EOF);
}

/******************************************************************/

char err_str[60];
int err_str_size = sizeof(err_str);

/******************************************************************
 * Display bad string.
 */
void bad_token( char *ptr, char *msg )
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
    int lim = sizeof(err_str);
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
    err_msg(MSG_ERROR,emsg);
}
/******************************************************************/
#define DEBUG_DO_TOKEN_ID (0)

#if 0 || DEBUG_DO_TOKEN_ID
typedef struct
{
	int tknType;
	const char *name;
} TokenTypeNames_t;
static const TokenTypeNames_t TokenNames[] = 
{
	{ TOKEN_cmd,	"TOKEN_cmd" },
	{ TOKEN_ID,		"TOKEN_ID" },
	{ TOKEN_ID_num,	"TOKEN_ID_num" },
	{ TOKEN_const,	"TOKEN_const" },
	{ TOKEN_oper,	"TOKEN_oper" },
	{ TOKEN_expr_tag,"TOKEN_expr_tag" },
	{ TOKEN_bins,	"TOKEN_bins" },
	{ TOKEN_ascs,	"TOKEN_ascs" },
	{ TOKEN_LB,		"TOKEN_LB" },
	{ TOKEN_sep,	"TOKEN_sep" },
	{ TOKEN_repf,	"TOKEN_repf" },
	{ TOKEN_uc,		"TOKEN_uc" },
	{ TOKEN_LINK,	"TOKEN_LINK" },
	{ TOKEN_EOF,	"TOKEN_EOF" }
};

static const char *token_type_to_ascii(int token_type)
{
	int ii;
	static char undefined[32];
	for (ii=0; ii < n_elts(TokenNames);++ii)
	{
		if ( TokenNames[ii].tknType == token_type )
			return TokenNames[ii].name;
	}
	snprintf(undefined,sizeof(undefined),"Undefined %d", token_type);
	return undefined;
}
#endif

/******************************************************************
 * Get the first char of the next token
 */
int get_token_c( void )
/*
 * At entry:
 *	inp_ptr - points to next source char in inp_str
 *	inp_str - contains the source line
 * At exit:
 *	returns EOL if end of source line
 *	  "     EOF if end of file detected
 *	  "	special token eater code if success
 *	token_type set to type of token detected
 */
#define TOKEN_E_nanstng 0	/* non-a/n terminated string */
#define TOKEN_E_tstrng	1	/* string terminated with token_end char */
#define TOKEN_E_idnum	2	/* decimal id number */
#define TOKEN_E_hexnum	3	/* hex number */
#define TOKEN_E_hexstng	4	/* hex string */
#define TOKEN_E_char	5	/* single char (operator, etc) */
#define TOKEN_E_decnum	6	/* decimal number */
{
    uint8_t c;
    while (1)
    {
        token_value = token_minus = 0;
        if (debug > 3)
        {
            while (isspace(c= get_c()));
        }
        else
        {
            while (isspace(c= *inp_ptr++));
        }
        tkn_ptr = inp_ptr;    /* point to beginning of token */
        token_curchr = c;     /* say what the token is */
        switch (c)
        {
        case NULL:
        case '\n': {
                --inp_ptr;      /* don't advance over EOL char */
                token_type = EOL;
                return(EOL);
            }
        case 255: {
                token_type = EOF;
                return(EOF);
            }
        case '.': { token_type = TOKEN_cmd; return(TOKEN_E_nanstng);}
        case '{': { token_type = TOKEN_ID; token_end = '}';
                return(TOKEN_E_tstrng);}
        case '%': { token_type = TOKEN_ID_num; return(TOKEN_E_idnum);}
        case '#': { token_type = TOKEN_const; return(TOKEN_E_hexnum);}
        case '\'': { token_type = TOKEN_bins; token_end = '\'';
                return(TOKEN_E_hexstng);}
        case 'A':
        case 'a':
        case 'O':
        case 'o':
        case 'S':
        case 's':
        case '+':      /* add */
        case '<':      /* shift left */
        case '>':      /* shift right */
        case '*':      /* multiply */
        case '/':      /* divide */
        case '@':      /* modulo */
        case '|':      /* logical or */
        case '^':      /* logical exclusive or */
        case '~':      /* not (1's compliment) */
        case '&':      /* logical and */
        case '_':      /* negate (2's compliment) */
        case '?':      /* unsigned divide */
        case '!':      /* relational (next char is type) */
        case '$':      /* dup */
        case '`':      /* xchg */
        case '=': {        /* swap bytes of a 2 byte word */
                token_type = TOKEN_oper; return(TOKEN_E_char);
            }
        case 'u':
        case 'c': { token_type = TOKEN_uc; return(TOKEN_E_char);}
        case ':': { token_type = TOKEN_expr_tag; return(TOKEN_E_nanstng);}
        case '\"': { token_type = TOKEN_ascs; token_end = '\"';
                return(TOKEN_E_tstrng);}
        case 'L':
        case 'B': { token_type = TOKEN_LB; return(TOKEN_E_char);}
        case '\\': {
                if (*inp_ptr == '\n')
                { /* next thing a \n? */
                    if (get_text() == EOF) return(EOF); /* get another line */
                    continue;        /* and keep going */
                }
            }
        case ',': { token_type = TOKEN_sep; return(TOKEN_E_char);}
        case '-': {
                if (!isdigit(*inp_ptr))
                {
                    token_type = TOKEN_oper;
                    return(TOKEN_E_char);
                }
                token_minus++;
                if (debug > 3)
                {
                    c = get_c();
                }
                else
                {
                    c = *inp_ptr++;
                }
                token_curchr = c;
            }
        default: {
                if (isdigit(c))
                {
					token_type = TOKEN_const;
					if ( c == '0' && (*inp_ptr = 'X' || *inp_ptr == 'x') )
					{
						++inp_ptr;	/* eat the 'x' */
						return TOKEN_E_hexnum;
					}
                    return TOKEN_E_decnum;
                }
                break;
            }
        }
        bad_token(tkn_ptr,"Unrecognised token character");
        token_type = EOL;
        return(EOL);
    }
}

/******************************************************************
 * get_token - gets the next term from the input file.
 */
int get_token( int part1)
/*
 * At entry:
 *	part1 - .lt. 0 if to do get_token_c first
 *	   else part1 is the special token eater code
 * 	current_fnd - pointer to file descriptor structure
 *	inp_ptr - points to next source byte
 *	inp_str - contains the source line
 * At exit:
 *	The token is copied into the token pool and the routine
 *	returns a code according to the type of token processed.
 *	The variable token_value is set to the length of string
 *	type tokens or the binary value of constant type tokens.
 * 	TOKEN_cmd	- command term (.xxx)
 * 	TOKEN_ID	- identifier string ({string})
 * 	TOKEN_ID_num	- identifier number (%xxx)
 * 	TOKEN_hexs	- hex text string ('xxxx..')
 * 	TOKEN_oper	- arithimetic operator (+,-,<,>,...)
 * 	TOKEN_expr_tag - expression tag (:c)
 * 	TOKEN_ascs	- ascii text string ("text")
 * 	TOKEN_LB	- Length/Base expression operators
 * 	TOKEN_sep	- seperator character (comma or backslash)
 * 	EOL - newline
 * 	EOF - end of file
 */
{
    int j,c;
    char *s=token_pool,*hexs;
    if ((j=part1) < 0)
    {
        if ((j = get_token_c()) < 0 ) return(j); /* gotta be good */
    }
    hexs = inp_ptr;
    switch (j)
    {
    case TOKEN_E_nanstng: {   /* eat everything to non-alpha (.xxx) */
            if (debug > 3)
            {
                while ((c= get_c()) != EOF)
                {
                    if (isalpha(c))
                    {
                        if (++token_value < MAX_TOKEN-1) *s++ = c;
                        else
                        {
                            bad_token(tkn_ptr,"Token too long");
                            break;
                        }
                    }
                    else
                    {
                        if (c) --inp_ptr;
                        break;
                    }
                }
            }
            else
            {
                while ((*s++ = c = *inp_ptr++) != 0)
                {
                    if (!isalpha(c))
                    {
                        --inp_ptr;
                        break;
                    }
                }
                --s;
                token_value = inp_ptr - hexs;
            }
            break;
        }
    case TOKEN_E_tstrng: {    /* eat everything til \n or term char ("xxx") */
            if (debug > 3)
            {
                while ((int)(c= get_c()) != EOF )
                {
                    if (c != '\n' && c != token_end )
                    {
                        if (++token_value < MAX_TOKEN-1) *s++ = c;
                        else
                        {
                            bad_token(tkn_ptr,"Token too long");
                            break;
                        }
                    }
                    else break;
                }
            }
            else
            {
                while (( *s++ = c = *inp_ptr++) != token_end) ;
                --s;
                token_value = inp_ptr - hexs - 1;
            }
            break;
        }
    case TOKEN_E_idnum: { /* get a decimal number string (%nnn) */
            if (debug > 3)
            {
                while ((int)(c=get_c()) != EOF)
                {
                    if (isdigit(c))
                    {
                        token_value = token_value*10 + (c-'0');
                    }
                    else
                    {
                        --inp_ptr; /* put the character back */
                        if (token_value > 65535)
                        {
                            bad_token(tkn_ptr,"ID value greater than 65,535");
                            token_value &= 65535;
                        }
                        break;
                    }
                }
            }
            else
            {
                while ((c = *inp_ptr++) != 0)
                {
                    if (!isdigit(c)) break;
                    token_value = token_value*10 + (c-'0');
                }
                --inp_ptr;
            }
            if (current_fnd->fn_max_id < token_value)
                current_fnd->fn_max_id = token_value;
            break;
        }
    case TOKEN_E_hexnum: {    /* get hex number string (#xxx) */
            while ((c = *inp_ptr++) != 0)
            {
                if (!isxdigit(c)) break;
                if (!isdigit(c)) c += 9;
                token_value = (token_value << 4) + (c & 0x0F);
            }
            --inp_ptr;
            break;
        }
    case TOKEN_E_hexstng: {   /* get hex string ('xx...') */
            if (debug > 3)
            {
                token_value = 0;
                while (1)
                {
                    c = get_c();
                    if (!isxdigit(c))
                    {
                        if (token_value&1)
                        {
                            bad_token(inp_ptr,"Odd number of hex digits in string");
                        }
                        if (c != token_end)
                        {
                            bad_token(inp_ptr,"Illegal hex digit");
                            --inp_ptr;
                        }
                        break;
                    }
                    if (!isdigit(c)) c += 9;
                    if (token_value&1)
                    {
                        *s++ |= c & 0x0F;
                    }
                    else
                    {
                        *s = c << 4;
                    }
                    ++token_value;
                }
            }
            else
            {
                while ((c = *inp_ptr++) != token_end)
                {
                    if (!isdigit(c)) c += 9;
                    *s = c << 4;
                    c = *inp_ptr++;
                    if (!isdigit(c)) c += 9;
                    *s++ |= c & 0x0F;
                }
                token_value = (inp_ptr - hexs - 1);
                if (!c) --inp_ptr;
            }
            token_value /= 2;  /* compute actual number of bytes */
            break;
        }
    case TOKEN_E_char: {  /* single character (+-,\<>) */
            *s++ = token_curchr;
            token_value++;
            break;
        }
    case TOKEN_E_decnum: {    /* decimal constant (nnn) */
            c = token_curchr;
            if (debug > 3)
            {
                do
                {
                    if (isdigit(c))
                    {
                        token_value = token_value*10 + (c-'0');
                    }
                    else
                    {
                        if (c) --inp_ptr;
                        break;
                    }
                } while ((int)(c= get_c()) != EOF);
            }
            else
            {
                token_value = token_value*10 + (c-'0');
                while ((c = *inp_ptr++) != 0)
                {
                    if (!isdigit(c)) break;
                    token_value = token_value*10 + (c-'0');
                }
                --inp_ptr;
            }
            if (token_minus) token_value = -token_value;
            break;
        }             /* -- case */
    }                /* -- switch */
    *s++ = '\0';         /* terminate string */
    return(token_type);     /* return with token type */
}

/* static uint32_t sym_idTableIdx; */

/******************************************************************
 * Process ID type token. This token has the syntax of:
 *	[{string}]%nnn
 * where the "[]" indicates an optional parameter. There may or may not
 * be whitespace seperating the paramters.
 */
SS_struct *do_token_id( int flag, uint32_t *tableIndexP )
/*
 * At entry:
 *	flag -  0 if not to insert into symbol table.
 *		1 if to insert into symbol table if symbol not found
 *		2 if to insert into symbol table even if symbol found
 * 	token_type must be set to the type of token most recently
 *	processed. If token_type is TOKEN_ID, then the string is made
 *	permanent in the token pool, the string (symbol) is added to
 *	the hashed symbol table and a symbol structure is picked up
 *	from free pool. The next token is then obtained. If token_type
 *	is TOKEN_ID_num, then the symbol block pointer is picked up
 *	from the hash table and returned to the caller. If token_type
 *	is neither TOKEN_ID nor TOKEN_ID_num, then an error message
 * 	is generated indicating that an ID was expected.
 * At exit:
 *	returns 0 if error else returns pointer to symbol block
 */
{
    SS_struct *sym_ptr;
	uint32_t tableIndex;
	
#if DEBUG_DO_TOKEN_ID
	printf("do_token_id(): flag=%d, token_type=%d(%s), token_value=%d, token_pool='%s', tableIndexP=%p\n",
		   flag, token_type, token_type_to_ascii(token_type), token_value, token_pool, (void *)tableIndexP );
#endif
   if (token_type == TOKEN_ID_num && !flag)
    {
        tableIndex = id_table_base+token_value;
        sym_ptr = id_table[tableIndex];
        if (sym_ptr != 0 && sym_ptr->ss_fnd == current_fnd)
        {
            if (sym_ptr->flg_defined)
            {
                bad_token(tkn_ptr,"Multiple definition of a symbol");
            }
            if (sym_ptr->ss_string != 0)
            {
                sym_delete(sym_ptr);    /* remove it from symbol chain */
            }
        }
        else
        {
            id_table[tableIndex] = sym_ptr = get_symbol_block(1);
        }
        new_symbol = 1;
		if ( tableIndexP )
			*tableIndexP = tableIndex;
        return(sym_ptr);
    }
    if (token_type == TOKEN_ID)
    {
#if DEBUG_DO_TOKEN_ID
      printf("do_token_id(): Found TOKEN_ID for symbol %s. flag=%d, token_value = %d\n",
          token_pool,flag,token_value);
#endif
        if (flag == 2 && token_value > 0)
        {   /* if it's a segment name */
            *(token_pool+token_value) = ' ';   /* add a trailing space */
            token_value += 1;          /* to tell it from a like */
            *(token_pool+token_value) = 0;     /* named symbol */
        }
        if (flag && token_value)
        {
            sym_ptr = sym_lookup(token_pool,++token_value,flag);
        }
        else
        {
            sym_ptr = get_symbol_block(1);
            new_symbol = 1;
        }
#if DEBUG_DO_TOKEN_ID
      printf("\tsym_ptr=%p, new_symbol = %d\n", (void *)sym_ptr, new_symbol);
#endif
        switch (new_symbol)
        {
        case 5: {      /* symbol added and is a duplicate (segment) */
                sym_ptr->ss_string = first_symbol->ss_string;
                token_value = 0;    /* pretend we have no string */
            }          /* fall though to case 1,3 and 0 */
        case 1:        /* symbol is added */
        case 3: {      /* symbol is added, first in the hash table */
                sym_ptr->ss_fnd = current_fnd;
                if (token_value > 1)
                {
                    token_pool += token_value; /* update the free mem pointer */
                    token_pool_size -= token_value;  /* and size */
                }
            }
        case 0:  {     /* no symbol added */
                if (qual_tbl[QUAL_CROSS].present)
					do_xref_symbol(sym_ptr,0);
                break;      /* done */
            }
        default: {
                sprintf(emsg,
                        "Internal error. {new_symbol} invalid (%d)\n\t%s%s%s%s",
                        new_symbol,"while processing {",sym_ptr->ss_string,
                        "} in file ",current_fnd->fn_buff);
                err_msg(MSG_FATAL,emsg);
            }
        }
        get_token(-1);
        if (token_type == TOKEN_ID_num)
        {
            insert_id(token_value,sym_ptr);    /* record the id number */
        }
    }
    if (token_type == TOKEN_ID_num)
    {
#if DEBUG_DO_TOKEN_ID
      printf("do_token_id(): Found TOKEN_ID_num for symbol %s. flag=%d, token_value = %d, it_table_base=%d\n",
          token_pool,flag,token_value, id_table_base);
#endif
        tableIndex = id_table_base+token_value;
        if (tableIndex >= id_table_size)
        {
            sym_ptr = get_symbol_block(1);
            insert_id(token_value,sym_ptr);
        }
        else
        {
            sym_ptr = id_table[tableIndex];
            if (sym_ptr == 0)
            {
                id_table[tableIndex] = sym_ptr = get_symbol_block(1);
            }
        }
		if ( tableIndexP )
			*tableIndexP = tableIndex;
        return(sym_ptr);
    }
    bad_token(tkn_ptr,"ID number expected here");
    return(0);
}

/*******************************************************************
 * Check that there's no more crap on the line
 */

int f1_eol( void )
/*
 * At entry:
 *	no requirements
 * At exit:
 *	the next line has been placed in inp_str
 */
{
    get_token_c();
    if (token_type != EOL)
    {
        bad_token(tkn_ptr,"Extraneous data at end of line, ignored");
    }
    return(get_text());
}

/******************************************************************
 * Ignore the contents of a line
 */
int f1_featit( int flag )
/*
 * At entry:
 *  flag - not used
 * At exit:
 *	next line has been placed in inp_str
 */
{
    return(get_text());      /* ignore the whole line, get the next */
}

/******************************************************************
 * Ignore the contents of a line
 */
int f1_eatit( void )
/*
 * At entry:
 * At exit:
 *	next line has been placed in inp_str
 */
{
    return(get_text());      /* ignore the whole line, get the next */
}



/*******************************************************************
 * Check for multiple definition of symbol/segment
 */
int chk_mdf(int flag, SS_struct *sym_ptr, int quietly)
/*
 * At entry:
 *	flag =  0 - testing a segment name
 *		    1 - testing a symbol name
 *	        2 - testing a group name
 *  sym_ptr = pointer to ss block
 *  quietly = 0 if to squawk about multiple defines. 1=ignore multiple defines
 *  expr_stack has expression of symbol to be defined.
 *
 * At exit:
 *	returns 0 if symbol multiply defined
 *		1 if symbol/segment ok
 */
{
    int i;
    char *old_type,*new_type;
    char *sy_ptr="symbol",*sg_ptr="segment",*gr_ptr="group";
    i =      flag        * 8 +
             sym_ptr->flg_group  * 4 +
             sym_ptr->flg_segment    * 2 +
             sym_ptr->flg_symbol;
    switch (i)
    {
    default: {
            sprintf(emsg,"Internal error in chk_mdf. %d %d %d %d %d {%s} %s",
                    i,flag,sym_ptr->flg_group,sym_ptr->flg_segment,
                    sym_ptr->flg_symbol,sym_ptr->ss_string,
                    current_fnd->fn_buff);
            err_msg(MSG_FATAL,emsg);
            return(0);
        }
	case 0:
		/* Fall through to 2 */
	case 2:
		/* Fall through to 8 */
	case 8:
		/* Fall through to 16 */
	case 16:
		/* Fall through to 20 */
    case 20:
		return(1);  /* it's ok */
    case 9:
		{
			/* Testing a symbol. */
            if (sym_ptr->ss_fnd == current_fnd)
				return(1); /* ok if same file */
			if ( expr_stack_ptr == 1 && expr_stack[0].expr_code == EXPR_VALUE )
			{
				/* Expression testing against is an absolute value */
				if ( sym_ptr->flg_abs && sym_ptr->ss_value == expr_stack[0].expr_value )
				{
					/* Symbol is an absolute value */
					return 1;	 /* values are assigned the same, so must be absolute so it's ok */
				}
				if (    sym_ptr->ss_exprs
					&& sym_ptr->ss_exprs->len == 1
					&& sym_ptr->ss_exprs->ptr->expr_code == EXPR_VALUE
					&& sym_ptr->ss_exprs->ptr->expr_value == expr_stack[0].expr_value
				   )
				{
					/* Symbol is an absolute value but still attached to a single term expression */
					return 1;	 /* values are assigned the same, so must be absolute so it's ok */
				}
			}
			if ( !quietly )
			{
				sprintf(emsg,
						"Multiple definition of {%s}, attempted in file %s,\n\t%s%s",
						sym_ptr->ss_string,current_fnd->fn_buff,
						"...previously defined in file ",sym_ptr->ss_fnd->fn_buff);
				err_msg(MSG_WARN,emsg);
			}
			return 0;
        }
    case 1: {     /* new segment name matches old symbol name */
            new_type = sg_ptr;
            old_type = sy_ptr;
            break;
        }
    case 4: {     /* new segment name matches old group name */
            new_type = sg_ptr;
            old_type = gr_ptr;
            break;
        }
    case 10: {    /* new symbol name matches old segment name */
            new_type = sy_ptr;
            old_type = sg_ptr;
            break;
        }
    case 12: {    /* new symbol name matches old group name */
            new_type = sy_ptr;
            old_type = gr_ptr;
            break;
        }
    case 17: {    /* new group name matches old symbol name */
            new_type = gr_ptr;
            old_type = sy_ptr;
            break;
        }
    case 18: {    /* new group name matches old segment name */
            new_type = gr_ptr;
            old_type = sg_ptr;
            break;
        }
    }
    sprintf(emsg,"{%s} defined as a %s in file %s\n\t%s%s%s%s",
            sym_ptr->ss_string,new_type,current_fnd->fn_buff,
            "...previously defined as a ",old_type," in file ",
            sym_ptr->ss_fnd->fn_buff);
    err_msg(MSG_WARN,emsg);
    return(0);
}

/*******************************************************************
 * Define local/global symbol
 */
int f1_defg(int flag)
/*
 * At entry:
 * 	flag - 0 if local symbol, else global symbol
 * At exit:
 * 	symbol definition written to sym_def_file
 *
 * Command syntax:
 *	.defg ID expression
 *	.defl ID expression
 */
{
    SS_struct *ptr;
    if ((ptr = do_token_id(flag,NULL)) == 0)
		return(f1_eatit());
    if (qual_tbl[QUAL_CROSS].present)
	{
        if (flag != 0)
			do_xref_symbol(ptr,1);
	}
	if (exprs(-1) < 0)
	{     /* get an expression */
		bad_token(tkn_ptr,"Undefined expression");
		return(0);
	}
    if (!chk_mdf(1,ptr,0))
		return(f1_eatit());
    ptr->flg_symbol = 1;
    ptr->flg_defined = 1;    /* signal symbol found in .defg */
    ptr->flg_local = !flag;  /* signal symbol is local/global */
    ptr->ss_fnd = current_fnd;   /* record the actual file that defined it */
    ptr->flg_exprs = 1;      /* signal that there's an expression */
    ptr->flg_nosym = current_fnd->fn_nosym;
    write_to_symdef(ptr);    /* write symbol stuff */
    return(f1_eol());
}

/*******************************************************************
 * Get length of segment
 */
int f1_len(int flag)
/*
 * At entry:
 *	flag = not used
 * At exit:
 *	segment length set in the segment block
 *
 * Command syntax:
 *	.len ID segment_length_constant
 */
{
    struct ss_struct *sym_ptr;
    struct seg_spec_struct *seg_ptr;
	
    if (token_type == TOKEN_ID)
    {
        sprintf(emsg,"Segment {%s} ID declared with a .len in %s",
                token_pool,current_fnd->fn_buff);
        err_msg(MSG_WARN,emsg);
    }
    if ((sym_ptr = do_token_id(2,NULL)) == 0)
		return(f1_eatit());
    if (!chk_mdf(0,sym_ptr,0))
	{
		return(f1_eatit());
	}
    switch (get_token(-1))
    {
    default: {
            bad_token(tkn_ptr,"Constant expected here");
            return(f1_eatit());
        }
    case TOKEN_const: {
            if ((seg_ptr=sym_ptr->seg_spec) == 0)
            {
                if ((seg_ptr=get_seg_spec_mem(sym_ptr)) == 0) break;
            }
            seg_ptr->seg_len = token_value;
			if ( seg_ptr->sflg_literal && seg_ptr->seg_maxlen )
				seg_ptr->seg_maxlen -= token_value;
        }
    }
    return(f1_eol());
}

/*******************************************************************
 * Segment declaration
 */
int f1_seg(int flag)
/*
 * At entry:
 *	flag = not used
 * At exit:
 *	symbol ID inserted in symbol table(s)
 *
 * Command syntax:
 *	.seg ID constant combin identifier
 * where
 *	constant - alignment factor
 *	combin - "u" for unique, "c" for common (overlaid) or
 *		constant for alignment of data within segment
 *	identifier - list of segment options {abduz} for abs,based,data,unref,zero
 */
{
    int i,combin,align;
    struct ss_struct *sym_ptr;
    struct seg_spec_struct *seg_ptr;
#if 0
    struct seg_spec *fseg_ptr;
#endif
    if ((sym_ptr = do_token_id(2,NULL)) == 0)
		return(f1_eatit());
    get_token(-1);       /* get the alignment constant */
    if (token_type != TOKEN_const)
    {
        bad_token(tkn_ptr,"Alignment constant expected here");
        token_value = 0;
    }
    if (!chk_mdf(0,sym_ptr,0))
	{
		return(f1_eatit());
	}
#if 0
    if (new_symbol & 4) fseg_ptr = first_symbol->seg_spec;
#endif
    align = token_value;         /* save the alignment constant */
    get_token(-1);           /* get the combin argument */
    if (token_type == TOKEN_uc )
    {
        if (token_curchr == 'c')
        {
            sym_ptr->flg_ovr = 1;      /* signal that we're to be overlaid */
            if (new_symbol & 4)
            {      /* it added one to the list */
                if (!(first_symbol->flg_segment && first_symbol->flg_ovr))
                {
                    sprintf(emsg,"Segment {%s} is defined in %s as Common\n\t%s%s%s",
                            sym_ptr->ss_string,current_fnd->fn_buff,
                            "and is defined in ",first_symbol->ss_fnd->fn_buff,
                            " as Unique");
                    err_msg(MSG_WARN,emsg);
                }
            }
        }
        combin = 0;           /* no special alignment with cu */
    }
    else
    {
        if (token_type != TOKEN_const)
        {
            bad_token(tkn_ptr,"Alignment constant, 'u' or 'c' expected here");
            combin = 0;
        }
        else
        {
            combin = token_value;
        }
    }
    if ((seg_ptr=sym_ptr->seg_spec) == 0)
    {
        if ((seg_ptr=get_seg_spec_mem(sym_ptr)) == 0) return(f1_eol());
    }
    get_token(-1);
    if (token_type != TOKEN_ID)
    {
        bad_token(tkn_ptr,"Segment class ID expected here");
    }
    else if (token_value != 0)
    {
        char *s=token_pool;
        for (i=0;i<token_value;++i,++s)
        {
            if (*s == 'a')
            {
                sym_ptr->flg_abs = 1; continue;
            }
            if (*s == 'b')
            { /* sym_ptr->flg_based = 1; */ 
                continue;
            }
            if (*s == 'c')
            {
                sym_ptr->flg_ovr = 1; continue;
            }
            if (*s == 'd')
            {
                seg_ptr->sflg_data = 1; continue;
            }
            if (*s == 'l')
            {
                seg_ptr->sflg_literal = 1; continue;
				haveLiteralPool = 1;
				if (lit_group == 0)
				{
					lit_group_nam = get_symbol_block(1);
					lit_group_nam->ss_string = "Literal Pool";
					lit_group = get_grp_ptr(lit_group_nam,0,0);
					lit_group_nam->ss_fnd = current_fnd;
					lit_group_nam->seg_spec->sflg_literal = 1;
					lit_group_nam->seg_spec->seg_maxlen = 256*1024-32;
				}
				insert_intogroup(lit_group,sym_ptr,lit_group_nam);
            }
            if (*s == 'o')
            {
                sym_ptr->flg_noout = 1; continue;
            }
            if (*s == 'r')
            {
                seg_ptr->sflg_ro = 1; continue;
            }
            if (*s == 'u')
            {
                seg_ptr->sflg_noref = 1; continue;
            }
            if (*s == 'z')
            {
                seg_ptr->sflg_zeropage = 1; continue;
            }
            bad_token(tkn_ptr+(s-token_pool),"Unknown segment class code");
        }
    }
    sym_ptr->flg_defined = 1;        /* .seg defines the segment */
    seg_ptr->seg_salign = align;     /* set the alignment factor */
    seg_ptr->seg_dalign = combin;    /* set data alignment factor */
    if (!(new_symbol & 4) &&     /* if not a duplicate symbol */
        !sym_ptr->flg_member)
    {   /* and not already a group member */
        if (sym_ptr->flg_abs)
        {
            if (abs_group == 0)
            {
                abs_group_nam = get_symbol_block(1);
                abs_group_nam->ss_string = "Absolute_sections";
                abs_group = get_grp_ptr(abs_group_nam,0,0);
                abs_group_nam->ss_fnd = current_fnd;
                abs_group_nam->flg_based = 1;
                abs_group_nam->seg_spec->sflg_absolute = 1;
            }
            insert_intogroup(abs_group,sym_ptr,abs_group_nam);
        }
        else
        {
            if (seg_ptr->sflg_zeropage)
            {
                if (base_page_grp == 0)
                {
                    base_page_nam = get_symbol_block(1);
                    base_page_nam->ss_string = "Zero_page_sections";
                    base_page_nam->ss_fnd = current_fnd;
                    base_page_grp = get_grp_ptr(base_page_nam,0,0);
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
#if 0
    if (new_symbol & 4)
    {        /* if duplicate symbol */
        if (align != fseg_ptr->seg_salign)
        {
            sprintf(emsg,
                    "Segment {%s} declared in %s with alignment of %d\n\t%s%s%s%d",
                    sym_ptr->ss_string,current_fnd->fn_buff,align,
                    "and is declared in ",first_symbol->ss_fnd->fn_buff,
                    " with alignment of ",fseg_ptr->seg_salign);
            err_msg(MSG_WARN,emsg);
        }
        if (combin != seg_ptr->seg_dalign)
        {
            sprintf(emsg,
                    "Segment {%s} declared in %s with combin of %d\n\t%s%s%s%d",
                    sym_ptr->ss_string,current_fnd->fn_buff,combin,"and is declared in ",
                    first_symbol->ss_fnd->fn_buff," with combin of ",
                    fseg_ptr->seg_dalign);
            err_msg(MSG_WARN,emsg);
        }
    }
#endif
    return(f1_eol());
}

/*******************************************************************
 * Segment placement (absolute)
 */
int f1_abs(int flag)
/*
 * At entry:
 *	flag = not used
 * At exit:
 *	segment based at specified location
 *
 * Command syntax:
 *	.abs ID value
 */
{
    struct ss_struct *sym_ptr;
    struct seg_spec_struct *seg_ptr;
    char *s;
	uint32_t tableIndex;
	
    if (token_type != TOKEN_ID_num)
    {
        bad_token(tkn_ptr,"Expected an ID number here");
        return f1_eatit();
    }
    tableIndex = id_table_base+token_value;
    sym_ptr = id_table[tableIndex];
    if (sym_ptr == 0 || sym_ptr->ss_fnd != current_fnd)
    {
        bad_token(tkn_ptr,"Expected segment ID here");
        return f1_eatit();
    }
    seg_ptr = sym_ptr->seg_spec;
    if (seg_ptr->seg_group && seg_ptr->seg_group != abs_group_nam)
    {
        if (sym_ptr->flg_member)
        {
            if (seg_ptr->seg_group != group_list_default)
            {
                sprintf(emsg,"Segment {%s} cannot belong to group {%s} and be based",
                        sym_ptr->ss_string,seg_ptr->seg_group->ss_string);
                err_msg(MSG_ERROR,emsg);
                return(f1_eatit());
            }
        }
        if (abs_group == 0)
        {
            abs_group_nam = get_symbol_block(1);
            abs_group_nam->ss_string = "Absolute_sections ";
            abs_group = get_grp_ptr(abs_group_nam,0,0);
            abs_group_nam->flg_based = 1;
        }
        add_to_group(sym_ptr,abs_group_nam,abs_group); /* add seg to list */
    }
    if (sym_ptr->flg_based)
    {
        s = qual_tbl[QUAL_OCTAL].present ? "Segment {%s} previously based at %010lo" :
            "Segment {%s} previously based at %08lX";
        sprintf(emsg,s,sym_ptr->ss_string,seg_ptr->seg_base);
        err_msg(MSG_WARN,emsg);
    }
    if (sym_ptr->flg_more)
    {
        sprintf(emsg,"Based segment {%s} must have unique name",
                sym_ptr->ss_string);
        err_msg(MSG_ERROR,emsg);
        return(f1_eatit());
    }
    if (get_token(-1) == TOKEN_const)
    {
        sym_ptr->seg_spec->seg_base = token_value; /* base the segment */
        sym_ptr->flg_abs = sym_ptr->flg_based = 1; /* signal based */
        return(f1_eol());
    }
    bad_token(tkn_ptr,"Expected a constant here");
    return(f1_eatit());
}

/*******************************************************************
 * External symbol declaration
 */
int f1_ext(int flag)
/*
 * At entry:
 *	flag = not used
 * At exit:
 *	symbol ID inserted in symbol table(s)
 *
 * Command syntax:
 *	.ext ID
 */
{
    if (do_token_id(1,NULL) == 0)
		return(f1_eatit());
    return(f1_eol());
}

/*******************************************************************
 * Test expression
 */
int f1_test(int flag)
/*
 * At entry:
 *  flag = 0, generic test
 *  flag = 1, branch offset out of range
 *  flat = 2, generic out of range test
 * At exit:
 *	test expression inserted into tmp_file
 *
 * Command syntax:
 *	.test "message" expression
 *  where message is the text that is to be output if expression is true
 *  expression is the expression to test (0 = false, not zero = true)
 */
{
    int size;
    char *s=0;           /* pointer to pointer to string */
    static const int tmp_opr[3] = {TMP_TEST, TMP_BOFF, TMP_OOR};

    if (token_type != TOKEN_ascs)
    {
        bad_token(tkn_ptr,"Expected message string here");
        f1_eatit();
        return 0;
    }
    size = token_value+1;
    s = (char *)MEM_alloc(size);
    strcpy(s, token_pool);   /* save message */
    if (exprs(-1) < 0)
    {     /* get an expression */
        bad_token(tkn_ptr,"Undefined expression");
        MEM_free(s);      /* give back the memory */
        f1_eatit();
        return 1;
    }
    write_to_tmp(tmp_opr[flag], expr_stack_ptr,
                 (char *)expr_stack,sizeof(struct expr_token));
    write_to_tmp(TMP_ASTNG,strlen(s),s,sizeof(char));
    MEM_free(s);         /* give back the memory */
    return(f1_eol());       /* next thing better be an eol */
}

/*******************************************************************
 * File Identification (.id)
 */
int f1_id( int flag )
/*
 * At entry:
 *  flag - not used
 * At exit:
 *	pointers updated in the current_fnd
 * Command syntax:
 *	.id type argument
 *  where type is one of "date" (argument = "creation_date");
 *	"target" (argument = "processor_type");
 *	"translator" (argument = "name_of_image_that_created_file");
 */
{
    int code=0;
    char **s=0;          /* pointer to pointer to string */
    if (token_type == TOKEN_ascs)
    {
        if (strcmp(token_pool,"date") == 0)
        {
            s = &current_fnd->fn_credate;
            code = 1;
        }
        if (strcmp(token_pool,"target") == 0)
        {
            s = &current_fnd->fn_target;
            code = 2;
        }
        if (strcmp(token_pool,"translator") == 0) s = &current_fnd->fn_xlator;
        if (strcmp(token_pool,"mod") == 0) s = &current_fnd->fn_name_only;
    }
    if (!s)
    {
/*      bad_token(tkn_ptr,
            "Expected one of \"date\", \"target\" or \"translator\"");
 */
        return(f1_eatit());
    }
    get_token(-1);       /* get the text */
    if (code == 1)
    {
        if (*s && *s != null_string)
        {
            if (strcmp(*s,token_pool) != 0)
            { /* does the date match the one in the lib? */
                sprintf(emsg,"Date code of file %s in library is \"%s\"\n\tand date code in file is \"%s\"",
                        current_fnd->fn_buff,*s,token_pool);
                err_msg(MSG_WARN,emsg);
            }
        }
    }
    *s = token_pool;     /* copy text pointer */
    if (code == 2 && target == 0) target = token_pool;
    token_pool += token_value;   /* move the pointer */
    *token_pool++ = 0;       /* null terminate the string */
    token_pool_size -= token_value+1; /* take from total */
    return(f1_eol());       /* next thing better be an eol */
}

/*******************************************************************
 * Group declaration command
 */
int f1_group( int flag )
/*
 * At entry:
 *  flag - not used
 * At exit:
 *	group table updated
 *
 * Command syntax:
 *	.group ID alignment_constant maxlen_constant group_list
 */
{
    int32_t i;
    struct ss_struct  *grp_nam,*sym_ptr;
    struct grp_struct *grp_ptr;
    if ((grp_nam = do_token_id(1,NULL)) == 0)
		return(f1_eatit());
    get_token(-1);       /* get the alignment constant */
    if (token_type != TOKEN_const)
    {
        bad_token(tkn_ptr,"Alignment constant expected here");
        return(f1_eatit());
    }
    if (!chk_mdf(2,grp_nam,0))
	{
		return(f1_eatit());
	}
    i = token_value;     /* save alignment factor */
    get_token(-1);       /* get the maxlen argument */
    if (token_type != TOKEN_const)
    {
        bad_token(tkn_ptr,"Maxlength constant expected here");
        return(f1_eatit());
    }
    grp_ptr = get_grp_ptr(grp_nam,i,token_value);
    while (get_token(-1) == TOKEN_ID || token_type == TOKEN_ID_num)
    {
        if ((sym_ptr = do_token_id(1,NULL)) == 0)
			return(f1_eatit());
        if (!add_to_group(sym_ptr,grp_nam,grp_ptr)) break;
    }
    return(f1_eol());
}   


/*******************************************************************
 * Set program transfer address
 */
int f1_start( int flag )
/*
 * At entry:
 *  flag - not used
 * At exit:
 *	tmp file updated with start command
 *
 * Command syntax:
 *	.start expression
 */
{
    if (exprs(1) > 0 && 
        expr_stack_ptr < sizeof(expr_stack)/sizeof(struct expr_token))
    {
        if (expr_stack_ptr == 1 && expr_stack[0].expr_code == EXPR_VALUE &&
            (expr_stack[0].expr_value&~1) == 0) return(f1_eatit());
        if (xfer_fnd != 0)
        {
            sprintf(emsg,"Transfer address spec'd in %s will not override one spec'd in %s",
                    current_fnd->fn_buff,xfer_fnd->fn_buff);
            err_msg(MSG_WARN,emsg);
            return(f1_eatit());
        }
        xfer_fnd = current_fnd;
        write_to_tmp(TMP_START,expr_stack_ptr,
                     (char *)expr_stack,sizeof(struct expr_token));
        return(f1_eol());
    }
    bad_token(tkn_ptr,"Expression stack overflow");
    return(f1_eatit());
}

/*******************************************************************
 * Set origin (program PC).
 */
int f1_org(int flag)
/*
 * At entry:
 *	flag - not used
 * At exit:
 *	tmp file updated with origin command
 *
 * Command syntax:
 *	.org ID expression
 */
{
    if (exprs(1) > 0 && 
        expr_stack_ptr < sizeof(expr_stack)/sizeof(struct expr_token))
    {
        expr_stack[expr_stack_ptr].expr_code = EXPR_OPER;
        expr_stack[expr_stack_ptr++].expr_value  = '+';
        write_to_tmp(TMP_ORG,expr_stack_ptr,
                     (char *)expr_stack,sizeof(struct expr_token));
        return(f1_eol());
    }
    bad_token(tkn_ptr,"Expression stack overflow");
    return(f1_eatit());
}

/*******************************************************************
 * Set absolute origin (program PC).
 */
int f1_aorg(int flag)
/*
 * At entry:
 *	flag - not used
 * At exit:
 *	tmp file updated with origin command
 *
 * Command syntax:
 *	.aorg ID constant
 */
{
    if (do_token_id(flag,NULL) == 0)
		return(f1_eatit());
    if (get_token(-1) == TOKEN_const)
    {
        expr_stack[0].expr_code = EXPR_VALUE;
        expr_stack[0].expr_value = token_value;
        write_to_tmp(TMP_ORG,1l,(char *)expr_stack,sizeof(struct expr_token));
        return(f1_eol());
    }
    bad_token(tkn_ptr,"Expected a constant here");
    return(f1_eatit());
}

/*******************************************************************
 * Get debug filename spec
 */
int f1_dbgod( int flag)
/*
 * At entry:
 *	flag - not used
 * At exit:
 *	.od filename and version recorded in current_fnd
 *
 * Command syntax:
 *	.dbgod "filename" "version"
 */
{
    if (token_type != TOKEN_ascs)
    {
        bad_token(tkn_ptr,"Expected a filename string here");
        return(f1_eatit());
    }
    current_fnd->od_name = token_pool;   /* copy text pointer */
    token_pool += token_value;   /* move the pointer */
    *token_pool++ = 0;       /* null terminate the string */
    token_pool_size -= token_value+1; /* take from total */
    get_token(-1);       /* get the next token */
    if (token_type != TOKEN_ascs)
    {
        bad_token(tkn_ptr,"Expected a version string here");
        return(f1_eatit());
    }
    current_fnd->od_version = token_pool; /* copy text pointer */
    token_pool += token_value;   /* move the pointer */
    *token_pool++ = 0;       /* null terminate the string */
    token_pool_size -= token_value+1; /* take from total */
    return(f1_eol());       /* next thing better be an eol */
}

/*******************************************************************/

struct cmd_struct
{
    char *cs_strng;      /* pointer to command string */
    int  (*cs_func1)( int arg );      /* what to do */
    int cs_flag;         /* aux flags */
};

struct cmd_struct cmds[] = {
    { 0,    0,      0},
    { "org",    f1_org ,    0},
    { "defg",   f1_defg ,   1},
    { "seg",    f1_seg ,    0},
    { "len",    f1_len ,    0},
    { "ext",    f1_ext ,    0},
    { "defl",   f1_defg ,   0},
    { "id",     f1_id ,     0},
    { "start",  f1_start ,  0},
    { "group",  f1_group ,  0},
    { "abs",    f1_abs ,    0},
    { "aorg",   f1_aorg ,   0},
    { "bgn",    f1_featit ,  0},
    { "end",    f1_featit ,  0},
    { "dcl",    f1_featit ,  0},
    { "mark",   f1_featit ,  0},
    { "test",   f1_test ,   0},
    { "bofftest",f1_test ,  1},
    { "oortest", f1_test ,  2},
    { "file",   f1_featit ,  0},
    { "dbgod",  f1_dbgod ,  0}
};

#define CMDS_SIZE (sizeof(cmds)/sizeof(struct cmd_struct))

int cmd_search(char *string_ptr, int *cmd)
{
    for (*cmd=1;*cmd<CMDS_SIZE;(*cmd)++)
    {
        if (!strcmp(string_ptr,cmds[*cmd].cs_strng))
            return(*cmd);
    }
    return(NULL);
}
   
/*******************************************************************
 * Expression evaluator
 */
int exprs( int flag)
/*
 * At entry:
 *	flag - .ge. 0 means have the whole token in the token buffer
 * At exit:
 *	expression stack filled with expression
 */
{
    SS_struct *sym_ptr;
    EXPR_token *exp;
    exp = expr_stack;
    if (flag < 0 )
		get_token(flag);  /* get rest of token */
    expr_stack_ptr = 0;
    while (1)
    {
        flag = 0;
        if (expr_stack_ptr >= (sizeof(expr_stack)/sizeof(EXPR_token)))
            return(-1);
		exp->ss_id = 0;		/* preclear these items */
		exp->ss_ptr = NULL;
        switch (token_type)
        {
        case TOKEN_ID:
        case TOKEN_ID_num:
			{
				uint32_t tableIndex;
                if ((sym_ptr = do_token_id(1,&tableIndex)) == 0)
                {  /* process token ID */
                    return(-1);
                }
                exp->expr_code = EXPR_IDENT;
                exp->expr_value = 0;
				exp->ss_id = tableIndex;
                ++exp;
                ++expr_stack_ptr;
                break;
            }
        case TOKEN_const: {
                exp->expr_code = EXPR_VALUE;    
                exp->expr_value = token_value;
                ++exp;
                ++expr_stack_ptr;
                break;
            }
        case TOKEN_oper: {
                char tok;
                tok = *token_pool;
                exp->expr_code = EXPR_OPER;
                exp->expr_value = tok;
                if (tok == '!')
                {       /* relational? */
                    exp->expr_value |= *inp_ptr++ << 8;
                }
#if 0
                if (tok == '+' || tok == '-')
                { /* special case? */
                    int t=0;
                    struct expr_token *tos,*sos;
                    tos = exp-1;
                    sos = exp-2;
                    if (tos->expr_code == EXPR_VALUE) t = 1;
                    else if (tos->expr_code == EXPR_IDENT) t = 2;
                    if (sos->expr_code == EXPR_VALUE) t |= 4*1;
                    else if (sos->expr_code == EXPR_IDENT) t |= 4*2;
                    if (t == 10)
                    {       /* sos is IDENT, tos is IDENT */
                        if (tok == '+')
                        {
                            sos->expr_value += tos->expr_value;
                        }
                        else
                        {
                            sos->expr_value -= tos->expr_value;
                        }
                        tos->expr_value = 0;
                    }
                    else if (t == 9 ||     /* sos is IDENT, tos is VALUE */
                             t == 5)
                    {     /* sos is VALUE, tos is VALUE */
                        if (tok == '+')
                        {
                            sos->expr_value += tos->expr_value;
                        }
                        else
                        {
                            sos->expr_value -= tos->expr_value;
                        }
                        exp = tos;
                        --expr_stack_ptr;
                        break;
                    }
                }
#endif
                ++exp;
                ++expr_stack_ptr;
                break;
            }
        case TOKEN_LB: {
                exp->expr_code = (*token_pool == 'L') ? EXPR_L:EXPR_B;
                get_token(-1);
                if ((sym_ptr = do_token_id(1,NULL)) != 0)
                {
                    exp->expr_value = 0;
                    exp->ss_ptr = sym_ptr;
                    ++exp;
                    ++expr_stack_ptr;
                }
                else
                {
                    flag++;
                }
                break;
            }
        default:
			return expr_stack_ptr;
        }
        if (!flag) get_token(-1);
    }
}


/*******************************************************************
 * Library file processor. Reads the library file and inserts the
 * appropriate file into the input stream if the symbols are present
 * but not yet defined.
 */
struct fn_struct *library( void )
/*
 * At entry:
 * 	no requirements. (called by main dispatcher)
 * At exit:
 *	additional files may have been inserted into the input
 *	file stream.
 */
{
    int len=0,lib_len=0,flag=0,errcnt=0;
    char c,*s,*lib_date=0;
    struct fn_struct *fnd=0,*old_fnd=current_fnd;
    struct ss_struct *sym_ptr;
    while (1)
    {
        if (get_text() == EOF)
        {
            if (errcnt != 0) return(0);
            return(old_fnd);
        }
        if ((c= *inp_ptr++) == '\t')
        {
            if (fnd == 0) continue;    /* no filename yet */
            s = inp_ptr;
            while (((c = *s) != 0) && !isspace(c)) s++;
            *s++ = 0;
            if ((sym_ptr=sym_lookup(inp_ptr,(int32_t)(s-inp_str),0)) != 0)
            {
                if (sym_ptr->flg_segment) continue; /* ignore segments */
                if (sym_ptr->flg_defined) continue; /* already defined */
                if (sym_ptr->flg_libr) continue; /* already sucked in a library */
                if (debug > 2)
                {
                    printf ("\tSymbol: {%s} gets file %s\n",
                            inp_ptr,fnd->fn_buff);
                }
                if (flag == 0)
                {
                    int erc;
                    fnd->r_length = len;
#ifdef VMS
                    fnd->d_length = len + 1;
#endif
                    if (lib_date != 0) fnd->fn_credate = lib_date;
                    fn_pool += lib_len;      /* keep the string(s) */
                    fn_pool_size -= lib_len;     /* take from total */
                    erc = add_defs(fnd->fn_buff,def_obj_ptr,(char **)0,0,&fnd->fn_nam);
                    errcnt += erc;
                    if (erc == 0)
                    {
                        fnd->fn_buff = fnd->fn_nam->full_name;
                        fnd->fn_name_only = fnd->fn_nam->name_only;
                    }
                    else
                    {
                        sprintf(emsg,"Unable to parse filename \"%s\" (from lib): %s",
                                fnd->fn_buff,err2str(errno));
                        err_msg(MSG_WARN,emsg);
                    }
                    fnd->fn_present = 1;
                    fnd->fn_nosym = current_fnd->fn_nosym;
                    fnd->fn_nostb = current_fnd->fn_nostb;
                    fnd->fn_next = old_fnd->fn_next;
                    old_fnd->fn_next = fnd;
                    old_fnd = fnd;
                }
                flag = sym_ptr->flg_libr = 1;   /* got a file */
            }          /* --symbol in sym table */
            continue;      /* ignore the symbol */
        }             /* --tab */
        if (!c || isspace(c)) continue;
        if (fnd == 0 || flag != 0) fnd = get_fn_struct(); /* get a fn struct */
        s = fn_pool;
        *s++ = c;
        while (((c= *inp_ptr++) !=0) && !isspace(c)) *s++ = c;    /* get the filename */
        *s++ = 0;         /* terminate the filename */
        lib_date = 0;     /* say there's no date */
        len = s-fn_pool;   
        while (((c= *inp_ptr++) !=0) && c != '"');    /* skip to date field */
        if (c)
        {          /* is there one? */
            lib_date = s;      /* remember where it starts */
            while (((c= *inp_ptr++) !=0) && c != '"') *s++ = c; /* skip to the end */
            *s++ = 0;      /* terminate the date string */
        }
        lib_len = s-fn_pool;   
        flag = 0;
    }                /* --while (1) */
}               /* --library() */

/*******************************************************************
 * Main entry for PASS1 processing
 */
void pass1( void )
/*
 * At entry:
 *	no requirements
 * At exit:
 *	lots of stuff done. Symbol tables built, temp files filled
 *	with text, etc.
 */
{
    int i, tkn_flg= 1,(*f)();
    if (get_text() == EOF)
		return;
    while (1)
    {
        if (tkn_flg)
			get_token(-1);
        tkn_flg = 1;          /* assume not to get next token */
#if 0
		printf("pass1(): token_type=%d(%s), token_pool=%s, token_value=%d, inp_ptr=%s\n",
			   token_type, token_type_to_ascii(token_type), token_pool, token_value, inp_ptr);
#endif
        switch ((i=token_type))
        {
        case TOKEN_cmd: {
                i = cmd_search(token_pool,&cmd_code);
                if ((f = cmds[i].cs_func1) != 0)
                {  /* do the command process */
                    get_token(-1);       /* get the next token for command */
                    (*f)(cmds[i].cs_flag);   /* do command */
                }
                else
                {
                    bad_token(tkn_ptr,"Failed to decode command");
                }
                break;
            }
        case TOKEN_bins: {
                write_to_tmp(TMP_BSTNG,token_value,token_pool,sizeof(char));
                break;
            }
        case TOKEN_ascs: {
                write_to_tmp(TMP_ASTNG,token_value,token_pool,sizeof(char));
                break;
            }
        case TOKEN_sep: {
                break;
            }
        case TOKEN_ID:
        case TOKEN_ID_num:
        case TOKEN_const: {
                if (exprs(1) > 0)
                {
#if 0
					{
						EXP_stk tmpExp;
						tmpExp.len = expr_stack_ptr;
						tmpExp.ptr = expr_stack;
						dump_expr("After exprs()", &tmpExp);
					}
#endif
                    write_to_tmp(TMP_EXPR,expr_stack_ptr,
                                 (char *)expr_stack,sizeof(struct expr_token));
                    if (token_type != TOKEN_expr_tag)
                    {
                        bad_token(tkn_ptr,"Expected expression tag here, l 1 assumed");
                    }
                    tkn_flg = 0;     /* say not to get a token next time */
                }
                else
                {
                    bad_token(tkn_ptr,"Expression stack overflow");
                }
                break;
            }
        case EOL: {
                if (get_text() != EOF) break;
            }
        case EOF: {
                return;
            }
        case TOKEN_expr_tag: {
                if (token_value == 1)
                {
                    char tag;
                    tag = *token_pool;
                    get_token(-1);       /* get next token */
                    if (token_type == TOKEN_const)
                    {
                        write_to_tmp(TMP_TAG,token_value,&tag,1);
                    }
                    else
                    {
                        write_to_tmp(TMP_TAG,1l,&tag,1);
                        tkn_flg = 0;      /* say not to get a token next time */
                    }
                }
                else
                {
                    bad_token(tkn_ptr,"Expected one of {b,c,s,w,W,i,I,u,U,l,L}");
                }
                break;
            }
        case TOKEN_LB:
        case TOKEN_oper: {
                bad_token(tkn_ptr,"Expression operator out of place");
                if (get_text() == EOF) return;
                break;
            }
        default: {
                sprintf(emsg,"Undefined token %d\n",i);
                err_msg(MSG_ERROR,emsg);
                bad_token(tkn_ptr,"Bad source");
                break;
            }
        }
    }
}
