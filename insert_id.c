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
 * long numeric_id;
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
long id_table_size=0;       /* size of id_table in int's */
long id_table_base=0;       /* base for the current file */
long tot_ids,max_idu;

/* Entry */

void insert_id( long id, SS_struct *id_ptr)
{
    struct ss_struct **st,*sp;   /* st is pointer to pointer to ss_struct */
    long i,k;
    if ((i=id+id_table_base) >= id_table_size)
    {
        k = (i/1024l+1l)*1024l;
        if (id_table == 0)
        {  /* first time, build an array */
            if (k < 28672/sizeof(char *)) k = 28672/sizeof(char *);
            id_table = (struct ss_struct **)MEM_calloc((unsigned int)(id_table_size=k),
                                                       sizeof (struct ss_struct **));
        }
        else
        {
            int old_sz;
            old_sz = id_table_size;
            id_table = (struct ss_struct **)MEM_realloc((char *)id_table,
                                                        (unsigned int)((id_table_size = k)*(sizeof(struct ss_struct **))));
            while (old_sz<id_table_size) id_table[old_sz++] = 0;
        }
    }
    if (i > max_idu) max_idu = i;
    if ((sp = *(st = id_table+i)) != 0)
    {
        if (sp == id_ptr)
        {       /* reinsertion of the same ID */
            return;            /* is ok */
        }
        sprintf (emsg,"ID number %ld redefined in %s\n\tPreviously defined to {%s} in file %s",
                 id,current_fnd->fn_buff,sp->ss_string,sp->ss_fnd->fn_buff);
        err_msg(MSG_WARN,emsg);
    }
    tot_ids++;
    *st = id_ptr;
    return;
}
