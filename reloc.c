/*
    reloc.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include "version.h"		/* get our current version */
#include <stdio.h>		/* get standard I/O definitions */
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get all normal stuff */

#if 0
extern void free_rm_mem(RM_control **);
extern RM_control *clone_rm_mem(RM_control *), *rm_control;
extern void add_to_reserve(uint32_t start, uint32_t len);
extern int check_reserve(uint32_t start, uint32_t len);
extern int get_free_space(uint32_t len, uint32_t *start, int align);
extern void outseg_def();
#endif

/***********************************************************************
 * Segment locator utility routines. seg_place places a segment at a
 * specified address. It checks that it doesn't overlay another region
 * already allocated and reports same if so, but places it there anyway.
 */
static uint32_t seg_place(
/*
 * At entry:
 */
struct ss_struct *ms,       /* pointer to segment to place */
struct ss_struct *grp_nam,  /* pointer to group */
uint32_t base,     /* base address to place it */
uint32_t offset,       /* offset address to place it */
uint32_t len,      /* amount of space to reserve */
int chk)            /* if true, check for overlap */
/*
 * At exit:
 *
 *	returns the actual base address of the placed section
 */
{
    struct seg_spec_struct *seg_ptr;
    char *s;
    if (offset != 0)
    {
        uint32_t toff;
        toff = base+offset;
        if (chk && check_reserve(toff,len))
        {     /* TRUE if in reserved mem list */
            s = qual_tbl[QUAL_OCTAL].present ? "Segment {%s} at %010lo OUTPUT's at %010lo overwriting another segment or reserved mem":
                "Segment {%s} at %08lX OUTPUT's at %08lX overwriting another segment or reserved mem";
            sprintf (emsg,s,ms->ss_string,base,toff);
            err_msg(MSG_WARN,emsg);
        }
        add_to_reserve(toff,len);
    }
    else if ((seg_ptr=grp_nam->seg_spec)->sflg_zeropage == 0 &&
             seg_ptr->sflg_absolute == 0)
    {
        if (chk && check_reserve(base,len))
        {  /* TRUE if in reserved mem list */
            s = qual_tbl[QUAL_OCTAL].present ? "Segment {%s} at %010lo overlays another segment or reserved mem":
                "Segment {%s} at %08lX overlays another segment or reserved mem";
            sprintf (emsg, s, ms->ss_string, base);
            err_msg(MSG_WARN,emsg);
        }
        add_to_reserve(base,len);
    }
    seg_ptr = ms->seg_spec;
    ms->flg_noout = grp_nam->flg_noout; /* make the noout bits match */
    ms->ss_value = seg_ptr->seg_base = base;
    seg_ptr->seg_offset = offset;
    return base;
}

/***********************************************************************
 * Segment locator utility routine. seg_fit trys to fit a segment starting at
 * or after a specified address. If it won't fit within the range specified,
 * it'll complain but put it there anyway.
 */
static uint32_t seg_fit(
/*
 * At entry:
 */
struct ss_struct *ms,       /* pointer to segment to place */
struct ss_struct *grp_nam,  /* pointer to group */
uint32_t base,     /* base address to place it */
uint32_t offset,       /* offset address to place it */
uint32_t len,      /* amount of space to reserve */
int align,          /* minimum alignment to accept */
int chk)            /* if true, check for overlap */
/*
 * At exit:
 *
 *	returns the actual base address of the placed section
 */
{
    struct seg_spec_struct *seg_ptr;
    uint32_t c_base = base;
    char *s;
    base += align;
    base &= ~align;
    c_base = base;
    if (!get_free_space(len, &c_base, align))
    {
        if (chk)
        {
            s = qual_tbl[QUAL_OCTAL].present ? "No room for segment {%s} based at %010lo\n":
                "No room for segment {%s} based at %08lX\n";
            sprintf (emsg,s,ms->ss_string,base);
            err_msg(MSG_ERROR,emsg);
        }
        c_base = base;
    }
    seg_ptr = ms->seg_spec;
    ms->ss_value = seg_ptr->seg_base = c_base;
    seg_ptr->seg_offset = offset;
    return c_base;
}


