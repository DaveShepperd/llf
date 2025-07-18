#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include "vlda_structs.h"
#include "version.h"
#include "formats.h"

#if defined(__CYGWIN__) || defined(WIN32)
extern int fileno(FILE *fp);
#endif

#define DEBUG 0

#if DEBUG
#define DBG(x) do { fflush(stderr); printf x; fflush(stdout); } while (0)
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
            fprintf(stderr, "Failed to allocate " FMT_SZ " bytes for filter pool\n",
					NUM_FILTERS*sizeof(Filter_t));
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
static int hashIt( const char *name )
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
static Filter_t *find( const char *name, int *hashResult )
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

static int recnum, rt11;

static unsigned char *getNextRT11Record(unsigned char *inpBuff, int bufLen, int *bufIdxP, int *recLenP, int quietly)
{
	unsigned char cs, *rcdStart;
	int ii, bufIdx, rcdCnt, zeros=1;
	
	bufIdx = *bufIdxP;
	*recLenP = 0;
	DBG(("getNextRT11Record(): At entry: recnum=%d, inpBuff=%p, bufLen=%d, *bufIdxP=%d, quietly=%d\n", recnum, inpBuff, bufLen, *bufIdxP, quietly));
	for ( ; bufIdx < bufLen; ++bufIdx )
	{
		if ( inpBuff[bufIdx+0] != 1 )
		{
			if ( !inpBuff[bufIdx+0] )
				++zeros;
			continue;
		}
		zeros = 0;
		if ( inpBuff[bufIdx+1] != 0 )
			continue;
		bufIdx += 2;	/* Skip over sentinel */
		if ( bufLen-bufIdx < 3 )
		{
			if ( !quietly )
				fprintf(stderr, "Malformed record. bufLen=%d, bufIdx=%d\n", bufLen, bufIdx);
			*bufIdxP = bufIdx;
			return NULL; 
		}
		rcdCnt = (inpBuff[bufIdx+1]<<8)|inpBuff[bufIdx+0];
		DBG(("getNextRT11Record(): Found sentinel at bufIdx=0x%04X. Count bytes=%02X %02X (0x%04X)\n", bufIdx-2, inpBuff[bufIdx+0], inpBuff[bufIdx+1], rcdCnt));
		if ( rcdCnt < 4 )
		{
			if ( !quietly )
				fprintf(stderr, "Malformed record. Record length is too small. bufLen=%d, bufIdx=%d, rcdCnt=0x%04X.\n", bufLen, bufIdx, rcdCnt);
			*bufIdxP = bufIdx;
			return NULL;
		}
		bufIdx += 2;	/* skip over count */
		rcdStart = inpBuff+bufIdx;	/* Point to data in record */
		cs = 0;			/* prime checksum */
		for (ii=0; ii <= rcdCnt; ++ii)
			cs += inpBuff[bufIdx-4+ii];
		rcdCnt -= 4;	/* count includes sentinel and itself so take them out */
		bufIdx += rcdCnt;	/* skip over the record's data */
		++bufIdx;		/* skip over the checksum */
		*bufIdxP = bufIdx;	/* pass back the updated index */
		*recLenP = rcdCnt;	/* pass back the record's length */
		if ( cs )
		{
			if ( !quietly )
				fprintf(stderr, "Checksum error on record %d. Expected 0x00 computed 0x%02X, bufLen=%d, bufIdx=%d, rcdCnt=%d\n", recnum, cs, bufLen, bufIdx, rcdCnt);
			return NULL;
		}
		DBG(("getNextRT11Record(): At exit: bufIdx=%d, recLen=%d\n", bufIdx, rcdCnt));
		return rcdStart;
	}
	if ( !quietly && !zeros )
		fprintf(stderr, "Did not find sentinel on record %d. bufLen=%d, bufIdx=%d, zeros=%d\n", recnum, bufLen, bufIdx, zeros);
	*bufIdxP = bufIdx;
	return NULL;
}

