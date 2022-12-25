/*
    gcstruct.h - Part of llf, a cross linker. Part of the macxx tool chain.
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

#ifndef _GCSTRUCT_H_
#define _GCSTRUCT_H_

struct qual {
   unsigned int noval:1;	/* value is not allowed */
   unsigned int optional:1;	/* value is optional */
   unsigned int number:1;	/* value must be a number */
   unsigned int output:1;	/* parameter is an output file */
   unsigned int error:1;	/* field is in error */
   unsigned int negate:1;	/* value is negatable */
   unsigned int negated:1;	/* option is negated */
   unsigned int present:1;	/* up to next byte boundary */
   unsigned char index;		/* output file index */
   char *string;		/* pointer to qualifier ascii string */
   char *value;			/* pointer to user's value string */
   int qualif;			/* value to .or. into cli_options */
};
extern struct qual qual_tbl[];
extern const int max_qual;

#endif /* _GCSTRUCT_H_ */
