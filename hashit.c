/*
    hashit.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

/**********************************************************
 *
 * int hashit(input_string,hash_table_size)
 * char *input_string
 * unsigned int hash_table_size
 * 
 * Hashes the input string to a int sutiable for use as an
 * index into the hash table. The string must be null terminated.
 *
 *******************************************************************/

/* Entry */

int hashit( char *strng, int hash_size )
{
    unsigned int hashv;
    unsigned char c;
    hashv = 0;
    while ((c= *strng++) != 0)
    {
        hashv = hashv*11 + c;
    }
    return hashv % hash_size;
}
