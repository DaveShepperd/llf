/*
    symbol.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

/********************************************************************
 *
 * This module does all the symbol table managment. The symbol table
 * storage technique used here is the chained hash table (see Compiler
 * Writing, Tremblay & Sorenson). The hash table size is chosen here to
 * quite large since memory is not as much at a premium on the VAX as on
 * other computers (i.e. PDP-11). You may consider reducing the hash table
 * size if you are to port this to a memory limited system.
 * Tremblay/Sorenson suggest that the hash table size be a prime number. 
 *
 *******************************************************************/

#include <stdio.h>		/* get standard I/O definitions */
#include <string.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"

#undef NULL
#define NULL 0

#define DEBUG 9

/* external references */
#if 0
extern int hashit();        /* hashing routine */
#endif
/* Static Globals */

int32_t sym_pool_used;
SS_struct *hash[HASH_TABLE_SIZE];  /* hash table */
SS_struct *symbol_pool=0; /* pointer to next free symbol space */
int symbol_pool_size=0;     /* number of symbol spaces left */
SS_struct *first_symbol=0; /* pointer to first symbol of 'duplicate' list */
int16_t new_symbol;       /* flag indicating that an insert happened */
/* value (can be added)	*/
/*   1 - symbol added to symbol table */
/*   2 - symbol added is first in hash table */
/*   4 - symbol added is duplicate symbol */

/************************************************************************
 * Get a block of memory to use for symbol table
 */
struct ss_struct *get_symbol_block( int flag )
/*
 * At entry:
 *      flag:
 *		0 = don't take block from pool.
 *		1 = get a block and take it from the pool.
 *
 * At exit:
 *	returns pointer to next free symbol block (0'd)
 */
{
    if (symbol_pool_size <= 0)
    {
        int t = 256*sizeof(struct ss_struct);
        sym_pool_used += t;
        symbol_pool = (struct ss_struct *)MEM_alloc(t);
        symbol_pool_size = 256;
    }
    if (!flag) return(symbol_pool);
    --symbol_pool_size;      /* count it down */
    return(symbol_pool++);   /* get pointer to free space */
}

/*******************************************************************
 *
 * Symbol table lookup and insert
 */
SS_struct *sym_lookup(
                     char *strng,
                     int32_t slen,
                     int err_flag)
/*
 * At entry:
 *	strng - pointer to a null terminated symbol name string
 *      slen - length of the string (including null)
 *      err_flag:
 *		0 = don't automatically insert the symbol
 *	      		into the symbol table if its not there.
 *		1 = automatically insert it if its not there.
 *		2 = insert it even if its already there but only
 *		    if the previous symbol is defined.
 *
 * At exit:
 *	If err_flag is != 0, then returns with pointer to either new
 * 	symbol block if symbol not found or old symbol block if symbol
 * 	already in symbol table. If err_flag == 0, then returns
 * 	NULL if symbol not found in the symbol table, else returns with
 * 	pointer to old symbol block.
 *********************************************************************/
{
    int i,condit;
    struct ss_struct *st,**last,*new,*old=0;
    first_symbol = NULL;
    new_symbol = NULL;

/* Check for presence of free symbol block and add one if none */

    if (slen <= 0) return(NULL); /* not there, don't insert it */
    if (symbol_pool_size <= 0)
    {
        if (!get_symbol_block(0)) return(NULL);
    }

/* hash the hash value */

    i = hashit(strng,HASH_TABLE_SIZE);

/* Pick up the pointer from the hash table to the symbol block */
/* If NULL then this is a table miss, add a new entry to the hash table */

    if ((st = hash[i]) == 0)
    {
        if (!err_flag) return(NULL); /* no symbol */
        st = hash[i] = symbol_pool++; /* pick up pointer to new symbol block */
        --symbol_pool_size;   /* take from total */
        st->ss_string = strng;    /* set the string constant */
        st->ss_strlen = slen;   /* set the length of the string */
        st->ss_prev = hash+i; /* ptr to place that holds ptr to us */
        new_symbol = 3;       /* 3 = symbol added and is first in the chain */
        return(st);       /* return pointing to new block */
    }

/* This is a table collision. Its linear search time from here on. */
/* The symbols are stored ordered alphabetically in the list. The */
/* variable "last" is a pointer to a pointer that says where to */
/* deposit the backlink. If the result of a string compare denotes */
/* equality, the routine exits pointing to the found block. If the */
/* user's string is less than the one tested against, then a new block */
/* is inserted in front of the tested block else a new one is added at */
/* the end of the list */

    last = hash + i;     /* remember place to stuff backlink */

    while (1)
    {           /* loop through the whole ordered list */
        condit = strcmp(strng,st->ss_string);
        if (condit < 0) break; /* not there if user's is less */
        if (condit == 0)
        {
            if (err_flag != 2) return(st); /* found it */
            if (!st->flg_defined) return(st); /* found it */
            new_symbol |= 4;   /* signal duplicate symbol to be added */
            if (!first_symbol) first_symbol = st; /* record first entry */
        }
        last = &(old=st)->ss_next;  /* next place to store backlink */
        if ((st = st->ss_next) == 0) break; /* get link to next block, exit if NULL */
    }

    if (!err_flag) return(NULL); /* not there, don't insert one */

/* Have to insert a new block in between two others or at the end of the */
/* list. (st is NULL if inserting at the end). */

    new_symbol |= 1;     /* signal that we've added a symbol */
    new = symbol_pool++;     /* get pointer to free space */
    --symbol_pool_size;      /* count it down */
    if (new_symbol & 4)
    {    /* duplicate symbol being added */
        old->flg_more = 1;    /* signal that there's another symbol */
    }
    *last = new;         /* point previous block to the new one (backlink) */
    new->ss_prev = last;     /* keep the ptr to the place holding ptr to us */
    new->ss_next = st;       /* point to the following one */
    new->ss_string = strng;  /* point to the string */
    new->ss_strlen = slen; /* record the string length */
    if (st != 0) st->ss_prev = &new->ss_next; /* next guy gets backlink to us */
    return(new);         /* return pointing to new block */
}    

