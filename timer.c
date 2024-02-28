/*
    timer.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include <time.h>		/* get time stuff */
#include <string.h>

#ifdef VMS
    #include <jpidef.h>		/* get JPI stuff */
#endif
#include "token.h"		/* define compile time constants */

#include "structs.h"		/* define structures */
#include "header.h"		/* get our stuff */

#if !NO_TIMERS

#if defined(ATARIST) || defined(WIN32)
    #define SIMPLE 1
#endif

#if !defined(M_XENIX) && !defined(M_UNIX)
    #if defined(M_I86)
        #define SIMPLE 1
    #endif
#endif

#if !defined(SIMPLE)
    #define SIMPLE 0
#endif

#define TICKS 100		/* normally 100 clock ticks/second */

#if defined(M_XENIX) || defined(M_UNIX)
    #include <sys/types.h>
    #include <sys/times.h>
    #if defined(M_XENIX)
        #undef TICKS
        #define TICKS 50		/* XENIX has 50 clock ticks/second */
    #endif
#endif

#if 0
extern char *map_subtitle;  /* map subtitle */
extern char emsg[];     /* error message buffer */
extern int debug;
#if !defined(__GNUC__)
extern int strlen(),strcpy();
#endif
#if !defined(VMS)
extern long times();
#endif
extern struct tm *our_time; /* current time */
extern int gc_argc;      /* arg count */
extern char **gc_argv;   /* arg value */
        extern void sym_stats();
#endif

static int item_count;      /* count of times to output */

#if defined(M_XENIX) || defined(M_UNIX)
static short usr_time_elem[4];
static short sys_time_elem[4];
#endif

static short ela_time_elem[4];  /* time broken into elements */

#ifdef VMS
struct jpi_struct
{
    short buf_len;       /* buffer length */
    short jpi_itm;       /* JPI item	*/
    char *buf_ptr;       /* pointer to buffer */
    int *ret_len;        /* pointer to return length */
};

static long curr_time[2];   /* current time */
static short cpu_time_elem[4];  /* time broken into elements */
static long ten_thou = 10000;   /* divisor */

static struct jpi_struct itm_lst[] = {
    {sizeof(int),JPI$_BUFIO,0,0},
    {sizeof(int),JPI$_CPUTIM,0,0},
    {sizeof(int),JPI$_DIRIO,0,0},
    {sizeof(int),JPI$_PAGEFLTS,0,0},
    {sizeof(int),JPI$_PPGCNT,0,0},
    {0,0,0,0}
};

#endif
static long *tim_top;

void display_mem( void )
{
    char *s = emsg;
    strcpy(s,"Memory useage statistics:\n\tfile stuff:\t");
    s += strlen(s);
    sprintf(s,"%ld\n\txref stuff:\t%ld\n\tgroup stuff:\t%ld\n\tsym stuff:\t%ld",
            fn_pool_used,xref_pool_used,grp_pool_used,sym_pool_used);
    s += strlen(s);
    sprintf(s,"\n\ttmp file:\t%ld\n\tsymd file:\t%ld\n\tmisc stuff:\t%ld",
            tmp_pool_used,symdef_pool_used,misc_pool_used+rm_pool_used);
    s += strlen(s);
    sprintf(s,"\n\tID table:\t%ld\n\ttotal used:\t%ld\tpeak used:\t%ld\n",
            id_table_size*sizeof(struct ss_struct *),total_mem_used,peak_mem_used);
    return;
}

static unsigned int tqty;
#if defined(M_XENIX) || defined(M_UNIX)
static struct tms start_time;
#endif
/****************************************************************
 * Lap timer. Record the current runtime stats
 */
