/*
    mapsym.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include "version.h"
#ifdef VMS
    #include <ssdef.h>
#endif
#include <stdio.h>		/* get standard I/O definitions */
#include <string.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"
#include "qksort.h"

static int32_t tot_lcl,tot_rel,tot_gbl,tot_udf;
static struct ss_struct **sorted_symbols=0;

/**************************************************************************
 * Symbol table sort
 */
void sort_symbols( void )
/*
 * At entry:
 *	no requirements
 * At exit:
 *	returns the address of an array of pointers to ss_structs
 *	  sorted by symbol name ascending alphabetically. There
 *	  will be tot_gbl + tot_lcl entries and terminated with null.
 */
{
    int32_t i,j,ret;
    struct ss_struct **ls,*st,*ost=0,**spp;

    for (j=i=0;i<HASH_TABLE_SIZE;i++)
    {
        if ((st=hash[(int16_t)i]) != 0)
        {
            do
            {
                if (st->flg_segment) continue;  /* ignore segments */
                if (st->flg_group) continue;    /* ignore groups */
                if (!st->flg_defined) ++tot_udf;/* count undefined */
                if (st->flg_exprs) ++tot_rel;   /* total relative syms */
                if (st->flg_local)
                {
                    ++tot_lcl;               /* count local */
                    if (st->flg_defined) continue; /* ignore defined locals */
                }
                j++;                /* count the records */
            } while ((st=st->ss_next) != 0);
        }
    }
    if (j != 0)
    {            /* any records to sort? */
        int t = (j+1)*sizeof(struct ss_struct *);
        misc_pool_used += t;
        ls = (struct ss_struct **)MEM_alloc(t);
        sorted_symbols = ls;
        for (i=ret=0;i<HASH_TABLE_SIZE;i++)
        {
            if ((st=hash[(int16_t)i]) != 0)
            {
                do
                {
                    if (st->flg_segment) continue;  /* ignore segments */
                    if (st->flg_group) continue;    /* ignore groups */
                    if (st->flg_local && st->flg_defined) continue;
                    if (ret < j) *ls++ = st;        /* record the pointer */
                    ++ret;
                } while ((st=st->ss_next) != 0);
            }
        }
        *ls = 0;              /* terminate the array */
        if (ret > j)
        {
            sprintf(emsg,"Counted %d syms but tried to sort %d",j,ret);
            err_msg(MSG_ERROR,emsg);
        }
        qksort(sorted_symbols,(unsigned int)j);
        spp = sorted_symbols;
        ls = sorted_symbols;
        while ((*spp = st = *ls++) != 0)
        {
            if (ost != st)
            {       /* eliminate duplicate syms */
                ost = st;           /* not dup, remember this one */
                spp++;          /* and move the output ptr */
                ++tot_gbl;          /* count global (for map purposes) */
                if (outxsym_fp != 0 &&
                    st->flg_defined &&
                    st->flg_abs &&
                    !st->flg_local)
                {
                    outsym_def(st,output_mode);  /* write to .sym file */
                }
            }
        }
    }
    return;
}

char *map_title=0,*map_subtitle=0;
int map_line;           /* lines remaining on map page */
int lines_per_page = 60;
int columns_per_line = 132;

static void print_map_title( void ) {
    char *s;
    fputs(map_title,map_fp);     /* write title line */
    map_line = lines_per_page-1;     /* reset the line counter */
    if (map_subtitle)
    {
        fputs(map_subtitle,map_fp);   /* write subtitle line */
        s = map_subtitle;         /* point to subtitle */
        while (*s) if (*s++ == '\n') --map_line; /* count \n's in text */
    }
    else
    {
        fputs("\n\n",map_fp);     /* write two blank lines */
        map_line -= 2;            /* count the 2 lines */
    }
    *map_title = '\f';       /* make first char a FF */
    return;
}

/**************************************************************************
 * PUTS_MAP - put a string to map file
 */
