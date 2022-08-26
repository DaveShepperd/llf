/*
    reserve.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"

RM_control *rm_control;     /* all reserved memory info is recorded here */
long rm_pool_used;

/**********************************************************************
 * Get a reserved_memory block of memory
 */
static struct rm_struct *get_rm_mem(RM_control **rmcp)
/*
 * At entry:
 *	no requirements
 * At exit:
 *	rm_control will have been created and/or updated.
 */
{
    RM_control *rmc;

    if ((rmc = *rmcp) == 0)
    {    /* if first time through */
        *rmcp = rmc = (RM_control *)MEM_alloc(sizeof(RM_control));
        rmc->size = 16;
        rmc->list = (RM_struct **)MEM_alloc(rmc->size*sizeof(RM_struct *));
        rmc->used = 0;
        rmc->free = 0;
        rmc->pool_used = sizeof(RM_control) + rmc->size*sizeof(RM_struct *);
        rm_pool_used += rmc->pool_used;
    }
    if (rmc->free <= 0)
    {
        if (rmc->used >= rmc->size)
        {
            rmc->size += 16;
            rmc->list = (RM_struct **)MEM_realloc((char *)rmc->list, rmc->size*sizeof(RM_struct *));
            rmc->pool_used += 16*sizeof(rmc);
            rm_pool_used += 16*sizeof(rmc);
        }
        rmc->free = 32;
        rmc->next = rmc->list[rmc->used++] = (RM_struct *)MEM_alloc(rmc->free*sizeof(RM_struct));
        if (rmc->top == 0) rmc->top = rmc->next;
        rmc->pool_used += rmc->free * sizeof(RM_struct);
        rm_pool_used += rmc->free * sizeof(RM_struct);
    }
    --rmc->free;
    return rmc->next++;
}

/**********************************************************************
 * Free reserved memory lists
 */
void free_rm_mem(RM_control **rmcp)
/*
 * At entry:
 *	rmcp = pointer to pointer to RM_control struct
 * At exit:
 *	*rmcp free'd and cleared
 */
{
    int ii;
    RM_control *rmc;
    if ((rmc = *rmcp) == 0) return;  /* nothing to do */
    for (ii=0; ii < rmc->used; ++ii)
    {
        MEM_free((char *)rmc->list[ii]);  /* free memory */
    }
    MEM_free((char *)rmc->list);     /* free the list */
    rm_pool_used -= rmc->pool_used;
    MEM_free((char *)rmc);
    *rmcp = 0;
    return;
}

/**********************************************************************
 * Clone reserved memory list
 */
RM_control *clone_rm_mem(RM_control *rmc)
/*
 * At entry:
 *	rmc - pointer to RM_control struct which to clone
 * At exit:
 *	returns pointer to new RM_control struct with duplicate data in it
 */
{
    RM_control *new=0;
    RM_struct *curr=0,*next,*old;
    if (rmc == 0) return 0;      /* nothing to do */
    old = rmc->top;          /* point to first element in chain */
    do
    {
        next = get_rm_mem(&new);      /* get an entry */
        if (curr) curr->rm_next = next;
        curr = next;
        curr->rm_start = old->rm_start;
        curr->rm_len = old->rm_len;
    } while ((old=old->rm_next) != 0);
    curr->rm_next = 0;
    return new;
}

/****************************************************************************
 * Fold reserved memory list collapsing overlapping entries to a single one
 */
static void fold_reservmem(RM_struct *rm)
/*
 * At entry:
 *	rm - pointer to RM_struct struct which to collapse
 * At exit:
 *	returns nothing
 */
{
    unsigned long start,end;
    struct rm_struct *nrm;
    if ((nrm=rm->rm_next) == 0) return;
    start = rm->rm_start;
    while (nrm)
    {
        end = start+rm->rm_len;
        if (nrm->rm_start > end) break;
        if (end < nrm->rm_start+nrm->rm_len) rm->rm_len += nrm->rm_len-(end-nrm->rm_start);
        rm->rm_next = nrm->rm_next;
        nrm = nrm->rm_next;
    }
    return;
}

/**********************************************************************
 * Add an item to the reserved memory list
 */
