/*
    gc.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

/**************************************************************************
 *
 * This module (wholly) contains the code to obtain the command string from
 * the user. The calling convention:
 *
 * if (!getcommand()) exit(FALSE);
 *
 * The routine returns a TRUE/FALSE flag indicating success/failure.
 * The output
 * filename data structures are located contigiously in memory. The input
 * filename data structures may or may not be located contigiously in memory and
 * there will be a variable number of them. You must use the .next pointer to
 * chain through them. There are 5 output files available.
 * 
 * ABSOLUTE - the main output file, absolute format either binary (VLDA)
 *	or ascii (tekHEX), relative format either binary (.OBJ) or ascii (.OL)
 * MAP - The map file. This file contains the link synopsis in a printable 
 * 	form.
 * SECTION - This file contains a loadable format data that specifies limits 
 * 	of sections. To be used by development systems as indications of
 *	program limits for dissassembly, etc. Output format is TEKhex only.
 * SYMBOL - This is a global symbol file in a loadable data format. Used by
 * 	development systems.
 * STB - This is a global symbol file suitable for re-linking. (.OBJ) format
 *	only.
 *
 * The remaining command qualifiers are: 
 * 
 * /CROSS - display a cross reference of section and symbol names in the
 * 	link map.
 * /DEBUG - displays various debug messages during the link procedure
 * /OCTAL - causes all values displayed in error messages and on the link
 *	map to be printed in octal.
 * /OBJECT - causes LLF to use .OB as the default input filename type
 * /VLDA - causes the output file to be written as binary
 * /RELATIVE - causes the output file to be written in relative format
 * /ERROR - forces the normally suppressed undefined global symbol 
 *	error messages to be displayed if outputing a relative file.
 * /LIBRARY - specifies that the filename preceeding the option is
 *	a library file.
 * /OPTIONS - specifies that the filename preceeding the option is an 
 *	option file (only 1 allowed)
 * 
 ***********************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "token.h"
#include "structs.h"		/* get the cli options structures */
#include "header.h"
#include "add_defs.h"
#include "gcstruct.h"
#include "version.h"

#undef NULL
#define NULL 0

#if !defined(VMS) && !defined(MS_DOS)
    #define OPT_DELIM '-'		/* UNIX(tm) uses dashes as option delimiters */
#else
    #define OPT_DELIM '/'		/* everybody else uses slash */
#endif

#if defined(LLF_BIG_ENDIAN) && LLF_BIG_ENDIAN
    #undef QUAL_VLDA
    #undef QUAL_OBJ
    #define QUAL_VLDA 0
    #define QUAL_OBJ  0
#endif
#ifdef _toupper
    #undef _toupper
#endif
#ifdef _tolower
    #undef _tolower
#endif
#define _toupper(c)	(((c) >= 'a' && (c) <= 'z') ? (c) & 0x5F:(c))
#define _tolower(c)	(((c) >= 'A' && (c) <= 'Z') ? (c) | 0x20:(c))

static struct cli_options cli_qualifiers;

struct cli_options *options = &cli_qualifiers;

/*****************************************************************************
 * The following are the command line options to be obtained from user.
 * The OPTIONS entity and LIBRARY are local to a specific file.
 * There can be only 1 OPTION file. Its fn_struct pointer is 
 * saved in option_file and passed back to the caller. The LIBRARY option
 * simply sets a bit in the fn_struct of the file on which it appears.
 *****************************************************************************/

#define opt_desc	qual_tbl[0]
#define lib_desc	qual_tbl[1]
#define xref_desc	qual_tbl[2]
#define rel_desc	qual_tbl[3]
#define err_desc	qual_tbl[4]
#define vlda_desc	qual_tbl[5]
#define obj_desc	qual_tbl[6]
#define oct_desc	qual_tbl[7]
#define deb_desc	qual_tbl[8]
#define tmp_desc	qual_tbl[9]
#define abs_desc	qual_tbl[10]
#define sym_desc	qual_tbl[11]
#define map_desc	qual_tbl[12]
#define sec_desc	qual_tbl[13]
#define stb_desc	qual_tbl[14]
#define bin_desc	qual_tbl[15]
#define msr_desc	qual_tbl[16]

