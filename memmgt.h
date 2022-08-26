/*
    memmgt.h - Part of llf, a cross linker. Part of the macxx tool chain.
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

#if !defined(_MEMMGT_H_)
#define _MEMMGT_H_

extern char *mem_alloc(int size, char *file, int line);	/* external memory allocation routine */
extern char *mem_realloc(char *old, int size, char *file, int line);
extern int   mem_free(char *old, char *file, int line);
#define MEM_alloc(size) mem_alloc(size, __FILE__, __LINE__)
#define MEM_calloc(cnt,size) mem_alloc((cnt)*(size), __FILE__, __LINE__)
#define MEM_malloc(size) mem_alloc(size, __FILE__, __LINE__)
#define MEM_free(area) mem_free((char *)(area), __FILE__, __LINE__)
#define MEM_realloc(area, size) mem_realloc((char *)(area), size, __FILE__, __LINE__)

#endif


