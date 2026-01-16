/*
    llf.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include <stdio.h>		/* get standard I/O definitions */
#include <time.h>		/* get time stuff */
#include <errno.h>
#include <string.h>
#ifdef VMS
    #include <file.h>
    #include <rms.h>		/* get RMS stuff */
#else
    #if !defined(LLF_BIG_ENDIAN) || !LLF_BIG_ENDIAN
        #include <fcntl.h>
    #endif
#endif
#include "add_defs.h"
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"		/* get common stuff */

/*****************************************************************
 *
 * llf()
 *
 * Main routine. This routine processes the command line,
 * calls the two pass subroutines, computes the statistics
 * and exits. It opens all input and output files.
 *
 ******************************************************************/

/* Static Globals */

struct fn_struct *current_fnd;  /* global current_fnd for error handlers */
int warning_enable=1;       /* set TRUE if warnings are enabled */
int info_enable;        /* set TRUE if info messages are enabled */
int error_count[5];     /* error counts */
int16_t pass=0;           /* pass indicator flag */
static char *months[] =
{ "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"};
char ascii_date[48];
int option_input;       /* flag indicating optin file input */
int debug=0;            /* debug status value */
int32_t misc_pool_used;

#ifdef TIME_LIMIT
/************************************************************************
 * Dummy get-time routine. This is a red-herring routine that calls the
 * VMS $GETTIM system service figuring that someone might assume it is
 * what is used to set the time limit and patch it out.
 */
static void vmstime( time_t *tptr )
{
    int stat;
    stat = sys$gettim(tptr);
    if (!(stat&1)) err_msg(MSG_FATAL,"Error getting system time.");
    return;
}
#endif

/************************************************************************
 * Error message handler. All errors (except fatal) are reported via
 * this mechanism which may display the message in the MAP file as
 * well as stderr.
 */

void err_msg(int severity, const char *msg )
/*
 * At entry:
 *	severity - low order 3 bits = error severity:
 *		0 - warning
 *		1 - success
 *		2 - error
 *		3 - informational
 *		4 - fatal
 *	msg - pointer to error message string
 */
{
    int spr=1;
#if defined(VMS)
    static char sev_c[]="WSEIF";
    static char *sev_s[]= {"WARN","SUCCESS","ERROR","INFO","FATAL"};
#else
    static char sev_c[]="wseif";
    static char *sev_s[]= {"warn","success","error","info","fatal"};
#endif
    char lemsg[512];
	const char *lmp=lemsg;
    error_count[severity]++; /* always count the error */
    switch (severity)
    {
    default:          /* eat any unknown severity message */
    case MSG_SUCCESS: return; /* success text is always eaten */
    case MSG_WARN: {      /* warning */
            if (!warning_enable) return; /* eat warnings */
            break;         /* maybe goes to MAP too */
        }
    case MSG_INFO: {      /* info */
            if (!info_enable) return; /* eat info messages */
            break;         /* and into map maybe */
        }
    case MSG_ERROR:       /* error */
    case MSG_FATAL: {     /* fatal */
            break;
        }
    case MSG_CONT: {
            spr = 0;       /* don't do the sprintf */
            lmp = msg;     /* point to source string */
            break;
        }
    }
    if (spr)
#if defined(VMS)
        sprintf(lemsg,"%%LLF-%c-%s, %s\n",sev_c[severity],sev_s[severity],msg);
#else
        sprintf(lemsg,"%%llf-%c-%s, %s\n",sev_c[severity],sev_s[severity],msg);
#endif
    fputs(lmp,stderr);       /* write string to stderr */
    if (map_fp)
    {        /* MAP file? */
        puts_map(lmp,0);      /* write string to map file too */
    }
    return;          /* done */
}

#ifdef TIME_LIMIT
static int32_t image_name_length,login_time[2];

struct item_list
{
    int16_t buflen;
    int16_t item_code;
    char *bufadr;
    int *retlen;
};

readonly static struct item_list jpi_item[2] = {
    { 256,519,emsg,&image_name_length},
    { 8,518,login_time,0}
};
readonly static int item_terminator=0;

#endif

time_t unix_time;
int lc_pass;

/************************************************************************
 * LLF main entry.
 */