/* exchange segments in the segment list */
static void swap_segs(SS_struct **one, SS_struct **two)
{
    struct ss_struct *a,*b,*tmp,**tmpp;
    a = *one;
    b = *two;
    if (a->ss_next == b)
    {       /* adjacent entries are special */
        a->ss_next = b->ss_next;
        b->ss_next = a;
        tmpp = a->ss_prev;
        a->ss_prev = &b->ss_next;
        b->ss_prev = tmpp;
        *tmpp = b;
        if (a->ss_next) a->ss_next->ss_prev = &a->ss_next;
        a->flg_more = b->flg_more;
        b->flg_more = 1;
    }
    else
    {
        if (b->ss_next != 0) b->ss_next->ss_prev = &a->ss_next;
        a->ss_next->ss_prev = &b->ss_next;
        tmpp = a->ss_prev;
        a->ss_prev = b->ss_prev;
        *b->ss_prev = a;
        *tmpp = b;
        b->ss_prev = tmpp;
        tmp = a->ss_next;
        a->ss_next = b->ss_next;
        b->ss_next = tmp;
        a->flg_more = b->flg_more;
        b->flg_more = 1;
    }
    *two = a;
    *one = b;
    return;
}

void dump_segtree( SS_struct *a )
{
    fprintf(stderr,"Dumping tree...\n");
    while (a)
    {
        fprintf(stderr,"cur=%p, next=%p, prev=%p, *prev=%p, more=%d, base=%08X, len=%08X\n",
                (void *)a,
				(void *)a->ss_next,
				(void *)a->ss_prev,
				(void *)*a->ss_prev,
				a->flg_more,
				a->seg_spec->seg_base,
				a->seg_spec->seg_len);
        if (!a->flg_more) break;
        a = a->ss_next;
    }
    return;
}

/***********************************************************************
 * Sort by size. This procedure will sort a list of segments in place and
 * in order by size, descending.
 */
static struct ss_struct *sort_by_size( 
/*
 * At entry:
 */
struct ss_struct *first)   /* ptr to first struct in the chain */
/*
 * At exit:
 *	nothing. Elements in the list will be
 *	sorted descending according to segment size.
 */
{
    struct ss_struct *a,*b,**beg;
    beg = first->ss_prev;
    for (a=first;a && a->flg_more;a=a->ss_next)
    {
        for (b=a->ss_next;b;b=b->ss_next)
        {
            if (a->seg_spec->seg_len < b->seg_spec->seg_len)
            {
                swap_segs(&a,&b);
            }
            if (!b->flg_more) break;
        } 
    }
    return *beg;
}

/***********************************************************************
 * Sort by base. This procedure will sort a list of segments in place and
 * in order by base address, ascending.
 */
static struct ss_struct *sort_by_base(
/*
 * At entry:
 */
struct ss_struct *first)   /* ptr to first struct in the chain */
/*
 * At exit:
 *	nothing. Elements in the list will be
 *	sorted ascending according to base address.
 */
{
    struct ss_struct *a,*b,**beg;
    beg = first->ss_prev;
    for (a=first;a && a->flg_more;a=a->ss_next)
    {
        for (b=a->ss_next;b;b=b->ss_next)
        {
            if (a->seg_spec->seg_base > b->seg_spec->seg_base)
            {
                swap_segs(&a,&b);
            }
            if (!b->flg_more) break;
        } 
    }
    return *beg;
}

/***********************************************************************
 * Segment locator.
 */