void puts_map( const char *string, int lines )
/*
 * At entry:
 *	string - pointer to text to write to map file
 *	lines - number of lines in the text (0=don't know how many)
 */
{
    int i;
    const char *s;
    if (!map_fp) return;         /* easy out if no map file */
    if (!map_title)
    {
        misc_pool_used += 133;
        map_title = MEM_alloc(133);   /* get some memory for a title */
        sprintf(map_title," %-40s LLF %s   %s\n",
                output_files[OUT_FN_ABS].fn_name_only,
                REVISION,ascii_date);
    }
    if (lines < 0)
    {         /* do special processing */
        int j, len;
        len = strlen(string);
        if (len <= columns_per_line)
        {
            lines = 0;         /* do normal processing */
        }
        else
        {
            while (*string)
            {
                const char *space=NULL;
				int sLen;
                s = string;
                for (j=0; j < columns_per_line && *s; ++j, ++s)
                {
                    if (*s == '\n') break;
                    if (*s == ' ' || *s == '\t') space = s;
                }
                if (map_line <= 1) print_map_title();
                if (space)
                {            /* if there's a space in the text */
                    s = space+1;         /* the new start is one after the last space */
                    while (space > string && (*(space-1) == ' ' || *(space-1) == '\t')) --space;
                }
                else
                {
                    space = s;
                }
				sLen = space-string;
                fprintf(map_fp, "%*.*s\n", sLen , sLen, string);
                if (*s == '\n') ++s;        /* if stopped on a \n, eat it */
                map_line -= 1;
                string = s;
            }
            return;
        }
    }
    if ((i=lines) == 0)
    {
        if ((s = string) != 0 )
        {         /* point to string */
            int j;
            while (*s)
            {
                for (j=0; j < columns_per_line && *s && *s != '\n'; ++s, ++j);
                if (*s == '\n') ++s;    /* skip the newline */
                ++i;
            }
        }
    }
    if (!i || i > map_line)
    {        /* room on page? */
        print_map_title();        /* insert page break */
    }
    if (i)
    {
        fputs(string,map_fp);     /* write caller's text */
        map_line -= i;            /* take lines from total */
    }
    return;              /* done */
}

static char *seg_control_string0=0;
static char *seg_control_stringn0=0;
static char *group_control_string0=0;
static char *group_control_stringn0=0;


static void dump_available( void )
{
    uint32_t low;
    struct rm_struct *rm;
    map_subtitle = "Available areas in the address space\n\n Start  -  End      Size\n-------- --------  --------\n";
    if (map_line < 6)
    {      /* respectable amount of room left? */
        puts_map(0l,0);       /* no, skip to top of form */
    }
    else
    {
        puts_map("\n",1);     /* write a blank line */
        puts_map(map_subtitle,0); /* else write the title line */
    }
    low = 0;
	if (rm_control)
	{
		rm = rm_control->top;    /* point to reserved memory list */
		while (rm)
		{         /* as long as there is something */   
			if (rm->rm_start > low)
			{
				if ( qual_tbl[QUAL_OCTAL].present )
					sprintf(emsg, "%08o-%08o  %08o\n",
							low&0xFFFFFF,
							(rm->rm_start-1)&0xFFFFFF,
							(rm->rm_start-low)&0xFFFFFF);
				else
					sprintf(emsg, "%08X-%08X  %08X\n",
							low,rm->rm_start-1,rm->rm_start-low);
				puts_map(emsg,1);
			}
			low = rm->rm_start+rm->rm_len;
			rm = rm->rm_next;
		}
	}
	if ( qual_tbl[QUAL_OCTAL].present )
		sprintf(emsg, "%08o-77777777  %08o\n", low&0xFFFFFF, (low == 0 ? -1 : 0 - low)&0xFFFFFF);
	else
		sprintf(emsg, "%08X-FFFFFFFF  %08X\n", low, low == 0 ? -1 : 0 - low);
    puts_map(emsg,1);
    return;
}

/**************************************************************************
 * map_seg_summary - display the segment summary in the map file
 */
