#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "formats.h"

/* OP  Val Opt Num Neg Enum              Name,              OutIdx,        Comment */

typedef struct
{
	int op;  /* OP - one of A or L */
	int val; /* Val - 1 if no value is allowed */
	int opt; /* Opt - 1 if value is optional */
	int num; /* Num - 1 if value must be a number */
	int neg; /* Neg - 1 if param is negatible */
	char enumName[32]; /* Enum - name of enum */
	char name[32]; /* Name - name of parameter */
	char outIdx[32]; /* OutIdx - if param is output file, the index of same */
	char comment[64]; /* Comment - comment to include */
	char fixedComment[128];
} Line_t;

typedef struct
{
	char str[128];	/* struct entry */
} Structs_t;

#define MAX_LINES (64)

#ifndef _SLICKEDIT_
int main(int argc, char *argv[])
{
	char *str, *end, inBuf[256], tokBuf[sizeof(inBuf)];
	int ii, datLineNo, numLines, numStructs;
	FILE *inF;
	Line_t lines[MAX_LINES], *lp;
	Structs_t structs[MAX_LINES], *sp;
	
	inF = fopen("qualtbl.dat","r");
	if ( !inF )
	{
		perror("Failed to open qualtbl.dat\n");
		return 1;
	}
	numLines = 0;
	datLineNo = 0;
	lp = lines;
	memset(lp,0,sizeof(lines));
	sp = structs;
	numStructs = 0;
	memset(sp,0,sizeof(structs));
	while ( numLines < MAX_LINES && numStructs < MAX_LINES && fgets(inBuf, sizeof(inBuf), inF) )
	{
		++datLineNo;
		end = strchr(inBuf,'\n');
		if ( !end )
		{
			fprintf(stderr,"qualtbl.dat:%d: Malformed entry. No newline found: %s\n", datLineNo, inBuf);
			end = inBuf;
		}
		*end = 0;
		memcpy(tokBuf,inBuf,sizeof(tokBuf));
		tokBuf[sizeof(tokBuf)-1] = 0;
		if ( inBuf[0] == ';' )
			continue;
		if ( inBuf[0] == 'S' && inBuf[1] == ',' )
		{
			size_t len = strlen(inBuf+2);
			if ( len > sizeof(sp->str)-1 )
				len = sizeof(sp->str)-1;
			memcpy(sp->str,inBuf+2,len);
			++sp;
			++numStructs;
			continue;
		}
		if ( inBuf[0] == 'A' )
			lp->op = 1;
		else if ( (inBuf[0] == 'L') )
			lp->op = 2;
		if ( !lp->op )
		{
			printf("%s\n",inBuf);
			continue;
		}
		if ( inBuf[1] != ',' )
		{
			fprintf(stderr,"qualtbl.dat:%d: Malformed entry. No first comma found: %s\n", datLineNo, inBuf);
			fclose(inF);
			return 1;
		}
		ii = 0;
		str = strtok(tokBuf,",");
		for ( ; str; ++ii, str=strtok(NULL,",") )
		{
			switch (ii)
			{
#if 0
				int op;  /* OP - one of A or L */
				int val; /* Val - 1 if no value is allowed */
				int opt; /* Opt - 1 if value is optional */
				int num; /* Num - 1 if value must be a number */
				int neg; /* Neg - 1 if param is negatible */
				char enumName[32]; /* Enum - name of enum */
				char name[32]; /* Name - name of parameter */
				char outIdx[32]; /* OutIdx - if param is output file, the index of same */
				char comment[64]; /* Comment - comment to include */
#endif
			case 0:
				continue;			/* op */
			case 1:
				lp->val = atoi(str);	/* Val */
				continue;
			case 2:
				lp->opt = atoi(str);	/* Opt */
				continue;
			case 3:
				lp->num = atoi(str);	/* Num */
				continue;
			case 4:
				lp->neg = atoi(str);	/* Neg */
				continue;
			case 5:
				while ( isspace(*str) )	/* enumName */
					++str;
				strncpy(lp->enumName,str,sizeof(lp->enumName)-1);
				continue;
			case 6:
				while ( isspace(*str) )	/* name */
					++str;
				strncpy(lp->name,str,sizeof(lp->name)-1);
				continue;
			case 7:
				while ( isspace(*str) )	/* outIdx */
					++str;
				strncpy(lp->outIdx, str, sizeof(lp->outIdx) - 1);
				continue;
			case 8:
				while ( isspace(*str) )	/* comment */
					++str;
				strncpy(lp->comment,str,sizeof(lp->comment)-1);
				continue;
			default:
				fprintf(stderr,"qualtbl.dat:%d: Malformed entry. too many terms: %s\n", datLineNo, inBuf);
				fclose(inF);
				return 1;
			}
		}
		if ( !ii )
		{
			fprintf(stderr,"qualtbl.dat:%d: Malformed entry. strtok() failed to find term: %s\n", datLineNo, inBuf);
			fclose(inF);
			return 1;
		}
		if ( lp->comment[0] )
			snprintf(lp->fixedComment, sizeof(lp->fixedComment) - 1, "/* %s: %s", lp->enumName, lp->comment + 3);
		++lp;
		++numLines;
	}
	fclose(inF);
	inF = NULL;
	fprintf(stdout,"/* numLines=%d, __SIZEOF_SIZE_T__=%d, __SIZEOF_INT__=%d, __SIZEOF_LONG__=%d */\n",
			numLines,
			__SIZEOF_SIZE_T__,
			__SIZEOF_INT__,
			__SIZEOF_LONG__);
	fprintf(stdout,	"/* sizeof(char)=" FMT_SZ
					", sizeof(int)=" FMT_SZ
					", sizeof(long)=" FMT_SZ
					", sizeof(void *)=" FMT_SZ
					" */\n/* "
					"sizeof(int8_t)=" FMT_SZ 
					", sizeof(int16_t)=" FMT_SZ 
					", sizeof(int32_t)=" FMT_SZ
					" */\n/* "
					"sizeof(sizeof)=" FMT_SZ
					", sizeof(size_t)=" FMT_SZ
					", sizeof(time_t)=" FMT_SZ 
					" */\n\n"
				,sizeof(char)
				,sizeof(int)
				,sizeof(long)
				,sizeof(void *)
				,sizeof(int8_t)
				,sizeof(int16_t)
				,sizeof(int32_t)
				,sizeof(sizeof(char))
				,sizeof(size_t)
				,sizeof(time_t)
			);
	fputs("#if QUALTBL_GET_ENUM\n"
		  "typedef enum\n{\n",stdout);
	lp = lines;
	for (ii=0; ii < numLines; ++ii, ++lp)
	{
		fprintf(stdout, "    %s%s\t%s\n", lp->enumName, lp->op != 2 ? "," : "", lp->comment);
	}
	fputs("} QualifierIDs_t;\n\n", stdout);
	sp = structs;
	for (ii=0; ii < numStructs; ++ii, ++sp)
	{
		fprintf(stdout, "%s\n", sp->str);
	}
	fputs("\n#undef QUALTBL_GET_ENUM\n"
		  "#endif /* QUALTBL_GET_ENUM */\n\n"
		  ,stdout);

	fputs("#if QUALTBL_GET_OTHERS\n",stdout);
	fputs("\nQualTable_t qual_tbl[QUAL_MAX] = \n{\n",stdout);
	lp = lines;
	for (ii=0; ii < numLines && lp->op == 1; ++ii, ++lp)
	{
		fprintf(stdout, "    { %d,%d,%d,%d,%d,0,0,0,%s,%s,%s,0,NULL }%s\t%s\n",
				lp->val,
				lp->opt,
				lp->num,
				lp->neg,
				lp->outIdx[0] ? 1:0,
				lp->outIdx,
				lp->name,
				lp->enumName,
				(lp + 1)->op == 1 ? ",":"",
				lp->comment);
	}
	fputs("};\n"
		  "#undef QUALTBL_GET_OTHERS\n"
		  "#endif /* QUALTBL_GET_OTHERS */\n"
		  ,stdout);
	return 0;	
}
#endif	/* _SLICKEDIT_ */