void seg_locate( void )
/*
 * Called by main line after all files have been processed. This routine
 * locates all the groups and segments according to the base value specified
 * or by the LOCATE command in the option file.
 */
{
    struct ss_struct *st,**ls,*grp_nam,*ms;
    struct grp_struct *grp_ptr;
    struct seg_spec_struct *seg_ptr,*grpseg_ptr;
    uint32_t base, align=0, seg_len=0, next_base, bottom, top, offset;
    int  f_based, jj, f_fit, f_stable;
    RM_control *rm_save=0;

    for (jj=0; jj < 4; ++jj)
    {
        if (jj == 0)
        {
            rm_save = rm_control;
            rm_control = 0;
        }
        else if (jj == 1)
        {
            if (rm_control)
                free_rm_mem(&rm_control);
            rm_control = rm_save;
        }

/*************************************************************************
* grp_list_top points to an array of pointers to grp_structs. Each
* grp_struct has a pointer to an array of pointers to segment structs. 
* This array of segment pointers is the actual list of segments which makeup
* a group. Each segment struct has a pointer to a group struct (identical to
* a segment struct) that contains the details about the group such as name,
* base, max size, etc.
* A group list entry whose value is  0 denotes the end of the list.
* A group list entry whose value is -1 indicates that the NEXT entry is a
*    pointer to another array of these pointers (continuation array). 
*************************************************************************/

        base = 0;         /* start location at 0 */
        grp_ptr = group_list_top; /* start at the list head */
        while ((ls = (grp_ptr++)->grp_top) != 0)
        {    /* point to group list */
            if (ls == (struct ss_struct **)-1l)        /* if it's a link, then */
            {
                --grp_ptr;                 /* back out the auto-inc above */
                grp_ptr = (struct grp_struct *)grp_ptr->grp_next;  /* point to next block of ptrs */
                continue;       /* and keep looking */
            }

/*************************************************************************
 * 'ls' points to the list of segment pointers that belong to this group.
 * A list entry whose value is  0 denotes the end of the list.
 * A list entry whose value is -1 denotes that the NEXT entry is a pointer
 *   to another array of these pointers (continuation array).
 * A list entry whose value is -2 denotes that this entry should be skipped
 *   (this happens when a segment is moved from one group to another).
 *************************************************************************/

            while ((st = *ls++) != 0)
            {    /* pickup pointer to segment struct */
                if (st == (struct ss_struct *)-2l)
                {
                    continue;    /* segment has been moved , skip this one */
                }
                if (st == (struct ss_struct *)-1l)
                {
                    ls = (struct ss_struct **)*ls;   /* point to next array */
                    continue;    /* and keep looping */
                }
                --ls;       /* undo the auto-increment */
                break;      /* and fall out of the loop */
            }
            if (st == 0) continue; /* the group is empty, nothing to do */
            grp_nam = st->seg_spec->seg_group; /* get pointer to group struct */
            if (!grp_nam->seg_spec->sflg_reloffset && (jj == 3 || jj == 0)) continue;
            grpseg_ptr = grp_nam->seg_spec;

/* This code is executed when jj == 3. It positions all the segments that have
   been located to another segment (if any) */

            if (jj == 3)
            {
                SS_struct *relof;
                relof = grp_nam->seg_spec->seg_reloffset;
                offset = relof->ss_value-grp_nam->ss_value; /* the offset is the location of this segment */
                grpseg_ptr->seg_offset = offset;
                while ((st = *ls++) != 0)
                {
                    if (st == (struct ss_struct *)-2l) continue;
                    if (st == (struct ss_struct *)-1l)
                    {
                        ls = (struct ss_struct **)*ls;
                        continue;
                    }
                    ms = st;
                    while (ms->flg_more)
                    {       /*  walk the chain and tell all nodes */
                        ms = ms->ss_next;
                        seg_ptr = ms->seg_spec;
                        seg_ptr->seg_offset = offset;
                    }
                    seg_ptr = st->seg_spec;
                    seg_ptr->seg_offset = offset;
                }
                continue;
            }

/****************************************************************/
/* This code is executed (at least twice, maybe) three times. 	*/
/* First with jj=0 to get all the relative offset groups.	*/
/* Then with  jj=1 to pickup all the based groups.		*/
/* Then with  jj=2 to pickup all the rest of the groups.	*/
/****************************************************************/

            f_based = grp_nam->flg_based;
            if (jj == 1)
            {
                if (!f_based) continue;     /* skip if not based */
            }
            else if (jj == 2)
            {
                if (f_based) continue;      /* skip if based */
            }
            st->flg_noout = grp_nam->flg_noout; /* make the noout bits match */
            if ((f_fit=grpseg_ptr->sflg_fit)) grp_nam->flg_based = 0;
            f_stable = grpseg_ptr->sflg_stable;
            base = grpseg_ptr->seg_base;
            top = bottom = base;
            if (jj != 0)
            {
                offset = grpseg_ptr->seg_offset; /* offset is carried as the... */
                /* ...difference between the base and the OUTPUT */
            }
            else
            {
                offset = 0;
            }
            grpseg_ptr->seg_len = 0;

/**************************************************************************
 * On pass 0, if there's any reserved memory, clone it
 **************************************************************************/

            if (jj == 0)
            {
                if (rm_control) free_rm_mem(&rm_control);
                rm_control = clone_rm_mem(rm_save);
            }

/**************************************************************************
 * Now we loop through all the segments in this group and locate them
 **************************************************************************/

            while ((st = *ls++) != 0)
            {
                int chk_flg,ovr_flg;
                if (st == (struct ss_struct *)-2l) continue;
                if (st == (struct ss_struct *)-1l)
                {
                    ls = (struct ss_struct **)*ls;
                    continue;
                }
                seg_ptr = st->seg_spec;
                st->flg_noout = grp_nam->flg_noout; /* make the noout bits match */
                if (st->flg_based) base = seg_ptr->seg_base;

/**************************************************************************
 * There may (probably will) be several segments with the same name which
 * are collected together (by the symbol insertion procedure) and treated
 * as a single segment. So we loop through the list of like named segments
 * either accumulating the sizes of each of them or getting the maximum length
 * if the segments are overlaid in order to treat all the segments as a single
 * one.
 **************************************************************************/

                ovr_flg = st->flg_ovr;      /* remember section overlaid bit */
                if (ovr_flg)
                {          /* do overlaid segments */
                    seg_len = seg_ptr->seg_len;  /* get length of segment */
                    ms = st;
                    align = (1 << seg_ptr->seg_salign) - 1;
                    while (ms->flg_more)
                    {       /*  sum the lengths... */
                        struct seg_spec_struct *ssp;
                        int taln;
                        ms = ms->ss_next;     /* ...segments */
                        ssp = ms->seg_spec;
                        taln = (1<<ssp->seg_salign)-1;
                        if (seg_len < ssp->seg_len) seg_len = ssp->seg_len;
                        if (align < taln) align = taln;
                    }
                }
                else
                {                /* make all segments one only if based */
                    if (!f_stable && f_fit && st->flg_more) st = sort_by_size(st);   /* sort them by size */
                }

/**************************************************************************
 * If we're not doing a relative link, then fix the segment to a particular
 * memory location else locate the segment at relative 0.
 **************************************************************************/

                if (jj == 0 || qual_tbl[QUAL_REL].present || grp_nam->seg_spec->sflg_reloffset)
                {
                    chk_flg = 0;         /* don't complain about overlaid sections */
                    if (qual_tbl[QUAL_REL].present) base = 0;
                }
                else
                {
                    chk_flg = 1;         /* else always complain */
                }
                ms = st;
                next_base = 0;
                while (1)
                {
                    unsigned t;
                    seg_ptr = ms->seg_spec;
                    if (!ovr_flg)
                    {
                        align = (1 << seg_ptr->seg_salign) - 1;
                        seg_len = seg_ptr->seg_len;
                    }
                    base += align;
                    base &= ~align;
                    if (qual_tbl[QUAL_REL].present || (f_based && !f_fit))
                    {
                        seg_place(ms, grp_nam, base, offset, seg_len, chk_flg);
                    }
                    else
                    {
                        base = seg_fit(ms, grp_nam, f_stable?base:bottom, offset, seg_len, align, chk_flg);
                    }           
                    t = base+seg_len;
                    if (seg_len > 0 && t-1 < base)
                    { /* check for memory wrap */
                        sprintf(emsg,"Segment {%s} wrapped top of address space",ms->ss_string);
                        err_msg(MSG_WARN,emsg);
                    }
                    if (next_base < t) next_base = t; /* maximize the next base */
                    if (top < t) top = t;        /* maximize the whole group */
                    if (ovr_flg)
                    {
                        chk_flg = 0;          /* don't complain about additional overmaps */
                    }
                    else
                    {
                        base = t;         /* move location */
                    }
                    if (!ms->flg_more) break;    /* stop if no additional segments */
                    ms = ms->ss_next;        /* point to the next one and continue */
                } 
                if (st->flg_more && !f_stable)
                {
                    st = sort_by_base(st);       /* sort them by location */
                    *(ls-1) = st;            /* tell grp where seglist starts */
                }
                ms = st;                /* st may have been changed by one of the sorts! */
                while (1)
                {
                    ms->seg_spec->seg_first = st;    /* record the first in all the segs */
                    ms->seg_spec->seg_group = grp_nam; /* make sure this points to the right group */
                    if (!ms->flg_more) break;
                    ms = ms->ss_next;
                }
                if (ovr_flg) base += seg_len;   /* now move if segments overlaid */
                if (jj != 0)
                {
                    if (sec_fp != 0 && grpseg_ptr->sflg_reloffset == 0)
                    {
                        outseg_def(st, next_base-st->seg_spec->seg_base, f_based);
                    }
                    st->flg_exprs = 0;       /* reset the expression flag 	*/
                }
            }              /* --while (segment loop)	*/
            if (jj == 0)
            {
                grpseg_ptr->seg_reloffset->seg_spec->seg_len = top-bottom; /* length of segment is length of group */
                continue;           /* nothing more to do in this loop */
            }
            grp_nam->ss_value = bottom;    /* indicate the position of the group */
            grpseg_ptr->seg_len = top-bottom;
            if ((align = grpseg_ptr->seg_maxlen) != 0)
            {
                if (grpseg_ptr->seg_len > align)
                {
                    sprintf(emsg,
                            "Group {%s} exceeds maximum length by %d bytes\n",
                            grp_nam->ss_string,grpseg_ptr->seg_len-align);
                    err_msg(MSG_WARN,emsg);
                }               /* --if				*/
            }              /* --if				*/
        }                 /* --while (group loop)		*/
    }                    /* --while (1)			*/
}                   /* --seg_locate()		*/