static void map_seg_summary( void )
/*
 * At entry:
 *	no special requirements
 * At exit:
 *	map file contains segment listing
 */
{
    int32_t i;
    struct ss_struct *ms,*st,**ls,*grp_nam;
    struct grp_struct *grp_ptr;
    struct seg_spec_struct *seg_ptr,*grp_seg;
    char *fno;

    map_subtitle = 0;        /* no subtitle */
    misc_pool_used += 280;
    map_subtitle = MEM_alloc(280);
    if (qual_tbl[QUAL_OCTAL].present)
    {
        group_control_stringn0 = "%-24.24s %011lo %011lo %011lo %011lo\n";
        group_control_string0  = "%-24.24s %011lo %011lo %011lo\n";
        seg_control_stringn0 = "\t%-16.16s %011lo %011lo %011lo %011lo %06X  %c  %s\n";
        seg_control_string0  = "\t%-16.16s %011lo %011lo %011lo           %06X  %c  %s\n";
        sprintf(map_subtitle,"Section Summary\n\nGroup\tSegment\t\t %s\n%s%s%s\n",
                "   Base       End        Size       MaxLen     Align  c/u File",
                "--------------------------------------------",
                "--------------------------------------------",
                "--------------------------------------------");
    }
    else
    {
        group_control_stringn0 = "%-24.24s %08lX %08lX %08lX %08lX\n";
        group_control_string0  = "%-24.24s %08lX %08lX %08lX\n";
        seg_control_stringn0 = "\t%-16.16s %08lX %08lX %08lX %08lX %04X   %c  %s\n";
        seg_control_string0  = "\t%-16.16s %08lX %08lX %08lX          %04X   %c  %s\n";
        sprintf(map_subtitle,"Section Summary\n\nGroup\tSegment\t\t %s\n%s%s%s\n",
                "  Base     End      Size    MaxLen  Align c/u File",
                "--------------------------------------------",
                "--------------------------------------------",
                "--------------------------------------------");
    }
    if (map_line < 6)
    {      /* respectable amount of room left? */
        puts_map(0l,0);       /* no, skip to top of form */
    }
    else
    {
        puts_map("\n",1);     /* write a blank line */
        puts_map(map_subtitle,0); /* else write the title line */
    }
    grp_ptr = group_list_top;
    while ((ls = (grp_ptr++)->grp_top) != 0)
    {
        if (ls == (struct ss_struct **)-1l)
        {
            --grp_ptr;
            grp_ptr = (struct grp_struct *)(grp_ptr->grp_next);
            continue;
        }
        i = 0;
        while ((st = *ls) != 0)
        {
            if (st == (struct ss_struct *)-2l)
            {
                ++ls;
                continue;
            }
            if (st == (struct ss_struct *)-1l)
            {
                ++ls;
                ls = (struct ss_struct **)*ls;
                continue;
            }
            break;
        }
        if (!st) continue;
        grp_seg = (grp_nam = st->seg_spec->seg_group)->seg_spec;
        while ((st = *ls++) != 0)
        {
            uint32_t nxtbeg;          /* holds the next expected address */
            char *lastSegName,*tmpSegName;

            if (st == (struct ss_struct *)-2l) continue;
            if (st == (struct ss_struct *)-1l)
            {
                ls = (struct ss_struct **)*ls; continue;
            }
            if (map_fp)
            {
                if (!i++)
                {
                    int32_t end = grp_seg->seg_len;
                    end += (end == 0) ? grp_nam->ss_value : grp_nam->ss_value-1;
                    if (grp_seg->seg_maxlen != 0)
                    {
                        sprintf (emsg,group_control_stringn0,
                                 grp_nam->ss_string,
                                 grp_nam->ss_value,
                                 end,
                                 grp_seg->seg_len,
                                 grp_seg->seg_maxlen);
                    }
                    else
                    {
                        sprintf (emsg,group_control_string0,
                                 grp_nam->ss_string,
                                 grp_nam->ss_value,
                                 end,
                                 grp_seg->seg_len);
                    }
                    puts_map(emsg,1);
                }
            }
            ms = st;
/*            s = st->ss_string; */
            lastSegName = "";
            nxtbeg = 0;
            while (1)
            {
                if ((seg_ptr=ms->seg_spec)->seg_len != 0)
                {
                    int32_t lalign,lim;
                    char chr;
                    lalign = 1<<seg_ptr->seg_salign;
                    chr = ms->flg_ovr ? 'c':' ';
                    lim = ms->ss_value+seg_ptr->seg_len-1;
                    if (nxtbeg != 0 && ms->ss_value > nxtbeg)
                    {
                        sprintf(emsg,seg_control_string0,
                                "(skipped)",
                                nxtbeg,
                                ms->ss_value-1,
                                ms->ss_value-nxtbeg,
                                1,
                                ' ',
                                "");
                        puts_map(emsg,1);
                    }
                    nxtbeg = lim+1;
                    if (!ms->ss_fnd)
                    {
                        fno = "";
                    }
                    else if ((fno=ms->ss_fnd->fn_name_only) == 0)
                    {
                        fno = ms->ss_fnd->fn_buff;
                    }
                    if ( lastSegName == ms->ss_string || !strcmp(lastSegName, ms->ss_string) )
                        tmpSegName = "";
                    else
                    {
                        tmpSegName = lastSegName = ms->ss_string;
                    }
                    if (seg_ptr->seg_maxlen != 0)
                    {
                        sprintf(emsg,seg_control_stringn0,
                                tmpSegName,
                                ms->ss_value,
                                lim,
                                seg_ptr->seg_len,
                                seg_ptr->seg_maxlen,
                                (int)lalign,
                                chr,
                                fno);
                    }
                    else
                    {
                        sprintf(emsg,seg_control_string0,
                                tmpSegName,
                                ms->ss_value,
                                lim,
                                seg_ptr->seg_len,
                                (int)lalign,
                                chr,
                                fno);
                    }
                    puts_map(emsg,1);
                }
                if (!ms->flg_more) break;
                ms = ms->ss_next;
            }      /* --while (1) 		*/
        }         /* --while (st= *ls++)	*/
    }            /* --while (ls=grp...	*/
    if (MEM_free(map_subtitle))
    { /* done with this memory */
        err_msg(MSG_WARN,"Error returning 280 bytes from map_subtitle");
    }
    dump_available();        /* display available areas in address space */
    map_subtitle = 0;        /* make sure we don't use freed memory */
}               /* --map_seg_summary	*/

