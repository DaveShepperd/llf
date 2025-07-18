/*
    insert_id.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get the common stuff */

/***************************************************************
 *
 * void insert_id(numeric_id,ptr_to_sym_struct)
 * int32_t numeric_id;
 * struct seg_struct *ptr_to_sym_struct;
 *
 * This routine inserts into the ID table at the offset spec'd by
 * the first argument the symbol block pointer passed as the
 * second argument. It will increase the size of the ID table
 * if required.
 *
 * Routine returns void. To retrieve an ID from the table,
 * use *(id_table[id_table_base+desired_id])
 ***************************************************************/

/* Global static variables */

struct ss_struct **id_table = 0; /* pointer to identifier table */
int32_t id_table_size=0;       /* size of id_table in int's */
int32_t id_table_base=0;       /* base for the current file */
int32_t tot_ids,max_idu;

/* Entry */

void insert_id( int32_t id, SS_struct *id_ptr)
{
    struct ss_struct **st,*sp;   /* st is pointer to pointer to ss_struct */
    int32_t idx,kk;
	
	idx = id+id_table_base;
    if (idx >= id_table_size)
    {
        kk = (idx/1024+1)*1024;
        if (id_table == 0)
        {  /* first time, build an array */
            if (kk < 28672/sizeof(char *))
				kk = 28672/sizeof(char *);
			id_table_size = kk;
            id_table = (SS_struct **)MEM_calloc(id_table_size, sizeof(SS_struct **));
        }
        else
        {
            int old_sz;
            old_sz = id_table_size;
			id_table_size = kk;
            id_table = (SS_struct **)MEM_realloc((char *)id_table, id_table_size*sizeof(SS_struct **));
            while (old_sz<id_table_size)
				id_table[old_sz++] = NULL;
        }
    }
    if (idx > max_idu)
		max_idu = idx;
	st = id_table+idx;
	sp = *st;
    if ( sp )
    {
        if (sp == id_ptr)
        {       /* reinsertion of the same ID */
            return;            /* is ok */
        }
        sprintf (emsg,"ID number %d redefined in %s\n\tPreviously defined to {%s} in file %s",
                 id,current_fnd->fn_buff,sp->ss_string,sp->ss_fnd->fn_buff);
        err_msg(MSG_WARN,emsg);
    }
    tot_ids++;
    *st = id_ptr;
    return;
}