#ifdef VMS
    #define FILENAME_LEN 256	/* maximum length of filename in chars	*/
#else
    #define FILENAME_LEN 64		/* maximum length of filename in chars	*/
#endif

struct fn_struct *option_file=0;    /* where the option file pointer is kept   */
struct fn_struct output_files[OUT_FN_MAX]; /* declare space to keep output file specs */
struct fn_struct *next_inp=0;   /* pointer to next available file pointer */
struct fn_struct *last_inp=0;   /* pointer to last fnd used */
struct fn_struct *first_inp=0;  /* pointer to first fnd used */
struct fn_struct *fn_struct_pool=0; /* pointer to fn_struct pool */
int fn_struct_poolsize=0;   /* number of structs left in the pool */
short output_mode;      /* output mode */

char *def_opt_ptr[] = { def_opt,0};
char *def_obj_ptr[] = { 0,0,0};
char *def_lib_ptr[] = { def_lib,0};
char *def_out_ptr[] = { 0,0};

static struct
{
    struct qual *qdesc;
    char *defext;
} fn_ptrs[] = {
    {&abs_desc,0},       /* /OUTPUT=filename */
    {&map_desc,def_map},     /* /MAP=filename */
    {&stb_desc,def_stb},     /* /STB=filename */
    {&sym_desc,def_sym},     /* /SYMBOL=filename */
    {&sec_desc,def_sec},     /* /SECTION=filename */
    {&tmp_desc,def_tmp}      /* /TEMPFILE=directory */
};

static char *oft[] = {
    def_hex,         /* absolute ascii = tekhex */
    def_ln,          /* relative ascii = .LN */
    def_vlda,            /* absolute binary = .VLDA */
    def_lb,          /* relative binary = .LB */
    0
};

/* The filenames are stored in free memory called fn_pool. The pool is */
/* managed by the fn_init routine by way of the following two variables */

int fn_pool_size=0;     /* records amount remaining in free pool */
char *fn_pool=0;        /* points to next char in free pool	 */
long fn_pool_used;
long xref_pool_used;

/***************************************************************************
 * FUNCTION fn_init:
 * This function initialises the fn_struct structure. It also adjusts the free
 * pool size if necessary.
 * The only required parameter is the pointer to the structure to be init'd
 * It always returns TRUE
 ***************************************************************************/

char *null_string="";   /* empty string */

void get_fn_pool( void )
{
    if (fn_pool_size < 2*FILENAME_LEN)
    { /* to save CPU time, the free pool */
        fn_pool_size = 1024;      /* a bunch of memory */
        fn_pool = (char *)MEM_alloc(fn_pool_size); /* the size required. It is assumed */
        fn_pool_used += fn_pool_size;
    }                    /* that most filenames will be short */
    return;
}

static struct fn_struct **xref_pool=0;  /* pointer to xref pool */
static int xref_pool_size=0;        /* size of xref free pool */

struct fn_struct **get_xref_pool( void )
{
    struct fn_struct **fn_ptr;
    if (xref_pool_size < XREF_BLOCK_SIZE)
    {
        int t;
        xref_pool_size = XREF_BLOCK_SIZE*256/sizeof(struct fn_struct **);
        t = xref_pool_size*sizeof(struct fn_struct **);
        xref_pool = (struct fn_struct **)MEM_alloc(t);
        xref_pool_used += t;
    }
    xref_pool_size -= XREF_BLOCK_SIZE;   /* dish them out n at a time */
    fn_ptr = xref_pool;
    xref_pool += XREF_BLOCK_SIZE;
    return(fn_ptr);
}

