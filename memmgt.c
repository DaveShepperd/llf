/*
    memmgt.c - Part of llf, a cross linker. Part of the macxx tool chain.
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
#include <stdlib.h>
#include <string.h>
#include "header.h"

#undef NULL
#define NULL 0

static char *macxx_name = "LLF";
int32_t total_mem_used;
int32_t peak_mem_used;

typedef struct hdr
{
    uint32_t size;
    void *caller;
    int line;
    char *file;
    struct hdr *next, *prev;
    uint32_t magic;
} Hdr;

#define PRE_MAGIC 	0x12345678
#define NOTALLOCED  	0x00010000
#define POST_MAGIC	0x87654321
#define NFG ((char *)0)

#if defined(DEBUG_MALLOC)
static Hdr *top, *bottom;
#endif

static char *check(Hdr *hdr) {
    char *msg = 0;
    uint32_t *end;

    if (hdr->magic != PRE_MAGIC)
    {
        if (hdr->magic == (PRE_MAGIC|NOTALLOCED))
        {
            msg = "%%%s-F-FATAL, %s:%d tried to free %08lX already free'd.\n";
        }
        else
        {
            msg = "%%%s-F-FATAL, %s:%d tried to free %08lX with corrupted header.\n";
        }
    }
    else
    {
        end = (uint32_t *)((char *)(hdr+1)+hdr->size);
        if (*end != POST_MAGIC)
        {
            msg = "%%%s-F-FATAL, %s:%d tried to free %08lX with corrupted tail.\n";
        }
    }
    return msg;
}

#if defined(DEBUG_MALLOC)
static void check_all( void ) {
    Hdr *h;
    char *msg;

    h = top;
    while (h)
    {
        msg = check(h);
        if (msg)
        {
            fprintf(stderr, msg, macxx_name, h->file, h->line, h+1);
            abort();
        }
        h = h->next;
        if (h == top) break;
    }
}
#endif

int mem_free(char *s, char *file, int line) {
    Hdr *hdr;
    char *msg=0;

    if (s == 0)
    {
        msg = "%s:%d tried to free %08lX.\n";
        hdr = 0;
    }
    else
    {
        hdr = (Hdr *)s - 1;
        msg = check(hdr);
    }
    if (msg)
    {
        fprintf(stderr, msg, macxx_name, file, line, s);
        abort();
    }
#if defined(DEBUG_MALLOC)
    check_all();
    if (hdr == top)
    {
        top = hdr->next;
    }
    if (hdr == bottom)
    {
        bottom = hdr->prev;
    }
    if (hdr->next) hdr->next->prev = 0;
    if (hdr->prev) hdr->prev->next = 0;
    hdr->next = hdr->prev = 0;
#endif
    free((char *)hdr);
    return(0);                  /* assume it worked */
}

char *mem_alloc(int nbytes, char *file, int line) {
    char *s;
    Hdr *hdr;
    uint32_t *end;
    int siz;
    nbytes = (nbytes + (sizeof(int32_t)-1)) & ~(sizeof(int32_t)-1);
    siz = nbytes + sizeof(Hdr) + sizeof(int32_t);
    hdr = (Hdr *)calloc((unsigned int)siz,(unsigned int)1);  /* get some memory from OS */
    if (hdr == (Hdr *)0)
    {
        fprintf(stderr,"%%%s-F-FATAL, %s:%d Ran out of memory requesting %d bytes. Used %d so far.\n",
                macxx_name, file, line, siz, total_mem_used);
        fprintf(stderr,"%s",emsg);
        abort();
    }
    hdr->size = nbytes;
    hdr->file = file;
    hdr->line = line;
    hdr->magic = PRE_MAGIC;
    s = (char *)(hdr+1);
    end = (uint32_t *)(s+nbytes);
    *end = POST_MAGIC;
#if defined(DEBUG_MALLOC)
    if (top == 0)
    {
        top = hdr;
    }
    hdr->prev = bottom;
    if (bottom) bottom->next = hdr;
    bottom = hdr;
    hdr->next = 0;
    check_all();
#endif
    total_mem_used += siz;
    if (total_mem_used > peak_mem_used) peak_mem_used = total_mem_used;
    return s;
}

char *mem_realloc(char *old, int nbytes, char *file, int line) {
    char *s;
    Hdr *hdr;
#if defined(DEBUG_MALLOC)
    Hdr *prev, *next;
#endif
    int siz;
    uint32_t *end;

    if (old != 0)
    {
        hdr = (Hdr *)old-1;
        siz = hdr->size;
        if (hdr->magic != PRE_MAGIC)
        {
            fprintf(stderr, "%%%s-F-FATAL, %s:%d realloc'd %p with corrupted header.\n",
                    macxx_name, file, line, old);
            abort();
        }
        total_mem_used -= siz+sizeof(Hdr)+sizeof(int32_t);
        end = (uint32_t *)(old+hdr->size);
        if (*end != POST_MAGIC)
        {
            fprintf(stderr, "%%%s-F-FATAL, %s:%d realloc'd %p with corrupted trailer.\n",
                    macxx_name, file, line, old);
            if (hdr->line > 0) fprintf(stderr, "              area allocated by %s:%d\n", hdr->file, hdr->line);
            abort();
        }
#if defined(DEBUG_MALLOC)
        check_all();
        next = hdr->next;
        prev = hdr->prev;
#endif
        *end = 0;

        nbytes = (nbytes + (sizeof(int32_t)-1)) & ~(sizeof(int32_t)-1);
        siz = nbytes + sizeof(Hdr) + sizeof(int32_t);
        s = (char *)realloc((char *)hdr, siz);
        if (s == NFG)
        {
            fprintf(stderr,"%%%s-F-FATAL, %s:%d ran out of memory realloc'ing %d bytes to %d. Used %d so far.\n",
                    macxx_name, file, line, hdr->size, siz, total_mem_used);
            fprintf(stderr, "%s", emsg);
            abort();
        }
        hdr = (Hdr *)s;
        hdr->file = file;
        hdr->line = line;
        hdr->magic = PRE_MAGIC;
        if (nbytes > hdr->size)
        {     /* 0 the newly alloc'd  area */
            s = (char *)(hdr+1)+hdr->size;
            memset(s, 0, nbytes-hdr->size);
        }
        s = (char *)(hdr+1);
        end = (uint32_t *)(s+nbytes);
        *end = POST_MAGIC;
        hdr->size = nbytes;
#if defined(DEBUG_MALLOC)
        hdr->next = next;
        hdr->prev = prev;
        if (next) next->prev = hdr;
        if (prev) prev->next = hdr;
        if (top == prev) top = hdr;
#endif
        total_mem_used += siz;
        if (total_mem_used > peak_mem_used) peak_mem_used = total_mem_used;
        return s;
    }
    else
    {
        return mem_alloc(nbytes, file, line);
    }
}
