/*
    grpmgr.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include "header.h"		/* get our standard stuff */


struct ss_struct  *group_list_default=0; /* pointer to default group name */
struct grp_struct *group_list_top=0;    /* pointer to top of group list */
struct grp_struct *group_list_next=0;   /* pointer to next free space */
long group_list_free;           /* number of free spaces left */
long grp_pool_used;

/********************************************************************
 * Group lists. There are 2 seperate lists involved in group lists:
 * A list of groups and a list of segments in each group.
 * The group list is initialised to a contigious set of 8 grp_struct's.
 * The group_list_top variable always points to the first grp_struct of the
 * first contigious set. The group_list_next variable points to the
 * next available grp_struct in a set and group_list_free contains the
 * number of free grp_structs left in a set. If more than 8 groups
 * are specified, then another contigious set of 8 grp_struct's are
 * obtained from the free pool and the last grp_struct of the previous
 * set points to the first grp_struct in the new set. To indicate that
 * the grp_struct points to another group struct instead of the segment
 * list, the first item in the grp_struct (grp_top) is set to -1. 
 * The grp_structs are organized thus:
 *
 *	group_list_top  ------>	grp_struct
 *	group_list_next ---	grp_struct
 *			  |	...
 *			  |	grp_struct = grp_top = -1
 *   			  |		     grp_next ---
 *			  |				|
 *			  | -----------------------------
 *			  | |
 *			  | --> grp_struct
 *			  |	...
 *			  ---->	grp_struct (grp_top = 0)
 *
 *	group_list_free = number of items left in the current free pool
 *
 * The grp_struct has 3 elements: grp_top, grp_next and grp_free. These
 * are used to manage a segment list similarly to the way the group list
 * is managed.
 *
 * The segment list is an array of pointers to ss_structs (presumably
 * segment structures). The blank array is drawn from the free pool
 * and if more segments are needed than there are elements in the array then
 * another array is taken from the free pool, the second to last element in
 * the previous array is set to -1 and the last element (and grp_next) is
 * made to point to the first element in the new array.
 *
 *	grp_top ----------> ss_struct *
 *	grp_next ----	    ss_struct *
 *		    |	    ...		(a value of -2 implies segment deleted)
 *		    |	    -1
 *		    |	    ss_struct ** ----
 *		    |			    |
 *		    | -----------------------
 *		    | |
 *		    | ----> ss_struct *
 *		    |	    ss_struct *
 *		    |	    ...
 *		    ------> 0
 *	grp_free contains the number of free elements left in the array.
 *
 * The group name is held in an ss_struct. Its ->seg_spec->seg_group
 * element points to the group list element defining the segment list.
 * That element in each of the segments in the group points back to the
 * group name ss_struct. This completes a closed loop so that given any
 * random segment pointer, we can find out what group it's in and what's
 * in that group.
 *
 ********************************************************************/

/********************************************************************
 * Group list manager. 
 */
void insert_intogroup(
                     GRP_struct *grp_ptr,
                     SS_struct *sym_ptr,
                     SS_struct *grp_nam
                     )
/*
 * Inserts the sym_ptr into the specified group.
 * At entry:
 *	grp_ptr - pointer to group struct into which to stuff segment.
 *	sym_ptr - pointer to segment struct to insert.
 *	grp_nam - pointer to ss_struct containing group name
 */
{
    struct ss_struct **sp;
    if (grp_ptr->grp_free <= 2)
    {
        int t = 32*sizeof(struct ss_struct **);
        sp = (struct ss_struct **)MEM_alloc(t);
        grp_pool_used += t;
        if (grp_ptr->grp_free)
        {
            *(long *)grp_ptr->grp_next++ = -1;
            *(struct ss_struct ***)grp_ptr->grp_next = sp;
        }
        else
        {
            grp_ptr->grp_top = sp;
        }
        grp_ptr->grp_next = sp;
        grp_ptr->grp_free = 8;
    }
    *grp_ptr->grp_next++ = sym_ptr;
    grp_ptr->grp_free -= 1;
    sym_ptr->seg_spec->seg_group = grp_nam;
    sym_ptr->flg_member = 1;
    return;
}

/*******************************************************************
 * get_grp_ptr - get a pointer to group struct
 */
GRP_struct *get_grp_ptr(
                       SS_struct *grp_nam,
                       long align,
                       long maxlen
                       )