int fn_init( FN_struct *pointer )
{
    get_fn_pool();           /* get some free space in the pool */
#ifdef VMS
    pointer->d_length = FILENAME_LEN;    /* area length */
    pointer->s_type = DSC$K_DTYPE_T; /* string type (text) */
    pointer->s_class = DSC$K_CLASS_S;    /* string class (string) */
#endif
    pointer->fn_buff = fn_pool;      /* pointer to buffer */
    pointer->fn_target = null_string;    /* no target initially */
    pointer->fn_xlator = null_string;    /* no xlator initially */
    pointer->fn_credate = null_string;   /* no creation date initially */
    pointer->fn_name_only = null_string; /* assume no filename */
    pointer->fn_next = 0;        /* pointer to next structure */
    pointer->fn_present = 0;     /* flag indicating filename present */
    pointer->fn_library = 0;     /* flag indicating file is library */
    pointer->fn_gotit = 0;       /* flag indicating file picked from lib */
    *fn_pool = 0;            /* make sure array starts with 0 */
    return(TRUE);            /* always return TRUE */
}

/**************************************************************************
 * Get a file descriptor structure.
 */
FN_struct *get_fn_struct( void )
/* At entry:
 * 	no requirements
 * At exit:
 *	returns pointer to blank file descriptor structure init'd
 */
{
    if (fn_struct_poolsize <= 0)
    {   /* get some memory to hold the structs */
        int t=128;
        fn_struct_pool = 
        (struct fn_struct *)MEM_alloc(t*sizeof(struct fn_struct));
        fn_struct_poolsize = t;
        fn_pool_used += t*sizeof(struct fn_struct);
    }
    fn_init(fn_struct_pool);
    fn_struct_pool->fn_nam = 0;
    --fn_struct_poolsize;
    return(fn_struct_pool++);
}

static int gc_err;