static unsigned char *getNextRecord(unsigned char *inpBuff, int bufLen, int *bufIdxP, int *recLenP)
{
	unsigned char *rcdStart;
	int bufIdx, rcdCnt;

	bufIdx = *bufIdxP;
	*recLenP = 0;
	rcdCnt = (inpBuff[bufIdx + 1] << 8) | inpBuff[bufIdx];
	if ( !rcdCnt )
		return NULL;
	if ( rcdCnt == 1 && !bufIdx )
	{
		rcdStart = getNextRT11Record(inpBuff,bufLen,bufIdxP,recLenP,1);
		if ( rcdStart )
		{
			rt11 = 1;
			return rcdStart;
		}
		*bufIdxP = 0;
		*recLenP = 0;
		bufIdx = 0;
	}
	rcdStart = inpBuff+bufIdx+2;
	bufIdx += rcdCnt+2;
	if ( (bufIdx&1) )
		++bufIdx;
	*bufIdxP = bufIdx;
	*recLenP = rcdCnt;
	return rcdStart;
}

struct gsdstruct
{
    unsigned short gsdnm1;   /* msb of name field */
    unsigned short gsdnm0;   /* lsb of name field */
#if 0
    unsigned int gflg_weak:1;    /* weak/strong bit */
    unsigned int gflg_shr:1; /* refers to shared region (not used) */
    unsigned int gflg_ovr:1; /* section to be overlaid */
    unsigned int gflg_def:1; /* symbol defined */
    unsigned int gflg_base:1;    /* section is base page */
    unsigned int gflg_rel:1; /* relative symbol/section */
    unsigned int gflg_scp:1; /* global section (not used) */
    unsigned int gflg_dta:1; /* data section */
#else
#define FLGS_WEAK   (1<<0)
#define FLGS_SHR    (1<<1)
#define FLGS_OVR    (1<<2)
#define FLGS_DEF    (1<<3)
#define FLGS_BASE   (1<<4)
#define FLGS_REL    (1<<5)
#define FLGS_SCP    (1<<6)
#define FLGS_DTA    (1<<7)
    unsigned char flags;
#endif
    unsigned char gsdtyp;    /* gsd type */
    short gsdvalue;      /* gsd value */
};

struct gsdtime
{        /* gsd time/date structure */
    unsigned short gsdtim1;  /* msb of time longword */
    unsigned short gsdtim0;  /* lsb of time longword */
    unsigned fill1:16;       /* skip a short */
    unsigned int gsdyr:5;    /* bits 0:4 is the year since 1972 */
    unsigned int gsday:5;    /* bits 5:9 is the day */
    unsigned int gsdmn:4;    /* bits 10:13 is the month */
    unsigned fill2:2;        /* fill out to 16 bits */
};

struct gsdxlate
{       /* gsd xlator name */
    unsigned long gsdxn;     /* name */
    unsigned filler:16;      /* filler */
    unsigned char gsderrors; /* error counter */
    unsigned char gsdwarns;  /* warnings */
};

/*****************************************************************
 * An alternate GSD entry layout in order to get flags as a byte *
 *****************************************************************/

struct gsdflags
{
    unsigned long gsdlong;
    char flags;
    unsigned fill1:8;
    unsigned fill2:16;
};

struct r50name
{
    unsigned short r50msbs;
    unsigned short r50lsbs;
};

struct rldstruct
{
    unsigned int rldtyp:6;   /* rld type code */
    unsigned int rldmode:2;  /* 6800 mode (6) and byte mode (7) bits */
    unsigned char rlddsp;    /* displacement */
};

/*******************************************************************
 * The following is a union of pointers to various types of object *
 * file structures. This allows for obj to be auto-incremented     *
 * automagically after the correseponding item(s) have been        *
 * processed.							   *
 *******************************************************************/

#if 0
static union
{
    unsigned short *rectyp;  /* object record type */
    struct gsdstruct *gsd;   /* followed by a GSD */
    struct rldstruct *rld;   /* and an RLD */
    struct r50name *rldnam;  /* pointer to rad50 name */
    long *rldlong;       /* pointer to a long */
    short *rldval;       /* rld value or constant */
    char *complex;       /* pointer to complex relocation string */
    unsigned short *txtlda;  /* text load address */
    char *txt;           /* text */
/*   struct isdstruct *isd;	    there may be an ISD too */
} obj;
#endif