/*********************************************************************
 * Delete symbol from symbol table.
 */
SS_struct *sym_delete( SS_struct *old_ptr )
/*
 * At entry:
 *	old_ptr - pointer to symbol block to delete
 * At exit:
 *	symbol removed from the symbol table if present. Routine always
 *	returns old_ptr.
 */
{
    int i;
    struct ss_struct *st,**last;

/* compute the hash value */

    i = hashit(old_ptr->ss_string,HASH_TABLE_SIZE);

/* Pick up the pointer from the hash table to the symbol block */
/* If NULL then this is a table miss, nothing to delete */

    if ((st = hash[i]) == 0) return(old_ptr); /* he can have the old one */

/* There is a symbol in the symbol table pointed to by the hash table. */
/* The first entry is special in that the backlink is actually the hash */
/* table itself. Otherwise, this routine finds the occurance of the old_ptr */
/* in the chain and plucks it out by patching the link fields. */

    last = hash + i;     /* place to stash backlink */

    while (1)
    {           /* loop through the whole ordered list */
        if (st == old_ptr)
        {
            *last = old_ptr->ss_next; /* pluck it out, set the backlink */
            if (old_ptr->ss_next != 0) old_ptr->ss_next->ss_prev = last;
            old_ptr->ss_next = 0;
            old_ptr->ss_prev = 0;
            return(old_ptr);   /* he can have the old block */
        }
        last = &st->ss_next;  /* next place to store backlink */
        if ((st = st->ss_next) == 0) break; /* get link to next block, exit if NULL */
    }
    return(old_ptr);     /* not in the table, give 'em the old one */
}    

void do_xref_symbol( SS_struct *sym_ptr, unsigned int defined)
{
    struct fn_struct **fnp_ptr;
    int i=0,j=0;
    if ((fnp_ptr = sym_ptr->ss_xref) == 0)
    {
        fnp_ptr = sym_ptr->ss_xref = get_xref_pool();
        *fnp_ptr = (struct fn_struct *)-1l; /* reserve first place for "defined in" file */
    }
    if (*fnp_ptr == (struct fn_struct *)-1l && defined != 0)
    {
        *fnp_ptr = current_fnd;   /* defined file is first in the list */
        j++;
    }
    if (j != 0)
    {
        fnp_ptr++;        /* skip the defined file */
        i++;          /* count it */
        while (*fnp_ptr != 0 && *fnp_ptr != current_fnd)
        {
            i++;
            fnp_ptr++;
            if (i < XREF_BLOCK_SIZE-1) continue;
            if ((fnp_ptr= *((struct fn_struct ***)fnp_ptr)) == 0) return;
            i = 0;
            continue;
        }
        if (*fnp_ptr == current_fnd) *fnp_ptr = (struct fn_struct *)-1l;
        return;
    }
    while (*fnp_ptr != 0 )
    {
        if (*fnp_ptr++ == current_fnd) return;    /* already referenced */
        i++;
        if ( i < XREF_BLOCK_SIZE-1) continue;
        if (*fnp_ptr == 0)
        {  /* off the end and no more? */
            *((struct fn_struct ***)fnp_ptr) = get_xref_pool(); /* get a new block of memory */
        }
        fnp_ptr = *((struct fn_struct ***)fnp_ptr);
        i = 0;
        continue;
    }
    *fnp_ptr = current_fnd;  /* cross reference the symbol */
    return;
}