/*
 * At entry:
 *	grp_nam - pointer to group name ss_struct
 *	align - group alignment factor
 *	maxlen - maxlen for group
 * At exit:
 *	returns pointer to grp_struct
 */
{
    struct grp_struct *grp_ptr;
    struct seg_spec_struct *seg_ptr;
    if (!grp_nam->flg_group)
    {
        grp_nam->flg_group = grp_nam->flg_defined = 1;
        seg_ptr = get_seg_spec_mem(grp_nam);  /* get the special section */
        grp_nam->flg_segment = 0; /* reset the segment flag */
        seg_ptr->seg_salign = (unsigned short)align; /* pass the segment alignment factor */
        seg_ptr->seg_maxlen = maxlen; /* pass the maximum length */
        if (group_list_free <= 1)
        {
            int t = 10*sizeof(struct grp_struct);
            grp_ptr = (struct grp_struct *)MEM_alloc(t);
            grp_pool_used += t;
            group_list_next->grp_top = (struct ss_struct **)-1l;
            group_list_next->grp_next = (struct ss_struct **)grp_ptr;
            group_list_next = grp_ptr;
            group_list_free = 10;
        }
        grp_ptr = group_list_next++;
        seg_ptr->seg_group = (struct ss_struct *)grp_ptr;
        group_list_free -= 1;
    }
    else
    {
        grp_ptr = (struct grp_struct *)(seg_ptr = grp_nam->seg_spec)->seg_group;
        if (seg_ptr->seg_maxlen != maxlen)
        {
            if (grp_ptr != group_list_top)
            {
                sprintf(emsg,"Group {%s} maxlen is %lu in file: %s\n\t%s%lu%s%s",
                        grp_nam->ss_string,seg_ptr->seg_maxlen,
                        grp_nam->ss_fnd->fn_buff,
                        "and is ",maxlen,
                        " in file: ",current_fnd->fn_buff);
                err_msg(MSG_WARN,emsg);
            }
            else
            {
                seg_ptr->seg_maxlen = maxlen;
            }
        }
        if (seg_ptr->seg_salign != (unsigned short)align)
        {
            if (grp_ptr != group_list_top)
            {
                sprintf(emsg,"Group {%s}'s align is %u in file: %s\n\t%s%lu%s%s",
                        grp_nam->ss_string,seg_ptr->seg_salign,
                        grp_nam->ss_fnd->fn_buff,
                        "and is ",align,
                        " in file: ",current_fnd->fn_buff);
                err_msg(MSG_WARN,emsg);
            }
            else
            {
                seg_ptr->seg_salign = (unsigned short)align;
            }
        }
    }
    return grp_ptr;
}

/*******************************************************************
 * find_seg_in_group - looks through the default group list for
 * the segment.
 */
SS_struct **find_seg_in_group(
                             SS_struct *sym_ptr,     /* pointer to segment which to look for */
                             SS_struct **grp_list)   /* pointer to group list to search */
{
    while (*grp_list)
    {          /* 0 means end of list */
        if (*(long *)grp_list == -1)   /* -1 means next entry is new pointer */
        {
            ++grp_list;
            grp_list = *(SS_struct ***)grp_list; /* link to next list */
            continue;
        }
        if (*grp_list == sym_ptr) return grp_list;
        grp_list++;           /* up to next entry */
    }
    return 0;   
}

/*******************************************************************
 * add_to_group list
 */
int add_to_group(
                struct ss_struct *sym_ptr,
                struct ss_struct *grp_nam,
                struct grp_struct *grp_ptr
                )
/*
 * Adds a segment to a group list
 * At entry:
 *	sym_ptr - points to symbol to add to list
 *	grp_nam - points to group name ss_struct
 *	grp_ptr - points to group list
 *	returns FLASE if mem alloc error, else always returns
 *		TRUE.
 */
{
    struct ss_struct **sym_list;
    struct seg_spec_struct *seg_ptr;
    if (!chk_mdf(0,sym_ptr)) return TRUE; /* exit if multiple definitions */
    if ((seg_ptr=sym_ptr->seg_spec) == 0)
    {
        if ((seg_ptr=get_seg_spec_mem(sym_ptr)) == 0) return FALSE;
    }
    if (sym_ptr->flg_member)
    {
        if (seg_ptr->seg_group == grp_nam) return TRUE; /* already in the group */
        if (seg_ptr->seg_group != group_list_default)
        {
            sprintf(emsg,
                    "Segment {%s} declared in group {%s} in file %s\n\t%s%s%s%s",
                    sym_ptr->ss_string,grp_nam->ss_string,current_fnd->fn_buff,
                    "and declared in group {",seg_ptr->seg_group->ss_string,
                    "} in file ",seg_ptr->seg_group->ss_fnd->fn_buff);
            err_msg(MSG_WARN,emsg);
            return TRUE;
        }
    }
    insert_intogroup(grp_ptr,sym_ptr,grp_nam);   /* stick seg into group */

/* Next scan the default group list and remove the segment from it if present */

    sym_list = find_seg_in_group(sym_ptr, group_list_top->grp_top);
    if (sym_list)
    {
        *(long *)sym_list = -2;       /* -2 means skip it */
    }
    return TRUE;             /* done */
}      