static char r50toa[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.*0123456789";
static int d5050=050*050;
static int d50=050;
static char *months[] =
{"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

/****************************************************************************
 * r50div - convert a short containing rad50 code to an ASCII stream
 */
static char *r50div( unsigned short r50, char *dst )
/*
 * At entry:
 *	r50 - is the short containing the rad50 code
 *	dst - pointer to ascii buffer
 * At exit:
 *  up to 3 character string located at *dst
 *  null inserted either at where a space would be
 *  or at dst[4].
 *	returns pointer to where the null is
 */
{
	int ans0, ans1, ans2;
	char cc;
	ans0 = r50/d5050;
	ans1 = (r50%d5050)/d50;
	ans2 = r50%d50;
	cc = r50toa[ans0];
	if ( cc != ' ' )
	{
		*dst++ = cc;
		cc = r50toa[ans1];
		if ( cc != ' ' )
		{
			*dst++ = cc;
			cc = r50toa[ans2];
			if ( cc != ' ' )
				*dst++ = cc;
		}
	}
	*dst = 0;
	return dst;
}

/*********************************************************************
 * rad50_to_ascii - converts a longword rad50 value to an ASCII string
 * trimmed of trailing spaces and null terminated.
 */
static int rad50_to_ascii( struct r50name *ptr, char *dst )
/*
 * At entry:
 *  ptr - points to gsdstruct containing longword rad50
 *  dst - pointer to place to deposit string
 * At exit:
 *  length of null terminated and space trimmed ASCII string
 *  inserted at *dst
 */
{
	char *beg = dst;
    dst = r50div(ptr->r50msbs,dst);    /* unpack msb rad50 word */
    dst = r50div(ptr->r50lsbs,dst);    /* unpack lsb rad50 word */
    return dst-beg;
}

static void outputSymName(const char *symName, const char *filter, unsigned long value, int syntax)
{
	int len=strlen(symName);
	
	if ( filter )
	{
		Filter_t *blk;
		int hash;

		blk = find(symName,&hash);
		if ( blk )
		{
			if ( !syntax )
			{
				printf("%-*.*s == 0x%08lX\n",
					   len,len,symName,value);
			}
			else
			{
				printf("DECLARE( %-*.*s : #%08lX );\n",
					   len,len,symName,value);
			}
			blk->used = 1;
		}
	}
	else
	{
		if ( !syntax )
		{
			printf("%-*.*s == 0x%08lX\n",
				   len,len,symName,value);
		}
		else
		{
			printf("DECLARE( %-*.*s : #%08lX );\n",
				   len,len,symName,value);
		}
	}
}

static const char *inpFileName;

static void do_gsd( int len, struct gsdstruct *gsdptr, const char *filter, int outSyntax )
/*
 * At entry:
 *	len - number of bytes remaining in record
 *	gsdptr - pointer to first byte in record (as a gsdstruct)
 * At exit:
 *	all items in the record will have been processed.
 *	The definition file may have been updated if there were
 *	definitions in the GSD. Symbols and sections may have been
 *	inserted into the symbol table.
 */
{
    char *ss;
    int item=0;
	char symbolName[8];
	int i;
	unsigned int mn,sec,mon;
	char dateTime[32];
	char xlator[128];
	
	dateTime[0] = 0;
	symbolName[0] = 0;
	xlator[0] = 0;
	DBG(("do_gsd(): At entry ptr=%p, len=%d\n", (void *)gsdptr, len));
    for (; len > 0; len -= sizeof(struct gsdstruct),gsdptr++,item++)
    {
		DBG(("do_gsd():       Item: %2d: type: %d\n", item, gsdptr->gsdtyp));
        switch (gsdptr->gsdtyp)
        {
        case 4:		  /* Global symbol name */
			{
				ss = symbolName;
                ss += rad50_to_ascii((struct r50name *)gsdptr,ss);
                if ((gsdptr->flags&FLGS_DEF))
                {     /* if symbol being defined */
					outputSymName(symbolName,filter,gsdptr->gsdvalue,outSyntax);
                }
                break;
            }
		case 2:   /* Internal Symbol name */
			if ( !dateTime[0] )
			{
				i = ((((struct gsdtime *)gsdptr)->gsdtim1 << 16)+
						 ((struct gsdtime *)gsdptr)->gsdtim0)/60;
					sec = i % 60;    /* get the seconds */
					i /= 60;     /* toss the seconds */
					mn = i % 60; /* get the minutes */
					mon = ((struct gsdtime *)gsdptr)->gsdmn-1;
					snprintf(dateTime,sizeof(dateTime),"%02d-%s-%04d %02d:%02d:%02d",
							((struct gsdtime *)gsdptr)->gsday,
							months[mon],
							((struct gsdtime *)gsdptr)->gsdyr+1972,
							i/60,mn,sec);
			}
			else
			{
				ss = xlator;
				ss += rad50_to_ascii((struct r50name *)gsdptr, ss);
				if ((i=((struct gsdxlate *)gsdptr)->gsderrors) != 0)
				{
					ss += snprintf(ss,sizeof(xlator)-(ss-xlator)," err=%d",i);
				}
				if ((i=((struct gsdxlate *)gsdptr)->gsdwarns) != 0)
				{
					ss += snprintf(ss,sizeof(xlator)-(ss-xlator)," wrn=%d",i);
				}
				printf("%s Extracted from %s which was built via RT11 %s on %s\n\n", outSyntax ? "--":";", inpFileName, xlator, dateTime);
			}
			break;
		case 0:	  /* module name */
		case 1:   /* CSECT name */
		case 3:	  /* transfer address,ignore for now */
		case 5:   /* PSECT name */
		case 6:   /* GSD ident */
		case 7:	  /* Mapped array declaration */
		case 8:	  /* completion routine declaration */
			break;
        default:
			fprintf(stderr,"do_gsd(): Illegal GSD type: %d in input file\n", gsdptr->gsdtyp);
			break;
        }             /* switch  */
    }                /* for 	   */
}               /* do_gsd  */

static int helpEm(void)
{
    fprintf(stderr, "Usage: vecextract [-or][-f filter] input\n"
           "Reads input and converts absolute symbol defines to macxx source or llf option format.\n"
           "where:\n"
           "-f filter = points to filter filename containing names to extract, one per line.\n"
           "-o        = output syntax is llf's option file\n"
           "-r        = force input file to be of type RT11 (will automatically try to figure this out)\n"
           "input = input filename. Expected to be llf's .stb format (or linkm's .obj or .sym format).\n"
           "Outputs to stdout.\n");
    return 1;
}

int main( int argc, char *argv[])
{
    FILE *ifp, *ffp=NULL;
    VLDA_sym *vsym;
    unsigned short len=0;
    char *symName;
    int opt, results=0;
    int minLen=10;
    char *filter=NULL;
	unsigned char *inpBuff=NULL;
	int sts,inpSize,bufIdx,recLen;
	struct stat st;
	int outSyntax=0;
	
    while ( (opt = getopt(argc, argv, "f:or")) != -1 )
    {
        switch (opt)
        {
        case 'f':
            filter = optarg;
            break;
        case 'o':
            outSyntax = 1;
            break;
        case 'r':
            rt11 = 1;
            break;
        default: /* '?' */
            return helpEm();
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Expected input filename\n");
        return helpEm();
    }
    inpFileName = argv[optind];
	sts = stat(inpFileName,&st);
	if ( sts < 0 )
	{
		fprintf(stderr,"Failed to stat '%s': %s\n", inpFileName, strerror(errno));
		return 1;
	}
	ifp = fopen(inpFileName, "rb");
	if ( !ifp )
	{
		fprintf(stderr,"Failed to open for input '%s': %s\n", inpFileName, strerror(errno));
		return 1;
	}
	inpSize = st.st_size;
	inpBuff = (unsigned char *)malloc(inpSize);
	if ( !inpBuff )
	{
		fprintf(stderr,"Failed to malloc(%d): %s\n", inpSize, strerror(errno));
		fclose(ifp);
		return 1;
	}
	sts = fread(inpBuff,1,inpSize,ifp);
	if ( sts != inpSize )
	{
		fprintf(stderr,"Failed to read %d bytes from '%s'. Instead read %d: %s\n", inpSize, inpFileName, sts, strerror(errno));
		fclose(ifp);
		free(inpBuff);
		return 1;
	}
	fclose(ifp);
	ifp = NULL;
    if ( filter )
    {
        int res;
        ffp = fopen(filter,"r");
        if ( !ffp )
        {
            fprintf(stderr,"Failed to open filter file '%s': %s\n", filter, strerror(errno));
            return 1;
        }
        res = fillFilter(ffp);
        fclose(ffp);
        ffp = NULL;
        if ( res )
            return 1;
    }
	bufIdx = 0;
	recLen = 0;
    printf("%s vecextract built along with llf version %s\n", outSyntax ? "--":";", REVISION);
    for ( recnum = 0; bufIdx < inpSize; ++recnum )
    {
		unsigned char *rcd;
		if ( rt11 )
			rcd = getNextRT11Record(inpBuff, inpSize, &bufIdx, &recLen,0);
		else
			rcd = getNextRecord(inpBuff, inpSize, &bufIdx, &recLen);
		if ( !rcd )
			break;
		if ( !rt11 )
		{
			vsym = (VLDA_sym *)rcd;
			switch (vsym->vsym_rectyp)
			{
			case VLDA_ABS:      /* vlda relocatible text record */
				DBG(("%4d: %4d bytes. Type=%d(ABS), addr=0x%08lX\n",
					recnum, len, vsym->vsym_rectyp, ((VLDA_abs *)rcd)->vlda_addr ));
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
				len = strlen((char *)rcd+vsym->vsym_noff);
				if ( len < minLen )
					len = minLen;
				else
					minLen = len;
				symName = (char *)rcd+vsym->vsym_noff;
				outputSymName(symName, filter, vsym->vsym_value,outSyntax);
				break;
			case VLDA_ID:		/* misc header information */
				{
					VLDA_id *vid = (VLDA_id *)rcd;
					DBG(("%4d: %4d bytes. Type=%d(ID), %d.%d, idsz=%d/%d, sym=%d/%d, seg=%d/%d, %s %s %s %d,%d\n",
						recnum, len, vid->vid_rectyp,
						vid->vid_siz, (int)sizeof(VLDA_id),
						vid->vid_maj, vid->vid_min,
						vid->vid_symsiz, (int)sizeof(VLDA_sym),
						vid->vid_segsiz, (int)sizeof(VLDA_seg),
						rcd+vid->vid_image,
						rcd+vid->vid_target,
						rcd+vid->vid_time,
						vid->vid_errors,
						vid->vid_warns
						 ));
					printf("%s From %s, file version %d.%d built on %s\n",
                        outSyntax ? "--":";",
						rcd+vid->vid_image,
						vid->vid_maj, vid->vid_min,
						rcd+vid->vid_time);
					printf("%s Built for %s with %d error%s and %d warning%s\n\n",
                        outSyntax ? "--":";",
						rcd+vid->vid_target,
						vid->vid_errors, vid->vid_errors == 1 ? "" : "s",
						vid->vid_warns, vid->vid_warns == 1 ? "" : "s");
					break;
				}
			case VLDA_TPR:		/* transparent record (raw data follows */
					break;
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
		else
		{
			struct gsdstruct *gsdptr;
			if ( rcd[0] == 1 )
			{
				gsdptr = (struct gsdstruct *)(rcd+2);
				recLen -= 2;
				do_gsd(recLen, gsdptr, filter, outSyntax);    /* go do the GSD stuff */
			}
			else
				DBG(("Skipping record type %d\n", rcd[0]));
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
                    printf("%s *** No such symbol as %s in vector file *** \n", outSyntax?"--":";", ptr->name);
                    results = 1;
                }
                ptr = ptr->next;
            }
        }
    }
	if ( inpBuff )
		free(inpBuff);
    return results;
}