static char *do_option(char *str)
{
    char *s = str+1; /* eat the "/" character */
    char c,c1,loc[6],*lp=loc;    /* make room for local copy of string */
    int cnt,lc,neg=0,got_value=0;
    c = *s;
    c1 = *(s+1);
    if (_toupper(c) == 'N' && _toupper(c1) == 'O')
    {
        neg = 1;
        s += 2;
    }
    for (cnt = 0; cnt < sizeof(loc)-1; cnt++)
    {
        c = *s++;
        *lp++ = c = _toupper(c); /* convert the local copy to uppercase */
        if (c == 0 || c == OPT_DELIM || c == ' ' ||
            c == ',' || c == '=' || c == '\t')
        {
            --s;
            --lp;
            break;         /* out of for */
        }
    }        
    *lp = 0;         /* terminate our string */
    if (cnt == 0)
    {
        gc_err++;         /* signal an error occured */
        sprintf(emsg,"No qualifier present: {%s}",str);
        err_msg(MSG_FATAL,emsg);
    }
    else
    {
        for (lc = 0; lc < max_qual; lc++)
        {
            if (strncmp(qual_tbl[lc].string,loc,cnt) == 0) break;
        }
        if (neg != 0)
        {
            if (lc < max_qual)
            {
                if (!qual_tbl[lc].negate)
                {
                    sprintf(emsg,"Option {%c%s} is not negatable.",OPT_DELIM,loc);
                    err_msg(MSG_ERROR,emsg);
                    gc_err++;
                }
                else
                {
                    qual_tbl[lc].negated = 1;
                }
            }
        }
        while (1)
        {
            c = *s++;      /* pickup char */
            if (c == 0 || c == OPT_DELIM || c == ' ' ||
                c == ',' || c == '\t')
            {
                --s;
                break;      /* out of while */
            }
            if (c == '=')
            {        /* value is a special case */
                if (lc < max_qual)
                {
                    if (qual_tbl[lc].negated)
                    {
                        sprintf(emsg,"No value allowed on negated option {%c%s}.",OPT_DELIM,loc);
                        err_msg(MSG_ERROR,emsg);
                        gc_err++;
                        continue;
                    }
                    if (!qual_tbl[lc].noval)
                    {
                        if (qual_tbl[lc].value == 0)
                        {
                            char *beg = s;
                            if (qual_tbl[lc].number)
                            {
                                char *end;
                                while (isdigit(*s)) ++s;
                                qual_tbl[lc].value = (char *)strtol(s, &end, 0);
                                if (s-beg == 0 || *end != 0)
                                {
                                    sprintf(emsg,"Value {%s} on {%c%s} must be number.",
                                            beg,OPT_DELIM,loc);
                                    err_msg(MSG_ERROR,emsg);
                                    gc_err++;
                                }
                            }
                            else
                            {
                                while ((c = *s) && c != OPT_DELIM && c != ' ' &&
                                       c != ',' && c != '\t') ++s;
                                qual_tbl[lc].value = MEM_alloc(s-beg+2);
                                strncpy(qual_tbl[lc].value,beg,s-beg);
                                qual_tbl[lc].value[s-beg] = 0;
                            }          
                            ++got_value;
                        }
                        else
                        {
                            sprintf(emsg,"Value already specified on option {%c%s}",
                                    OPT_DELIM,loc);
                            err_msg(MSG_WARN,emsg);
                            ++gc_err;
                        }
                    }
                    else
                    {
                        sprintf(emsg,"Value not allowed on option {%c%s}",
                                OPT_DELIM,loc);
                        err_msg(MSG_ERROR,emsg);
                        gc_err++;
                    }
                }
            }
        }     
        if (lc >= max_qual )
        {
            gc_err++;
            sprintf(emsg,"Option {%c%s} not defined",OPT_DELIM,loc);
            err_msg(MSG_FATAL,emsg);
        }
        else
        {
            if (!got_value && !qual_tbl[lc].noval && !qual_tbl[lc].optional)
            {
                sprintf(emsg,"Value required on option {%c%s}",OPT_DELIM,loc);
                err_msg(MSG_ERROR,emsg);
                gc_err++;
            }
            else
            {
                qual_tbl[lc].present = 1;
                if (!qual_tbl[lc].negated)
                {
                    unsigned char *q;
                    q = (unsigned char *)options;
                    q[qual_tbl[lc].qualif] = 1;
                }       /* -- if negated	*/
            }      /* -- if got_value	*/
        }         /* -- if lc >= MAX	*/
    }            /* -- if cnt		*/
    return s;
}           /* -- do_option		*/

#if 0
static char temp_filename[] = "LLFXXXXXX";
#endif