static char *sym_control_string=0;
static char *sym_control_string_xr=0;

/**************************************************************************
 * MAPSYM - create a map and symbol file
 */
void mapsym( void )
/*
 * At entry:
 *	no requirements, called by mainline
 * At exit:
 *	map file opened, written and closed if requested.
 *	sym file opened, written and closed if requested.
 */
{
    struct ss_struct *st,**ls;
    int i,lc,col,line_page;
    char *d,*map_page;
    struct fn_struct **fnp_ptr,*fn_ptr;

    if (map_fp)
    {
        map_seg_summary();
        map_subtitle = "Symbol summary\n\n";
        if (qual_tbl[QUAL_OCTAL].present)
        {
            sym_control_string = "%-14.14s %010lo \n";
            sym_control_string_xr = "%-14.14s %010lo\n";
        }
        else
        {
            sym_control_string = "%-16.16s %08lX \n";
            sym_control_string_xr = "%-16.16s %08lX\n";
        }
        if (map_line < 6)
        {
            puts_map(0l,0);
        }
        else
        {
            puts_map("\n",1);
            puts_map(map_subtitle,2);
        }
        if ((ls = sorted_symbols) != 0)
        { /* point to symbols */
            int map_page_size = 132*60;
            if (!qual_tbl[QUAL_CROSS].present)
            {     /* not cross reference mode */
                misc_pool_used += 132*60;
                map_page = MEM_alloc(132*60);   /* get a whole map page of memory */
                while (1)
                {
                    col = 5;         /* set columns */
                    line_page = map_line;    /* set lines per page */
                    if (((tot_gbl+line_page-1)/line_page) < col)
                    {
                        while (1)
                        {
                            if ((line_page = (tot_gbl+col-1)/col) >= 5) break;
                            if (col == 1) break;
                            --col;
                        }
                    }
                    for (i=0;i<col && *ls;i++)
                    {
                        for (lc=0;lc<line_page && *ls;lc++)
                        {
                            d = map_page + lc*132 + i*26; /* point to destination */
                            while ((st = *ls) != 0)
                            {
                                if (st->flg_defined)
                                {
                                    sprintf(d,sym_control_string,st->ss_string,st->ss_value);
                                    ls++;
                                    --tot_gbl;   /* take from total */
                                    break;
                                }       /* --if 	*/
                                ls++;
                            }          /* --while	*/
                        }         /* --do vertical */
                    }            /* --do horiz	*/
                    for (i=line_page,lc=0;lc<i;lc++)
                    {
                        puts_map(map_page+lc*132,1);
                    }            /* --do write	*/
                    if (!*ls) break;     /* done 	*/
                    puts_map(0l,0);      /* skip to tof to reset map_line */
                }               /* --while (1)	*/
            }
            else
            {           /* -+if cross reference*/
                misc_pool_used += 132+4;
                map_page = MEM_alloc(132+4); /* get a line buffer */
                map_page_size = 132+4;
                while ((st = *ls++) != 0)
                {
                    sprintf(map_page,sym_control_string_xr,st->ss_string,st->ss_value);
                    col = 25;
                    d = map_page + col;  /* point to new line char */
                    if ((fnp_ptr = st->ss_xref) != 0)
                    {
                        i = 0;
                        while ((fn_ptr= *fnp_ptr++) != 0)
                        {
                            if (i++ >= XREF_BLOCK_SIZE - 1)
                            {
                                fnp_ptr = (struct fn_struct **)fn_ptr;
                                i = 0;
                                continue;
                            }
                            if (fn_ptr == (struct fn_struct *)-1l) continue;
                            lc = strlen(fn_ptr->fn_name_only);
                            if (lc+4+col > 131)
                            {
                                puts_map(map_page,1);
                                strcpy(map_page,"                             \n");
                                col = 25;
                                d = map_page + col;
                            }
                            strcpy(d,"    ");
                            strcat(d,fn_ptr->fn_name_only);
                            d += 4+lc;
                            *d = '\n';
                            *(d+1) = 0;
                            col += 4+lc;
                        }         /* --while filenames */
                    }            /* --if filename(s) */
                    puts_map(map_page,1);    /* display the symbol */
                }               /* --while symbols */
            }              /* --if cross reference*/
            if (MEM_free(map_page))
            { /* give back the map page */
                sprintf(emsg,"Error free'ing %d bytes at %p from map_page",
                        map_page_size, (void *)map_page);
                err_msg(MSG_WARN,emsg);
            }
        }                 /* --if symbols */
    }                    /* --if map_fp	*/
    if (tot_udf != 0 && !qual_tbl[QUAL_REL].present)
    {
        if ((ls = sorted_symbols) != 0)
        { /* if any symbols */
            while ((st= *ls++) != 0)
            { /* report undefined */
                if (!st->flg_defined)
                {
                    SS_struct *nsp;
                    char *s, *tstr, *sizep;
                    sizep = strstr(st->ss_string, "_size");
                    if (sizep && strlen(sizep) != 5) sizep = 0;
                    s = tstr = MEM_alloc(st->ss_strlen+2);
                    if (sizep)
                    {
                        strncpy(s, st->ss_string, sizep-st->ss_string);
                        s += sizep-st->ss_string;
                        *s++ = ' ';
                        *s = 0;
                    }
                    else
                    {
                        strcpy(tstr, st->ss_string);
                        strcat(tstr, " ");
                    }
                    nsp = sym_lookup(tstr, st->ss_strlen+1, 0);
                    if (nsp && nsp->flg_segment)
                    {   /* name matches a segment */
                        nsp = nsp->seg_spec->seg_first;   /* point to first one in chain */
                        if (sizep)
                        {          /* he wants the size of the segment */
                            uint32_t start, end;
                            start = nsp->ss_value; /* remember the top */
                            while (nsp->flg_more)
                            {    /* loop through all the segments */
                                nsp = nsp->ss_next; /* point to next one */
                            }
                            end = nsp->ss_value + nsp->seg_spec->seg_len;
                            st->ss_value = end-start;  /* symbol gets length of total */
                        }
                        else
                        {
                            st->ss_value = nsp->ss_value;  /* it gets the segments value */
                        }
                        st->flg_defined = 1;      /* now it's defined */
                        --tot_udf;            /* take from total */
                    }
                    MEM_free(tstr);
                }               /* --if df	*/
            }              /* --while	*/
        }
    }
    if (tot_udf != 0 )
    {         /* if any undefined's */
        if ((ls = sorted_symbols) != 0)
        { /* if any symbols */
            if (map_fp && tot_udf && (!qual_tbl[QUAL_REL].present || qual_tbl[QUAL_ERR].present))
            {
                map_subtitle = "Undefined symbol summary\n\n";
                puts_map("\n",1);       /* skip a line */
                puts_map(map_subtitle,2);   /* title what we're up to */
            }
            while ((st= *ls++) != 0)
            {     /* report undefined */
                if (!st->flg_defined)
                {
                    if (qual_tbl[QUAL_REL].present)
                    {  /* if relative, output reference */
                        outsym_def(st,output_mode); /* external reference */
                    }
                    if (!qual_tbl[QUAL_REL].present || qual_tbl[QUAL_ERR].present)
                    {
                        if (!st->flg_local)
                        {
                            sprintf(emsg,"Undefined symbol: { %s } in file: %s",
                                    st->ss_string,st->ss_fnd->fn_name_only);
                        }
                        else
                        {
                            sprintf(emsg,"Undefined LOCAL symbol: { %s } in file: %s",
                                    st->ss_string,st->ss_fnd->fn_name_only);
                        }
                        err_msg(MSG_WARN,emsg);
                    }            /* --if relative */
                }               /* --if df	*/
            }              /* --while	*/
        }                 /* --if sorted_symbols */
    }                    /* --if udf	*/
    return;
}                   /* --mapsym	*/

