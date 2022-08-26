#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "vlda_structs.h"

#ifdef __CYGWIN__
extern int fileno(FILE *fp);
#endif

#define DEBUG 0

#if DEBUG
#define DBG(x) printf x
#else
#define DBG(x) do { ; } while (0)
#endif

#define NAME_POOL_SIZE (10000)  /* block size of name pool */
static char *namePool;
static int namePoolSize;

/* Copy a name to a malloc()'d area.
 *
 * At entry:
 * name - pointer to ASCII name to save
 *
 * At exit:
 * returns pointer to malloc'd area with string copied.
 */
static char *saveName(char *name)
{
    char *ans;

    int len = strlen(name)+1;
    if ( len > namePoolSize )
    {
        namePoolSize = NAME_POOL_SIZE;
        if ( namePoolSize < len )
            namePoolSize += len;
        namePool = (char *)malloc(namePoolSize);
        if ( !namePool )
        {
            fprintf(stderr, "Ran out of memory allocating %d bytes\n", namePoolSize );
            exit(1);
        }
    }
    ans = namePool;
    strcpy(ans,name);
    namePool += len;
    namePoolSize -= len;
    return ans;
}

#define HASH_TABLE_SIZE (457)   /* Works best if is a prime number */
#define NUM_FILTERS  (1000)     /* Number of filter blocks to allocate at one time */

typedef struct filter_st
{
    char *name;                 /* Name of symbol */
    struct filter_st *next;     /* Next symbol in chain */
    int used;                   /* set not zero if symbol found in input file */
} Filter_t;

static Filter_t *hash_table[HASH_TABLE_SIZE];
static Filter_t *filterPool;
static int filterPoolSize;

/* Get a Filter_t pointer.
 *
 * At entry:
 * no requirements
 *
 * At exit:
 * returns pointer to pre-cleared Filter_t
 */
static Filter_t *getFilterBlk(void)
{
    Filter_t *ans;

    if ( filterPoolSize < 1 )
    {
        filterPoolSize = NUM_FILTERS;
        filterPool = (Filter_t *)calloc(NUM_FILTERS,sizeof(Filter_t));
        if ( !filterPool )
        {
            fprintf(stderr, "Failed to allocate %d bytes for filter pool\n", NUM_FILTERS*sizeof(Filter_t));
            exit(1);
        }
    }
    --filterPoolSize;
    ans = filterPool;
    ++filterPool;
    return ans;
}

/* Hash the symbol name
 *
 * At entry:
 * name - pointer to symbol name
 *
 * At exit:
 * returns hash value
 */
static int hashIt( char *name )
{
    unsigned int res=0;
    while ( *name )
    {
        res *= 13;
        res += *name++;
    }
    return res % HASH_TABLE_SIZE;
}

/* Find symbol in hash table
 *
 * At entry:
 * name - pointer to symbol name
 * hashResult - if not NULL, pointer to place to stash hash value
 *
 * At exit:
 * returns pointer to Filter_t or NULL if symbol not found
 */
static Filter_t *find( char *name, int *hashResult )
{
    Filter_t *result;
    int hash;

    hash = hashIt(name);
    if ( hashResult )
        *hashResult = hash;
    result = hash_table[hash];
    while( result )
    {
        if ( !strcmp(name,result->name) )
            break;
        result = result->next;
    }
    return result;
}

/* Insert symbol into hash table
 *
 * At entry:
 * name - pointer to symbol name (must be permanent memory; use saveName() above)
 *
 * At exit:
 * returns pointer to newly inserted Filter_t
 */
static Filter_t *insert( char *name )
{
    int hash;
    Filter_t *blk, *ptr, **prev;

    ptr = find(name,&hash);
    if ( ptr )
    {
        fprintf(stderr,"%s is already loaded into the symbol table. Ignored\n", name );
        return NULL;
    }
    blk = getFilterBlk();
    blk->name = name;
    prev = hash_table+hash;
    ptr = *prev;
    while ( ptr )
    {
        if ( strcmp(name,ptr->name) < 0 )
            break;
        prev = &ptr->next;
        ptr = *prev;
    }
    blk->next = ptr;
    *prev = blk;
    return blk;
}

