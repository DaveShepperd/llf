/*
    help.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "memmgt.h"

#if !defined(VMS) && !defined(MS_DOS)
    #define OPT_DELIM '-'		/* UNIX(tm) uses dashes as option delimiters */
static char opt_delim[] = "-";
#else
    #define OPT_DELIM '/'		/* everybody else uses slash */
static char opt_delim[] = "/";
#endif

static char upc_mark[1];
#define UPC upc_mark
#define OPT opt_delim
extern char def_vlda[],def_map[],def_stb[],def_ln[],def_lb[],def_sym[],def_hex[];
extern char def_ol[],def_ob[];

static char *help_msg[] = {
    "Usage: ",UPC,"llf"," file1 file2 ... [",OPT,"","option]\n",
    "Where option is one of ([] implies optional text):\n",
    OPT,"[no]output","[=name] - select and name output file\n",
    OPT,"[no]map","[=name]    - select and name map file\n",
#if 0
    OPT,"[no]temp","[=name]   - select and name temporary work file\n",
#endif
    OPT,"[no]symbol","[=name] - select and name symbol file\n",
    OPT,"[no]stb","[=name]	- select and name symbol table file\n",
    OPT,"option","[=name]	- names an input option file\n",
    OPT,"library","[=name]	- names an input library file\n",
    OPT,"[no]binary","	- select binary format output file\n",
    OPT,"[no]cross","	- select cross reference map file format\n",
    OPT,"[no]octal","	- select octal map file format\n",
    OPT,"[no]obj","	- select default input file type of ",def_ob,"\n",
    OPT,"[no]relative","	- select relative output file format\n",
    OPT,"[no]error","	- force display of undefined symbols in ",OPT,"relative"," mode\n",
	OPT,"[no]quiet","	- Suppress multiple symbol define warnings arising from a .stb file mode\n",
    "Options may be abbreviated to 1 or more characters.\n",
    "Defaults are ",OPT,"out ",OPT,"nomap ",OPT,"notemp ",OPT,"nosym ",
    OPT,"nostb ",OPT,"bin ",OPT,"nocross\n",
    "             ",OPT,"nooctal ",OPT,"noobj ",OPT,"norel ",
    OPT,"noerr ",OPT,"noopt ",OPT,"nolib ",OPT,"noquiet\n",
    "And the output filename defaults to the same name as the first input\n",
    "file with file types according to the following:\n",
    "  ",UPC,def_vlda," if ",OPT,"bin",OPT,"norel, ",UPC,def_lb," if ",OPT,"bin",OPT,"rel,\n",
    "  ",UPC,def_ln," if ",OPT,"nobin ",OPT,"norel and ",def_hex," if ",OPT,"nobin ",OPT,"rel\n",
    "The ",OPT,"map, ",OPT,"symbol ","and ",OPT,"stb"," files default to the same name as the ",OPT,"output"," file\n",
    "with file types of ",def_map,", ",def_sym," and ",def_stb," respectively.\n",
    0};

int display_help( void )
{
    int upc=0,i;
    char *src,*dst;
    src = dst = MEM_alloc(10240);
    for (i=0;help_msg[i] && i < sizeof(help_msg)/sizeof(char *);++i)
    {
        char *s;
        if (help_msg[i] == opt_delim)
        {
            *dst++ = OPT_DELIM;
#if defined(VMS) || defined(MS_DOS)
            upc = 1;
#endif
            continue;
        }
        if (help_msg[i] == upc_mark)
        {
#if defined(VMS) || defined(MS_DOS)
            upc = 1;
#endif
            continue;
        }
        if (!upc)
        {
            strcpy(dst,help_msg[i]);
            dst += strlen(help_msg[i]);
        }
        else
        {
            char c;
            s = help_msg[i];
            while ((c = *s++) != 0)
            {
                if (islower(c)) c = toupper(c);
                *dst++ = c;
            }
            upc = 0;
        }
    }
    *dst = 0;
    fputs(src,stderr);
    MEM_free(src);
#if defined(VMS)
    return 0x10000000;
#else
    return 1;
#endif
}