static int ht_count;
static int chain_min=65535;
static int chain_max=0;
static int coll=0;
static int ht_coll=0;
static int tot_seg=0;

/********************************************************************
 * Get symbol table statistics. For debugging purposes mainly.
 */
void sym_stats( void )
/*
 * At entry:
 *	no requirements
 * At exit:
 *	symbol table stats printed to stdout
 */
{
    int i,j=0;
    struct ss_struct *st;
    if (debug)
    {
        for (i=0;i<HASH_TABLE_SIZE;i++,j=0)
        {
            if ((st=hash[(int16_t)i]) != 0)
            {
                ++ht_count;         /* a hash table entry */
                tot_seg += st->flg_segment;
                if (st->ss_next) ++ht_coll; /* a collision */
                while ((st = st->ss_next) != 0)
                {
                    tot_seg += st->flg_segment;
                    ++j;
                    ++coll;
                }
                if (chain_max < j) chain_max = j;
                if (j && chain_min > j) chain_min = j;
            }
        }
        sprintf (emsg,"\nTot hash table entries: %d, alpha: .%02d, colls: %d\n",
                 ht_count,ht_count*100/HASH_TABLE_SIZE,coll);
        puts_map(emsg,2);
        sprintf (emsg,"\ttot table collisions: %d, Total chains: %d\n",
                 ht_coll,coll);
        puts_map(emsg,1);
        i = ht_coll ? coll/ht_coll : 0;
        sprintf (emsg,"\tchain_max: %d, chain_min: %d, chain_avg: %d\n",
                 chain_max,i?chain_min:0,i);
        puts_map(emsg,1);
        sprintf (emsg,"\ttotal segments: %d, total global sym entries: %d, glob syms: %d\n",
                 tot_seg,j=ht_count+coll-tot_seg,tot_gbl);
        puts_map(emsg,1);
        sprintf (emsg,"\ttotal local symbols: %d\n",tot_ids-j);
        puts_map(emsg,1);
        sprintf (emsg,"\ttotal ID's used: %d, maximum ID # used : %d\n",
                 tot_ids,max_idu);
        puts_map(emsg,1);
        sprintf (emsg,"\ttotal ID's allocated: %d, total ID's unused: %d\n",
                 id_table_size,id_table_size-tot_ids);
        puts_map(emsg,1);
    }
    return;
}