static char lineBuff[1000];         /* buffer into which to read filter data */

/* Fill symbol table with contents of filter file
 *
 * At entry:
 * ffp - pointer to open FILE
 *
 * At exit:
 * returns 0 on success, 1 on failure
 */
static int fillFilter(FILE *ffp)
{
    int res;
    char *save;

    while ( fgets(lineBuff,sizeof(lineBuff)-1,ffp) )
    {
        res = strlen(lineBuff);
        /* Syntax is simple. Just a symbol name followed by a newline. No spaces. 
         * If the line starts with a space, #, ;, * or a non-printing char, it
         * is assumed to be a comment line and is quietly ignored.
         */
        if (res > 1
            && lineBuff[0] != '#'
            && lineBuff[0] != ';'
            && lineBuff[0] != '*'
            && isprint(lineBuff[0]) )
        {
            save = lineBuff;
            lineBuff[res-1] = 0;
            while ( *save && !isspace(*save) && isprint(*save) )
                ++save;
            *save = 0;
            save = saveName(lineBuff);
            insert(save);
        }
    }
    res = 0;
    if ( ferror(ffp) )
    {
        perror("Error reading filter file");
        res = 1;
    }
    return res;
}

int main( int argc, char *argv[])
{
    FILE *ifp,*ffp=NULL;
    VLDA_sym *vsym;
    unsigned short len=0;
    char *buff=NULL, *symName;
    int bufsiz=0, results=0;
    int recnum,actual,minLen=10;
    char *filter=NULL;

    --argc;
    ++argv;
    while ( argc > 0 )
    {
        char *opt;
        opt = *argv;
        if ( *opt++ != '-' )
            break;
        switch (*opt++)
        {
        case 'f':
            if ( !*opt )
            {
                if ( argc )
                {
                    --argc;
                    ++argv;
                    opt = *argv;
                }
                else
                {
                    fprintf(stderr, "Need a filename parameter after -f\n" );
                    break;
                }
            }
            filter = opt;
            break;
        default:
            fprintf(stderr, "Unrecognised option: %c\n", *opt );
        case '?':
        case 'h':
            argc = 1;
            break;
        }
        --argc;
        ++argv;
    }
    if ( argc < 1 )
    {
        fprintf(stderr, "Usage: vecextract [-f filter] input\n"
               "Reads input and converts absolute symbol defines to macxx format.\n"
               "where:\n"
               "-f filter = points to filter filename containing names to extract, one per line.\n"
               "input = input filename. Expected to be llf's .stb format.\n"
               "Outputs to stdout.\n");
        return 1;
    }
    ifp = fopen(*argv,"rb");
    if (ifp == (FILE *)0)
    {
        perror("Unable to open input");
        return 1;
    }
    if ( filter )
    {
        int res;
        ffp = fopen(filter,"r");
        if ( !ffp )
        {
            perror("Unable to open filter file");
            fclose(ifp);
            return 1;
        }
        res = fillFilter(ffp);
        fclose(ffp);
        ffp = NULL;
        if ( res )
        {
            fclose(ifp);
            return 1;
        }
    }
    for ( recnum=0; ; ++recnum )
    {
        if ( read(fileno(ifp),&len,sizeof(len)) != (int)sizeof(len) )
            break;
        if ( !len )
            break;
        len += (len&1);
        if ( len > bufsiz )
        {
            bufsiz = len;
            buff = (char *)realloc(buff,bufsiz);
            if ( !buff )
            {
                fprintf(stderr,"Error realloc'ing %d bytes for buffer.\n", bufsiz );
                return 1;
            }
        }
        if ( (actual=read(fileno(ifp),buff,len)) != len )
        {
            fprintf(stderr, "Error reading from record %d. Wanted %d, got %d: %s\n",
                recnum, len, actual, strerror(errno));
            return 1;
        }
        vsym = (VLDA_sym *)buff;
        switch (vsym->vsym_rectyp)
        {
        case VLDA_ABS:      /* vlda relocatible text record */
            DBG(("%4d: %4d bytes. Type=%d(ABS), addr=0x%08lX\n",
                recnum, len, vsym->vsym_rectyp, ((VLDA_abs *)buff)->vlda_addr ));
            break;
        case VLDA_GSD:		/* psect/symbol definition record */
            DBG(("%4d: %4d bytes. Type=%d(GSD), flags=0x%04X\n",
                recnum, len, vsym->vsym_rectyp, vsym->vsym_flags ));
            if (vsym->vsym_rectyp != VLDA_GSD ||  /* if record not GSD or  */
                !(vsym->vsym_flags&VSYM_SYM) ||      /* ...not id'ing a symbol or */
                !(vsym->vsym_flags&VSYM_DEF) ||      /* ...symbol not defined or */
                !(vsym->vsym_flags&VSYM_ABS) ||      /* ...symbol not absolute then */
                 (vsym->vsym_flags&VSYM_LCL) ||      /* ...symbol is local */
                 (vsym->vsym_flags&VSYM_EXP)         /* ...symbol is defined via an expression */
                )
            {
                continue;       /* ...skip it */
            }
            len = strlen(buff+vsym->vsym_noff);
            if ( len < minLen )
                len = minLen;
            else
                minLen = len;
            symName = buff+vsym->vsym_noff;
            if ( filter )
            {
                Filter_t *blk;
                int hash;

                blk = find(symName,&hash);
                if ( blk )
                {
                    printf("%-*.*s == 0x%08lX\n",
                           len,len,symName,vsym->vsym_value);
                    blk->used = 1;
                }
            }
            else
            {
                printf("%-*.*s == 0x%08lX\n",
                       len,len,symName,vsym->vsym_value);
            }
            break;
        case VLDA_ID:		/* misc header information */
            {
                VLDA_id *vid = (VLDA_id *)buff;
                DBG(("%4d: %4d bytes. Type=%d(ID), %d.%d, idsz=%d/%d, sym=%d/%d, seg=%d/%d, %s %s %s %d,%d\n",
                    recnum, len, vid->vid_rectyp,
                    vid->vid_siz, (int)sizeof(VLDA_id),
                    vid->vid_maj, vid->vid_min,
                    vid->vid_symsiz, (int)sizeof(VLDA_sym),
                    vid->vid_segsiz, (int)sizeof(VLDA_seg),
                    buff+vid->vid_image,
                    buff+vid->vid_target,
                    buff+vid->vid_time,
                    vid->vid_errors,
                    vid->vid_warns
                     ));
                printf("; From a %s %d.%d build on %s\n",
                    buff+vid->vid_image,
                    vid->vid_maj, vid->vid_min,
                    buff+vid->vid_time);
                printf("; Built for %s with %d error%s and %d warning%s\n\n",
                    buff+vid->vid_target,
                    vid->vid_errors, vid->vid_errors == 1 ? "" : "s",
                    vid->vid_warns, vid->vid_warns == 1 ? "" : "s");
                break;
            }
        case VLDA_TPR:		/* transparent record (raw data follows */
            {
                break;
            }
        case VLDA_TXT:  	/* vlda relocatible text record */
        case VLDA_ORG:		/* set the PC record */
        case VLDA_EXPR:		/* expression */
        case VLDA_SLEN:		/* segment length */
        case VLDA_XFER:		/* transfer address */
        case VLDA_TEST:		/* test and display message if result is false */
        case VLDA_DBGDFILE:	/* dbg file specification */
        case VLDA_DBGSEG:		/* dbg segment descriptors */
        case VLDA_BOFF:		/* branch offset out of range test */
        case VLDA_OOR:		/* operand value out of range test */
        default:
            DBG(("%4d: %4d bytes. Type=%d(?)\n",
                recnum, len, vsym->vsym_rectyp ));
            break;
        }
    }
    DBG(("Hit EOF at record %d\n", recnum));
    if ( filter )
    {
        Filter_t *ptr;
        int ii;
        for ( ii=0; ii < HASH_TABLE_SIZE; ++ii )
        {
            ptr = hash_table[ii];
            while (ptr )
            {
                if ( !ptr->used )
                {
                    fprintf(stderr,"Didn't find '%s' in input file.\n",ptr->name);
                    printf("; *** No such symbol as %s in vector file *** \n", ptr->name);
                    results = 1;
                }
                ptr = ptr->next;
            }
        }
    }
    fclose(ifp);
    return results;
}