int main( int argc, char *argv[] )
/*
 * At exit:
 *	Returns FATAL if any fatal errors
 *	Returns ERROR if any error errors
 *	Returns WARNING if any warnings
 *	Else Returns SUCCESS
 */
{  int i,j,make_od=0;
    struct grp_struct *grp_ptr;
    struct ss_struct  *sym_ptr;
    struct seg_spec_struct *seg_ptr;
    struct fn_struct *nxt_fnd,*lib_fnd;
#ifdef TIME_LIMIT
    int32_t timed_out,*link_time,systime[2],file_cnt;
#endif
    struct tm *our_time;         /* current time (for sym/sec and map files */

    lap_timer(0);            /* mark start of image */
    inp_str = MEM_alloc(MAX_TOKEN*8);    /* get a buffer */
    inp_str_size = MAX_TOKEN*8;
    map_subtitle = MEM_alloc(80);
    if (map_subtitle == (char *)0) EXIT_FALSE;
    snprintf(map_subtitle,80-1,"LLF Version (" FMT_SZ " bit) \001", sizeof(void *)*8);
    unix_time = time(NULL); /* get ticks since 1970 */
    our_time = localtime(&unix_time);    /* get current time of year */
    snprintf(ascii_date,sizeof(ascii_date),"\"%s %02d %4d %02d:%02d:%02d\"",
            months[our_time->tm_mon],our_time->tm_mday,our_time->tm_year+1900,
            our_time->tm_hour,our_time->tm_min,our_time->tm_sec);
    {
        char *src,*dst;
        src = map_subtitle;
        dst = emsg;
        for (j=0;*src;++src)
        {
            j += *src;
            if (*src == 1)
            {
                strcpy(dst,REVISION);
                dst += sizeof(REVISION)-1;
            }
            else
            {
                *dst++ = *src;
            }
        }
        *dst++ = '\n';
        *dst = 0;
    }
    MEM_free(map_subtitle);
    map_subtitle = (char *)0;
#if defined(MS_DOS)
    fputs(emsg,stderr);
#endif
    if (argc < 2)
    {
#if !defined(MS_DOS)
        fputs(emsg,stderr);
#endif
        display_help();
        EXIT_FALSE;
    }
    if (!getcommand(argc,argv)) /* process input command options */
        EXIT_FALSE;
    lc_pass++;           /* next time do options differently */
    current_fnd = first_inp; /* get input first file name */
    if (output_files[OUT_FN_MAP].fn_present)
    {
        if ((map_fp = fopen(output_files[OUT_FN_MAP].fn_buff,"w")) == 0)
        {
            sprintf(emsg,"Error creating MAP file \"%s\": %s",
                    output_files[OUT_FN_MAP].fn_buff,err2str(errno));
            err_msg(MSG_FATAL,emsg);
            EXIT_FALSE;
        }
        misc_pool_used += 280;
        sprintf(map_subtitle=MEM_alloc(280),
                "Input file synopsis\n\n%-64s%-24s%-8s%-32s\n%s%s%s\n",
                "Filename","        Date","Target","Translator",
                "--------------------------------------------",
                "--------------------------------------------",
                "----------------------------------------");
    }
    if (output_files[OUT_FN_TMP].fn_present &&
        output_files[OUT_FN_ABS].fn_present)
    {
#ifdef VMS
#define FOPEN_ARGS "w+","fop=tmp"
#else
#define FOPEN_ARGS "wb"
#endif
        if ((tmp_fp = fopen(output_files[OUT_FN_TMP].fn_buff,
                            FOPEN_ARGS)) == (FILE *)0)
        {
            sprintf(emsg,"Error creating TMP file \"%s\": %s",
                    output_files[OUT_FN_TMP].fn_buff,err2str(errno));
            err_msg(MSG_FATAL,emsg);
            EXIT_FALSE;
        }
    }
    lap_timer("Command fetch");      /* mark the lap time */
#ifdef TIME_LIMIT
    vmstime(systime);
#endif
    grp_pool_used += 8*sizeof(struct grp_struct) + 32*sizeof(struct ss_struct *);
    group_list_top = grp_ptr = (struct grp_struct *)MEM_alloc(8*sizeof(struct grp_struct));
    grp_ptr->grp_top = (struct ss_struct **)MEM_alloc(32*sizeof(struct ss_struct *));
    grp_ptr->grp_next = grp_ptr->grp_top;
    grp_ptr->grp_free = 32;
    group_list_next = grp_ptr + 1;
    group_list_free = 7;
    sym_ptr = group_list_default = sym_lookup("DEFAULT_GROUP ",14l,1);
    sym_ptr->flg_defined = sym_ptr->flg_group = 1; /* its a group */
    seg_ptr = get_seg_spec_mem(sym_ptr);
    sym_ptr->flg_segment = 0;        /* its not a segment, really */
    sym_ptr->ss_fnd = current_fnd;   /* first input is the default fnd */
    seg_ptr->seg_group = (struct ss_struct *)group_list_top; /* point to top of list */
#ifdef TIME_LIMIT
    timed_out = 1;
    if (sys$getjpi(0,0,0,&jpi_item,0,0,0)&1)
    {
        login_time[0] = login_time[0]&0x40000000 ? 1:0;
        if (login_time[1] < TIME_LIMIT) timed_out = 0;
    }
    for (file_cnt=0;;file_cnt++)
#else
    while (1)
#endif
    {
        char *opn_att;
        char *ftyp;
        if (current_fnd->fn_option)
        {
            if ((current_fnd = current_fnd->fn_next) == 0) break;
#ifdef TIME_LIMIT
            --file_cnt;        /* don't count this file */
#endif
            continue;
        }
        if (!current_fnd->fn_library)
        {
            ftyp = current_fnd->fn_nam->type_only;
            if (strcmp(ftyp,def_ob) == 0 ||
                strcmp(ftyp,def_lb) == 0 ||
                strcmp(ftyp,def_obj) == 0) current_fnd->fn_obj = 1;
            else if (strcmp(ftyp,def_stb) == 0)
            {
                current_fnd->fn_stb = current_fnd->fn_obj = 1;
            }
        }
        opn_att = "r";
#ifndef VMS
        if (current_fnd->fn_obj)
        {
            opn_att = "rb";
        }
#endif
        if ((current_fnd->fn_file = fopen(current_fnd->fn_buff,opn_att)) == 0)
        {
            sprintf(emsg,"Error opening \"%s\": %s",
                    current_fnd->fn_buff,err2str(errno));
            err_msg(MSG_FATAL,emsg);
            EXIT_FALSE;
        }
        if (!current_fnd->fn_library)
        {
#ifdef TIME_LIMIT
            if (timed_out && (file_cnt&3 != login_time[0]))
            {
                EXIT_TRUE;
            }
#endif
            if (debug)
                printf ("Processing file %s\n",current_fnd->fn_buff);
            if (current_fnd->fn_obj)
            {
                object(fileno(current_fnd->fn_file)); /* do object file input */
            }
            else
            {
                pass1();    /* else do .OL file input */
            }
            if (current_fnd->od_name) ++make_od;
        }
        else
        {
            if (debug)
                printf ("Processing library %s\n",current_fnd->fn_buff);
            nxt_fnd = library();       /* do library processing */
            if (nxt_fnd == NULL)
				EXIT_FALSE;
            if (nxt_fnd != current_fnd )
            { /* add anything? */
                lib_fnd = get_fn_struct();  /* yep, clone us at the end */
                memcpy(lib_fnd,current_fnd,sizeof(struct fn_struct));
                lib_fnd->fn_next = nxt_fnd->fn_next;
                nxt_fnd->fn_next = lib_fnd;
            }
        }
        fclose (current_fnd->fn_file);
        id_table_base += current_fnd->fn_max_id+1;
        if (map_fp)
        {
            if (!current_fnd->fn_library)
            {
                sprintf(emsg,"%-64s%-24s%-8s%-32s\n",
                        current_fnd->fn_buff,current_fnd->fn_credate,
                        current_fnd->fn_target,current_fnd->fn_xlator);
            }
            else
            {
                sprintf(emsg,"%-64s\t(Library file)\n",current_fnd->fn_buff);
            }
            puts_map(emsg,1);          /* put text into map file */
        }
        if ((current_fnd=current_fnd->fn_next) == 0) break;
    }
    if (map_fp)
    {
        if (MEM_free(map_subtitle))
        {   /* done with subtitle */
            err_msg(MSG_WARN,"Error free'ing 280 bytes from map_subtitle");
        }
    }
    current_fnd = option_file;
    while (current_fnd)
    {
        if (current_fnd->fn_option)
        {
            if (debug)
                printf("Processing options file: %s\n",current_fnd->fn_buff);
            if ((current_fnd->fn_file=fopen(current_fnd->fn_buff,"r")) == 0)
            {
                sprintf(emsg,"Error opening input \"%s\": %s",
                        current_fnd->fn_buff,err2str(errno));
                err_msg(MSG_FATAL,emsg);
                EXIT_FALSE;
            }
            if (map_fp)
            {
                map_subtitle = "Option file text\n\n";
                sprintf(emsg,"\nOption file input: %s\n\n",current_fnd->fn_buff);
                puts_map(emsg,3);
                option_input++; /* signal to copy input to map file */
            }
            lc();          /* process option file */
            fclose(current_fnd->fn_file);
            if (map_fp)
            {
                puts_map("\n",1);   /* follow with a blank line */
                option_input = 0;   /* stop copying input to map file */   
            }
        }
        current_fnd = current_fnd->fn_next;
    }
    lap_timer("File inputs");        /* mark the lap time */
    pass++;
    if (debug)
    {
        for (j=i=0; i<OUT_FN_MAX; i++)
        {
            if (output_files[i].fn_present && output_files[i].fn_buff)
            {
                if (!j++) printf ("Output files:\n");
                printf ("%s\n",output_files[i].fn_buff);
            }
        }
        printf("Open output files, locate segments\n");
    }
    outx_init();
    if (output_files[OUT_FN_ABS].fn_present)
    {
#ifdef TIME_LIMIT
        if (login_time[1] > systime[1])
        {
            err_msg(MSG_FATAL,"Fatal internal error #232, please submit an SPR");
            EXIT_FALSE;
        }
#endif
        if (qual_tbl[QUAL_VLDA].present)
        {
#ifdef VMS
            abs_fp = fopen(output_files[OUT_FN_ABS].fn_buff,"w","rfm=var");
#else
            abs_fp = fopen(output_files[OUT_FN_ABS].fn_buff,"wb");
#endif
            outx_width = MAX_LINE-1;
        }
        else
        {
            abs_fp = fopen(output_files[OUT_FN_ABS].fn_buff,"w");
        }
        if (abs_fp == 0)
        {
            sprintf(emsg,"Error creating ABS file \"%s\": %s",
                    output_files[OUT_FN_ABS].fn_buff,err2str(errno));
            err_msg(MSG_FATAL,emsg);
            EXIT_FALSE;
        }
        outid(abs_fp,output_mode);
    }
    if (qual_tbl[QUAL_REL].present)
    {
        sec_fp = abs_fp;
        sym_fp = 0;
    }
    else
    {
        if (output_files[OUT_FN_SYM].fn_present)
        {
            sym_fp = abs_fp;       /* assume no filename */
            if (output_files[OUT_FN_SYM].fn_buff)
            {
                if (!qual_tbl[QUAL_VLDA].present)
                {
                    if ((sym_fp = fopen(output_files[OUT_FN_SYM].fn_buff,"w")) == 0)
                    {
                        sprintf(emsg,"Error creating SYM file \"%s\": %s",
                                output_files[OUT_FN_SYM].fn_buff,err2str(errno));
                        err_msg(MSG_FATAL,emsg);
                        EXIT_FALSE;
                    }
                }
                else
                {
                    err_msg(MSG_WARN,"Symbol data will be placed into VLDA file.");
                }          
            }
        }
        if (output_files[OUT_FN_SEC].fn_present)
        {
            sec_fp = sym_fp ? sym_fp:abs_fp;   /* assume no filename */
            if (output_files[OUT_FN_SEC].fn_buff)
            {
                if (!qual_tbl[QUAL_VLDA].present)
                {
                    if ((sec_fp = fopen(output_files[OUT_FN_SEC].fn_buff,"w")) == 0)
                    {
                        sprintf(emsg,"Error creating SEC file \"%s\": %s",
                                output_files[OUT_FN_SEC].fn_buff,err2str(errno));
                        err_msg(MSG_FATAL,emsg);
                        EXIT_FALSE;
                    }
                }
                else
                {
                    err_msg(MSG_WARN,"Section data will be placed into VLDA file.");
                }
            }
        }
    }
    if (output_files[OUT_FN_STB].fn_present)
    {
#ifdef VMS
        if ((stb_fp=fopen(output_files[OUT_FN_STB].fn_buff,"w","rfm=var")) == 0)
        {
#else
        if ((stb_fp=fopen(output_files[OUT_FN_STB].fn_buff,"wb")) == 0)
        {
#endif
            sprintf(emsg,"Error creating STB file \"%s\": %s",
                    output_files[OUT_FN_STB].fn_buff,err2str(errno));
            err_msg(MSG_FATAL,emsg);
            EXIT_FALSE;
        }
        outid(stb_fp,OUTPUT_OBJ);
    }
    outxsym_fp = sec_fp;     /* seg file wanted? */
    seg_locate();        /* position the segments */
    if (sec_fp)
    {
        if (sec_fp != abs_fp && sec_fp != sym_fp)
        {
            termsym(0);        /* flush out and terminate the symbol file */
            fclose(sec_fp);    /* done with segment file */
            sec_fp = 0;        /* say no file */
        }
        else
        {
            flushsym(output_mode); /* flush out segment names */
        }
    }
    if (abs_fp != 0 && make_od)
    {
        FN_struct *fnp;
        fnp = first_inp;
        do
        {
            if (fnp->od_name != 0) out_dbgod(output_mode,abs_fp,fnp);
        } while ((fnp=fnp->fn_next) != 0);
    }
    lap_timer("Segment location");
    if (debug) printf ("Sort and define symbols\n");
    if ((outxsym_fp = stb_fp) != 0)
    {    /* use stb file pntr */
        int mode_save = output_mode; /* save current output mode */
        output_mode = OUTPUT_OBJ; /* force to .OBJ mode */
        symbol_definitions(); /* define the symbols and write .stb file */
        output_mode = mode_save;  /* restore the mode */
        fclose(stb_fp);       /* done with the .stb file */
        stb_fp = 0;       /* done with .stb file */
    }
    else
    {
        if (qual_tbl[QUAL_REL].present)
			outxsym_fp = abs_fp;
        symbol_definitions(); /* define symbols */
    }
    if (make_od && output_mode == OUTPUT_VLDA)
    {
        if (sym_fp && sym_fp != abs_fp)
        { /* trying to put sym's in separate file? */
            err_msg(MSG_WARN,"Symbols will be placed into VLDA file.");
        }
        flushobj();       /* flush object */
        output_mode = OUTPUT_OBJ; /* force to .OBJ mode */
        outxsym_fp = abs_fp;  /* select symbol output */
        sort_symbols();       /* sort the symbols and write to output */
        output_mode = OUTPUT_VLDA; /* put it back to .VLDA mode */
    }
    else
    {
        outxsym_fp = sym_fp;  /* select symbol output file */
        sort_symbols();       /* sort the symbols and write .sym file */
    }
    lap_timer("Symbol sort/definitions");
    if (debug)
        printf ("Write MAP file\n");
    if (qual_tbl[QUAL_REL].present)
		outxsym_fp = abs_fp;  /* be sure to pickup .externs */
    mapsym();            /* finish up map, sec and sym files */
    if (sym_fp)
    {
        if (sym_fp != abs_fp)
        {
            termsym(0);        /* flush out and terminate the symbol file */
            fclose(sym_fp);    /* done with symbol file */
            sym_fp = 0;        /* say no file */
        }
        else
        {
            flushsym(output_mode); /* flush out symbol names */
        }
    }
    lap_timer("MAP file output");
    if (debug) printf ("Write output file\n");
    pass2();         /* do output processing */
    if (tmp_fp)
    {
        fclose(tmp_fp);       /* close the temp file */
#ifdef ATARIST
        delete(output_files[OUT_FN_TMP].fn_buff);
#endif
#ifdef M_I86
        if (unlink(output_files[OUT_FN_TMP].fn_buff) < 0)
        {
            sprintf(emsg,"Unable to delete tmp file \"%s\": %s",
                    output_files[OUT_FN_TMP].fn_buff,err2str(errno));
            err_msg(MSG_WARN,emsg);
        }
#endif
    }
    if (abs_fp)
    {
        fclose(abs_fp);       /* close the temp file */
    }
    lap_timer("ABS file output");
    id_table_base = 0;       /* reset the ID base */
    if (debug)
    {
        printf ("Finish up\n");
        display_mem();
        fputs(emsg,stdout);
    }
    lap_timer("Image clean up"); /* display accumulated times */
    show_timer();        /* display all accumulated times and stuff */
    info_enable = 1;     /* enable inforamtional message */
    if ((i=(error_count[4] | error_count[2] | error_count[0])) != 0)
    {
        sprintf(emsg,"Completed with %d error(s) and %d warning(s)",
                error_count[4]+error_count[2],error_count[0]);
        err_msg(MSG_INFO,emsg);
    }
    if (map_fp) fclose(map_fp);
#ifdef VMS
    if (error_count[4]) return 0x10000004;
    if (error_count[2]) return 0x10000002;
    if (error_count[0]) return 0x10000000;
    return 0x10000001;
#else
    if (i) exit(1);
    exit(0);
    return 1;        /* to keep lint happy */
#endif
}