void add_to_reserve(start,len)
unsigned long start;
unsigned long len;
/*
 * Adds the start/len pair to the reserved memory list
 * At entry:
 *	start - beginning location to exclude
 *	len   - length of area to exclude
 * At exit:
 *	will have updated reserved memory list. merges overlapping
 *	areas into one.
 */
{
    unsigned long et,end;
    int condit;
    struct rm_struct **prev=0, *rm, *nrm;;
    if (!len) return;        /* don't do anything if adding a 0 len seg */
    if (rm_control && (rm = rm_control->top) != 0)
    {
        prev = &rm_control->top;
        end = start + len;        /* compute end address */
        if (end < start)
        {
            end = 0xFFFFFFFFl;
            len = 0xFFFFFFFFl-start;
        }
        while (1)
        {
            et = rm->rm_start+rm->rm_len;  /* compute end address */
            if (et < rm->rm_start)
            {
                et = 0xFFFFFFFFl;
                rm->rm_len = 0xFFFFFFFFl-rm->rm_start;
            }
            condit = 1*(start == rm->rm_start)+
                     2*(start < rm->rm_start) +
                     4*(end == et)            +
                     8*(end < et);
            switch (condit)
            {
            case 4:             /* ns >  os && ne == oe */
            case 8:             /* ns >  os && ne <  oe */
            case 5:             /* ns == os && ne == oe */
            case 9: return;         /* ns == os && ne <  oe */
            case 1: {               /* ns == os && ne >  oe */
                    rm->rm_len = len;        /* make oe = ne */
                    fold_reservmem(rm);      /* merge overlapping segments */
                    return;              /* done */
                }
            case 2:             /* ns <  os && ne >  oe */
            case 6: {               /* ns <  os && ne == oe */
                    rm->rm_len = len;        /* oe = ne */
                    rm->rm_start = start;        /* os = ns */
                    fold_reservmem(rm);      /* merge overlapping segments */
                    return;              /* done */
                }
            case 10: {              /* ns <  os && ne <  oe */
                    if ( end >= rm->rm_start)
                    {  /* if ns <  os && ne >= os */
                        rm->rm_len += rm->rm_start-start; /* increase the length */
                        rm->rm_start = start;     /* make os = ns */
                    }
                    else
                    {             /* ns <  os && ne <  os */
                        nrm = get_rm_mem(&rm_control);/* there's a hole. Get a rm_struct */
                        nrm->rm_start = start;    /* set the start */
                        nrm->rm_len = len;        /* set the length */
                        nrm->rm_next = rm;        /* link the new to the old */
                        *prev = nrm;          /* link previous to new */
                    }
                    return;              /* done */
                }
            case 0: {               /* ns >  os && ne >  oe */
                    if (start <= et)
                    {       /* if ns >  os && ns <= oe */
                        rm->rm_len = end - rm->rm_start; /* oe = ne */
                        fold_reservmem(rm);       /* merge overlapping segments */
                        return;
                    }
                    break;               /* skip to next entry */
                }
            }
            prev = &rm->rm_next;           /* point to previous link */
            if ((rm = rm->rm_next) == 0) break;    /* link to next */
        }                     /* --while */
    }                        /* --if */
    rm = get_rm_mem(&rm_control);        /* get some memory */
    rm->rm_start = start;            /* insert the start */
    rm->rm_len = len;                /* and the length */
    if (prev)
    {
        *prev = rm;               /* point previous to new */
    }
    return;          /* that's all there is to it */
}               /* --add_to_reserve */

/**********************************************************************
 * Check some address to against the reserved memory list
 */
int check_reserve(unsigned long start, unsigned long len)
/*
 * check the start/len pair against the reserved memory list
 * At entry:
 *	start - beginning location to check for
 *	len   - length of area to check for
 * At exit:
 *	returns TRUE if specified area is in the reserved memory list
 */
{
    unsigned long et,end;
    struct rm_struct *rm;
    if (!len) return FALSE;  /* 0 len segment is not in list */
    if (rm_control == 0 || (rm=rm_control->top) == 0)
    {
        return FALSE;     /* reserved memory is emtpy */
    }
    end = start + len - 1;       /* compute end address */
    do
    {
        et = rm->rm_start + rm->rm_len - 1;
        if (start >= rm->rm_start && start <= et)
        {
            return TRUE;           /* it's in the tables */
        }
        if (end >= rm->rm_start && end <= et)
        {
            return TRUE;           /* it's in the table */
        }
        if (start < rm->rm_start && end > et)
        {
            return TRUE;           /* it's in the table */
        }
        if (end < rm->rm_start )
        {
            break;             /* it's not in the table */
        }
    } while ((rm = rm->rm_next) != 0);
    return FALSE; /* not in the list */
}

/**********************************************************************
 * Get next free n bytes and declare the area as reserved
 */
int get_free_space(unsigned long len, unsigned long *start, int align)
/*
 * Finds the first available n bytes as indicated by the reserved mem list
 * At entry:
 *	len   - length of area to get
 *	start - address of long containing location to begin the search
 *	align - alignment mask required for return addresses
 * At exit:
 *	returns TRUE and start address of area in *start if available
 *	else returns FALSE
 */
{
    unsigned long sta= *start;
    struct rm_struct *rm;
    if (rm_control != 0 && (rm = rm_control->top) != 0)
    {
        do
        {
            if (!len)
            {
                if (sta < rm->rm_start) break;
            }
            else
            {
                if (sta + len <= rm->rm_start) break; /* use sta as start addr */
            }
            if (rm->rm_start+rm->rm_len >= sta)
            {
                sta = rm->rm_start+rm->rm_len;  /* move to space after */
                sta += align;           /* correct the alignment */
                sta &= ~align;
            }
        } while ((rm = rm->rm_next) != 0);        /* skip to next element */
    }
    if (sta + len < sta) return FALSE;       /* no room */
    add_to_reserve(sta,len);         /* add it to the list */
    *start = sta;                /* pass back start address */
    return TRUE;                 /* say it's ok */
}