void lap_timer( char *strng )
/*
 * At entry:
 *	strng - points to text string used to ID the times
 * At exit:
 *	times recorded in free pool.
 */
{
    int cnt;
    long *vp;
    if (strng && !map_fp) return;    /* nuthin to do if no map file */
#ifdef VMS
    cnt = sizeof(itm_lst)/sizeof(struct jpi_struct)-1    /* all time variables */
          + 1                /* + 1 string pointer */
          + 2;               /* + 2 for elap time  */
#endif
#if SIMPLE
    cnt =  1                 /* string pointer */
           +1;                /* long elapsed time */
#endif
#if defined(M_XENIX) || defined(M_UNIX)
    cnt =  1                 /* string pointer */
           +1             /* long elapsed time */
           +1             /* long user time */
           +1;                /* long sys time */
#endif
    if (item_count >= tqty)
    {
        unsigned int otq = tqty;
        tqty = (item_count/10+1)*10;
        if (tim_top == 0)
        {
            vp = (long *)MEM_calloc(tqty,sizeof(long)*cnt);
        }
        else
        {
            vp = (long *)MEM_realloc(tim_top,(unsigned)(sizeof(long)*tqty*cnt));
            misc_pool_used -= otq*cnt*sizeof(long);
        }
        misc_pool_used += tqty*cnt*sizeof(long);
        tim_top = vp;
    }
    vp = item_count*cnt + tim_top;
    *(char **)vp++ = strng;      /* ... say where the string is */
#ifdef VMS
    sys$gettim(curr_time);       /* get current time of day */
    itm_lst[0].buf_ptr = vp++;       /* bufio 	*/
    itm_lst[1].buf_ptr = vp++;       /* cputim 	*/   
    itm_lst[2].buf_ptr = vp++;       /* dirio	*/   
    itm_lst[3].buf_ptr = vp++;       /* pageflts 	*/   
    itm_lst[4].buf_ptr = vp++;       /* ppgcnt 	*/   
    *vp++ = curr_time[0];        /* pass elapsed time low  */
    *vp++ = curr_time[1];        /* pass elapsed time high */
    if (!(sys$getjpi(0,0,0,itm_lst,0,0,0)&1))
    {
        err_msg(2,"Error getting JPI stats");
    }
#endif
#if SIMPLE
    *vp++ = time((long *)0);         /* get current time in seconds */
#endif
#if defined(M_XENIX) || defined(M_UNIX)
    *vp++ = times(&start_time);      /* get current elapsed time */
    *vp++ = start_time.tms_utime;    /* user time */
    *vp++ = start_time.tms_stime;    /* sys time */
#endif
    ++item_count;            /* count it */
    return;
}

#ifdef VMS
com_time(out,in)
short *out;
long in;
{
    *out++ = in/360000;      /* hours */
    *out++ = (in % 360000)/6000; /* minutes */
    *out++ = (in % 6000)/100;    /* seconds */
    *out   = (in % 100);     /* hundredths */
    return;
}
#endif

#if defined(M_XENIX) || defined(M_UNIX)
void com_time( short *out, long in )
{
    *out++ =  in/(3600*TICKS);       /* hours */
    *out++ = (in % (3600*TICKS))/(60*TICKS); /* minutes */
    *out++ = (in % (60*TICKS))/TICKS;    /* seconds */
    *out   = (in % TICKS)*(100/TICKS);   /* hundredths */
    return;
}
#endif

#if SIMPLE
    #ifdef ATARIST
void com_time( short *out, struct gemdos_date *curr, gemdos_date *last)
{
    int hour,min,sec;
    struct tm *tm;
    hour = curr->hour;
    min = curr->min;
    sec = curr->sec;
    sec -= last->sec;
    if (sec < 0)
    {       /* seconds */
        min -= 1;         /* take 1 from minutes */
        sec += 30;        /* and up the secs */
    }
    min -= last->min;
    if (min < 0)
    {       /* minutes */
        hour -= 1;        /* take 1 from hours */
        min += 60;        /* and up the mins */
    }
    hour -= last->hour;
    if (hour < 0)
    {      /* hours */
        hour += 24;       /* up the hours */
    }
    *out++ = hour;
    *out++ = min;
    *out = sec*2;        /* seconds is in 2 second intervals */
    return;
}

    #else				/* not ATARIST, must be MSDOS */

void com_time(short *out, long *curr, long *last)
{
    int hour,min,sec;
    struct tm *curr_time;               /* current time */
    curr_time = localtime(curr);        /* get current time of year */
    hour = curr_time->tm_hour;
    min = curr_time->tm_min;
    sec = curr_time->tm_sec;
    curr_time = localtime(last);        /* get last time */
    hour -= curr_time->tm_hour;
    min  -= curr_time->tm_min;
    sec  -= curr_time->tm_sec;
    if (sec < 0)
    {
        sec += 60;
        min -= 1;
    }
    if (min < 0)
    {
        min += 60;
        hour -= 1;
    }
    *out++ =  hour;
    *out++ =  min;
    *out++ =  sec;
    *out   =  0;             /* hundredths */
    return;
}
    #endif
#endif

#endif	/* !NO_TIMERS */

/****************************************************************************
 * Show times accumulated.
 */