int getcommand( void )
{
    char c,*s;
    int i,rms_errors=0;
    struct fn_struct *fnd;
    char **argv;

    argv = gc_argv;
    s = *++argv;
    for (i=1; i < gc_argc;i++,s= *++argv)
    {
        while ((c = *s) != 0)
        {
            switch (c)
            {
            case OPT_DELIM: {
                    s = do_option(s);
                    if (options->library)
                    {
                        last_inp = next_inp;      /* record the pointer to this one */
                        next_inp = get_fn_struct();   /* get a fn structure */
                        if (last_inp) last_inp->fn_next = next_inp; /* link last one to this one */
                        if (!first_inp) first_inp = next_inp;     /* record the first structure addr */
                        next_inp->fn_buff = lib_desc.value;
                        next_inp->fn_library = 1;
                        lib_desc.value = 0;
                        options->library = 0;
                    }
                    else if (options->option)
                    {
                        last_inp = next_inp;      /* record the pointer to this one */
                        next_inp = get_fn_struct();   /* get a fn structure */
                        if (last_inp) last_inp->fn_next = next_inp; /* link last one to this one */
                        next_inp->fn_buff = opt_desc.value;
                        next_inp->fn_option = 1;
                        if (option_file == 0) option_file = next_inp;
                        opt_desc.value = 0;
                        options->option = 0;
                    }
                    continue;
                }
            case ' ':       /* space */
            case '\t':      /* tab */
            case ',': {     /* comma */
                    s++;     /* means skip it */
                    continue;
                }
            default: {      /* everything else is a filename */
                    int len;
                    char *beg;
                    last_inp = next_inp;     /* record the pointer to this one */
                    next_inp = get_fn_struct();  /* get a fn structure */
                    if (last_inp) last_inp->fn_next = next_inp; /* link last one to this one */
                    if (!first_inp) first_inp = next_inp;    /* record the first structure addr */
                    beg = s;
                    while (1)
                    {
                        switch (*s++)
                        {   /* all chars between these are filename */
                        case OPT_DELIM:
                        case ' ':
                        case '\t':
                        case ',':
                        case NULL: break;  /* end of string */
                        default: continue;
                        }
                        --s;
                        break;
                    }
                    len = s-beg;
                    if (len > 0)
                    {
                        next_inp->fn_buff = MEM_alloc(len+1);
                        strncpy(next_inp->fn_buff,beg,len);
                        next_inp->fn_buff[len] = 0;
                    }
                    else
                    {
                        next_inp->fn_buff = "";
                    }
                    next_inp->r_length = len;
                    continue;
                }                   /* --default */
            }              /* --switch(*s) */
        }                 /* --while() */
    }                    /* --for() */
    if (gc_err) return FALSE;        /* don't do anything if option errors */
    if (deb_desc.present)
    {
        if ((debug = (int)deb_desc.value) == 0) debug++; /* debug defaults to 1 */
    }
    fnd = option_file;
    while ((current_fnd = fnd) != 0)
    {       /* make option file current */
        if (fnd->fn_option)
        {
            if (add_defs(fnd->fn_buff,def_opt_ptr,(char **)0,ADD_DEFS_INPUT,&fnd->fn_nam))
            {
                rms_errors++;
                sprintf(emsg,"Unable to parse option filename \"%s\": %s",
                        fnd->fn_nam?fnd->fn_nam->full_name:fnd->fn_buff,
                        err2str(errno));
                err_msg(MSG_FATAL,emsg);
            }
            else
            {
                fnd->fn_buff = fnd->fn_nam->full_name;
                fnd->fn_name_only = fnd->fn_nam->name_type;
                if (debug)          /* announce */
                    printf("Processing options file: %s\n",fnd->fn_buff);
                if ((fnd->fn_file=fopen(fnd->fn_buff,"r")) == 0)
                {
                    sprintf(emsg,"Unable to open option file %s for input: %s",
                            fnd->fn_buff,err2str(errno));
                    err_msg(MSG_FATAL,emsg);
                    rms_errors++;
                }
                else
                {
                    lc();            /* add any files and libraries */
                    fclose(fnd->fn_file);
                }
            }
        }
        fnd = fnd->fn_next;
    }
    if ((fnd = first_inp) == 0)
    {
        sprintf(emsg,"No files input.\n");
        err_msg(MSG_FATAL,emsg);
        return FALSE;
    }
    if (options->cross) map_desc.present = 1;    /* cross defaults to /MAP */
#if 0
#ifndef VMS
    if (!msr_desc.negated) options->miser = 1;   /* default to miser mode */
#endif
#endif
    if (options->rel)
    {
        i = 0;
        if (sym_desc.present && !sym_desc.negated) i++;
        if (stb_desc.present && !stb_desc.negated) i++;
        if (i)
        {
#if defined(VMS) || defined(MS_DOS)
            err_msg(MSG_FATAL,"/STB amd /SYM are not allowed with /REL");
#else
            err_msg(MSG_FATAL,"-stb and -sym are note allowed with -rel");
#endif
            rms_errors += i;
        }
    }
    if (options->objdef != 0)
    {
        def_obj_ptr[0] = def_obj; /* optional default file type */
        def_obj_ptr[1] = def_ol;  /* assume normal input file */
    }
    else
    {
        def_obj_ptr[1] = def_obj; /* optional default file type */
        def_obj_ptr[0] = def_ol;  /* assume normal input file */
    }
    while (fnd != 0)
    {
        if (!fnd->fn_option)
        {
            char **dfs;
            if (fnd->fn_library)
            {
                dfs = def_lib_ptr;
            }
            else
            {
                dfs = def_obj_ptr;
            }
            if (add_defs(fnd->fn_buff,dfs,(char **)0,ADD_DEFS_INPUT,&fnd->fn_nam) != 0)
            {
                ++rms_errors;
                sprintf(emsg,"Unable to parse %s filename \"%s\": %s",
                        fnd->fn_library?"library":"input",
                        fnd->fn_nam?fnd->fn_nam->full_name:fnd->fn_buff,
                        err2str(errno));
                err_msg(MSG_FATAL,emsg);
            }
            else
            {
                fnd->fn_buff = fnd->fn_nam->full_name;
                fnd->fn_name_only = fnd->fn_nam->name_type;
            }
        }
        fnd = fnd->fn_next;
    }
    output_mode = options->vldadef*2 + options->rel;
    if (!rms_errors)
    {
        char *defname;
        struct qual *desc_ptr;
        abs_desc.present = 1;     /* default /OUT */
        defname = first_inp->fn_nam->name_only;   /* use the first filename's stuff */
        for (i=0;i<OUT_FN_MAX;i++)
        {
            fnd = &output_files[i];    /* get pointer to fn_struct struct of  */
            fn_init(fnd);          /* destination and initialise it  */
            desc_ptr=fn_ptrs[i].qdesc; /* get pointer to parameter descriptor */
            if (desc_ptr->present && !desc_ptr->negated)
            { /* is the option there? */
                FILE_name *fnmp;
                int erc;
                fnd->fn_present = 1;    /* signal filename present */
                if (i == OUT_FN_ABS)
                {
                    def_out_ptr[0] = oft[output_mode];
                }
                else
                {
                    def_out_ptr[0] = fn_ptrs[i].defext;
                }
                if (desc_ptr->value == 0)
                {
                    if (i==OUT_FN_SYM || i==OUT_FN_SEC)
                    {
                        fnd->fn_buff = 0;
                        continue;
                    }
                    fnd->fn_buff = defname;
                }
                else
                {
                    char *cp;
                    fnmp = 0;
                    cp = desc_ptr->value;
                    if (!add_defs(cp,(char **)0,(char **)0,ADD_DEFS_SYNTAX,&fnmp))
                    {
                        int l;
                        if (strlen(fnmp->name_only) == 0 && (l=strlen(fnmp->path)) != 0)
                        {
                            cp = MEM_alloc(l+strlen(defname)+1);
                            strcpy(cp,fnmp->path);
                            strcat(cp,defname);
                        }
                    }
                    if (fnmp) MEM_free(fnmp);
                    fnd->fn_buff = cp;
                }
                if (i == OUT_FN_TMP)
                {
#if 0
                    fnd->fn_buff = mkstemp(temp_filename); /* create a dummy filename */
                    def_out_ptr[0] = 0;
#else
                    err_msg(MSG_FATAL,"Sorry, the /TEMP option is no longer avaiable\n");
                    return FALSE;
#endif
                }
                erc = add_defs(fnd->fn_buff,def_out_ptr,(char **)0,ADD_DEFS_OUTPUT,&fnd->fn_nam); /* get user's defaults */
                if (erc == 0)
                {
                    fnmp = fnd->fn_nam;
                    defname = fnmp->name_only;
                    fnd->fn_name_only = fnmp->name_type;
                    fnd->fn_buff = fnmp->full_name;
                }
                else
                {
                    rms_errors += erc;
                    sprintf(emsg,"Unable to parse output filename \"%s\": %s",
                            fnd->fn_buff,err2str(errno));
                }
            }
        }
    }
    if (rms_errors) return FALSE;    /* exit false if errors */
    return TRUE;             /* exit true if no errors */
}