void show_timer( void )
/*
 * At entry:
 *	assumed to have done one or more lap_timer() calls.
 * At exit:
 *	times displayed in map file
 */
{
	char *str;
#if !NO_TIMERS
    long *p,*lp=0,*cp,itc;
#if defined(VMS) || defined(M_XENIX) || defined(M_UNIX)
    long bufio,cputim,dirio;
#endif
#ifdef VMS
    long pageflts,ppgcnt,maxpp;
    maxpp = 0;
#endif
#endif	/* !NO_TIMERS */
    if (!map_fp) return;     /* nuthin to do if no map file */
#if !NO_TIMERS
	if ((p=tim_top) == 0) return;    /* haven't done any lap_timer's */
#endif	/* !NO_TIMERS */
    map_subtitle=MEM_alloc(280); /* get some memory */
    misc_pool_used += 280;
#if !NO_TIMERS
#ifdef VMS
    sprintf(map_subtitle,
            "Run time statistics\n\n%-24s %6s %6s %6s %6s %11s   %11s\n%s%s\n",
            "Procedure","BIO","DIO","MEM","PGFLT","CPU time","Elapsed",
            "----------------------------------------------------------",
            "-------------------");
#endif
#if SIMPLE
    sprintf(map_subtitle,
            "Run time statistics\n\n%-24s %11s\n%s\n",
            "Procedure","Elapsed time",
            "-------------------------------------");
#endif
#if defined(M_XENIX) || defined(M_UNIX)
    sprintf(map_subtitle,
            "Run time statistics\n\n%-24s     %s\n%s%s\n",
            "Procedure","System time    User time   Elapsed time",
            "-------------------------------------",
            "-------------------------------");
#endif
    if (map_line < 8+item_count)
    {
        puts_map(0l,0);       /* skip to top of form */
    }
    else
    {
        puts_map("\n",1);     /* one blank line */
        puts_map(map_subtitle,0); /* write subtitle */
    }
    for (itc=0;itc<item_count;itc++)
    {
        cp = p;           /* remember our place */
        if ((str = *(char **)p++) != (char *)0)
        { /* point to string */
#ifdef VMS
            bufio = *p++;
            cputim = *p++;
            dirio = *p++;
            pageflts = *p++;
            ppgcnt = *p++;
            if (ppgcnt > maxpp) maxpp = ppgcnt;
            bufio -= *lp++;
            cputim -= *lp++;
            dirio -= *lp++;
            pageflts -= *lp++;
            lp++;          /* point to previous elapsed time */
            dsubscale(lp,p,&curr_time[0],100000);
            com_time(ela_time_elem,curr_time[0]);
            com_time(cpu_time_elem,cputim);    /* compute cpu time */
            sprintf(emsg,"%-24s %6d %6d %6d %6d %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d\n",
                    str,bufio,dirio,ppgcnt,pageflts,
                    cpu_time_elem[0],cpu_time_elem[1],cpu_time_elem[2],cpu_time_elem[3],
                    ela_time_elem[0],ela_time_elem[1],ela_time_elem[2],ela_time_elem[3]);
#endif
#if SIMPLE
            com_time(ela_time_elem,p,lp);
            sprintf(emsg,"%-24s     %02d:%02d:%02d\n",str,
                    ela_time_elem[0],ela_time_elem[1],ela_time_elem[2]);
#endif
#if defined(M_XENIX) || defined(M_UNIX)
            bufio = *p++;      /* actually elapsed time */
            dirio = *p++;      /* actually usr time */
            cputim = *p++;     /* actually sys time */
            bufio -= *lp++;    /* compute differences */
            dirio -= *lp++;
            cputim -= *lp++;
            com_time(ela_time_elem,bufio);
            com_time(usr_time_elem,dirio);
            com_time(sys_time_elem,cputim);
            sprintf(emsg,"%-24s     %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d\n",
                    str,
                    sys_time_elem[0],sys_time_elem[1],sys_time_elem[2],sys_time_elem[3],
                    usr_time_elem[0],usr_time_elem[1],usr_time_elem[2],usr_time_elem[3],
                    ela_time_elem[0],ela_time_elem[1],ela_time_elem[2],ela_time_elem[3]);
#endif
            puts_map(emsg,1);  /* display the totals */
        }
#ifdef VMS
        p = cp + sizeof(itm_lst)/sizeof(struct jpi_struct)-1  /* all time variables */
            + 1                 /* + 1 string pointer */
            + 2;                /* + 2 for elap time  */
#endif
#if SIMPLE
        p = cp+2;
#endif
#if defined(M_XENIX) || defined(M_UNIX)
        p = cp + 1            /* string pointer */
            +1             /* long elapsed time */
            +1             /* long user time */
            +1;                /* long sys time */
#endif
        lp = cp+1;                /* remember this place */
    }
    p = lp;          /* point back to last one */
    lp = tim_top+1;      /* point to first one */
#ifdef VMS
    puts_map("-----------------------------------------------------------------------------\n",1);
    bufio = *p++ - *lp++;
    cputim = *p++ - *lp++;
    dirio = *p++ - *lp++;
    pageflts = *p++ - *lp++;
    p++;             /* skip last ppgcnt */
    lp++;            /* skip first ppgcnt */
    dsubscale(lp,p,&curr_time[0],100000);
    com_time(ela_time_elem,curr_time[0]);
    com_time(cpu_time_elem,cputim);  /* compute cpu time */
    sprintf(emsg,"%-24s %6d %6d %6d %6d %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d\n\n",
            "Runtime totals",bufio,dirio,maxpp,pageflts,
            cpu_time_elem[0],cpu_time_elem[1],cpu_time_elem[2],cpu_time_elem[3],
            ela_time_elem[0],ela_time_elem[1],ela_time_elem[2],ela_time_elem[3]);
    puts_map(emsg,1);        /* display the totals */
#endif
#if SIMPLE
    puts_map("-------------------------------------\n",1);
    com_time(ela_time_elem,p,lp);
    sprintf(emsg,"%-24s     %02d:%02d:%02d\n\n","Runtime total",
            ela_time_elem[0],ela_time_elem[1],ela_time_elem[2]);
    puts_map(emsg,1);        /* display the totals */
#endif
#if defined(M_XENIX) || defined(M_UNIX)
    puts_map("--------------------------------------------------------------------\n",1);
    bufio = *p++ - *lp++;    /* elapsed time */
    dirio = *p++ - *lp++;    /* user time */
    cputim = *p++ - *lp++;   /* sys time */
    com_time(ela_time_elem,bufio);
    com_time(usr_time_elem,dirio);
    com_time(sys_time_elem,cputim);
    sprintf(emsg,"%-24s     %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d   %02d:%02d:%02d.%02d\n",
            "Runtime totals",
            sys_time_elem[0],sys_time_elem[1],sys_time_elem[2],sys_time_elem[3],
            usr_time_elem[0],usr_time_elem[1],usr_time_elem[2],usr_time_elem[3],
            ela_time_elem[0],ela_time_elem[1],ela_time_elem[2],ela_time_elem[3]);
    puts_map(emsg,1);        /* display the totals */
#endif
    if (debug)
    {
        strcpy(map_subtitle,"Misc. statistics\n");
        sprintf (emsg,"\nA total of %ld ol records processed.\nA total of %ld obj records processed.\n",
                 record_count,object_count);
        puts_map(emsg,3);
        if (map_line < 15)
        {
            puts_map(0l,0);        /* skip to top of form */
        }
        display_mem();
        puts_map(emsg,11);
        sym_stats();      /* display symbol table stats */
    }
#endif	/* !NO_TIMERS */
    strcpy(map_subtitle,"Command line input:\n\n");
    if (map_line < 5)
    {
        puts_map(0l,0);       /* skip to top of form */
    }
    else
    {
        puts_map("\n",1);     /* write a blank line */
        puts_map(map_subtitle,2); /* write subtitle */
    }
    while (1)
    {
        char **argv,*s;
        int i = gc_argc,emlen,arglen;
        argv = gc_argv;
        s = *++argv;      /* get pointer to first argument */
        while (1)
        {
            str = emsg;        /* point to beginning of line */
            emlen = 0;     /* length starts at 0 */
            while ((arglen=strlen(s))+emlen < 128)
            {
                strcpy(str,s);
                str += arglen;
                *str++ = ' ';
                emlen += arglen +1;
                if (--i > 1)
                {
                    s = *++argv;
                    continue;
                }
                else
                {
                    break;
                }
            }
            if (i <= 1)
            {
                break;
            }
            else
            {
                char *estrt,*astrt,c;
                int newlen = emlen;
                estrt = str; astrt = s;
                while (newlen < 128)
                {
                    if ((c = *str = *s) == 0) break;
                    if (c == '/' || c == ',')
                    {
                        int offset;
                        offset = (c == ',') ? 1:0;
                        estrt = str+offset;
                        astrt = s+offset;
                    }
                    str++;   
                    s++;
                    newlen++;
                }
                *estrt++ = '-';
                *estrt++ = '\n';
                *estrt = 0;
                puts_map(emsg,1);
                s = astrt;
                continue;
            }
        }
        *str++ = '\n';
        *str++ = 0;
        puts_map(emsg,1);
        break;
    }
    return;
}

