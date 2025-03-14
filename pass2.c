/*
	pass2.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include <stdio.h>		/* get standard I/O definitions */
#include <string.h>
#include <errno.h>
#include <string.h>
#include "token.h"		/* define compile time constants */
#include "structs.h"		/* define structures */
#include "header.h"

#include "vlda_structs.h"

static struct exp_stk tmp_expr;
long tmp_pool_used;
static char *last_tmp_org;
static short tmp_length;

#if !defined(INLINE)
	#define INLINE
#endif

#if ALIGNMENT > 0
	#define LAY1(ptr,value) (*ptr.b8++ = (unsigned char)(value))
	#if 0
static INLINE char *lay2(char *p, int value)
{
	*p++ = value;
	*p++ = value>>8;
	return p;
}
static INLINE char *lay4(char *p, unsigned long value)
{
	*p++ = value;
	*p++ = (value >>= 8);
	*p++ = (value >>= 8);
	*p++ = (value >>= 8);
	return p;
}
		#define LAY2(ptr,value) (ptr.b8 = lay2(ptr.b8, (int)value))
		#define LAY4(ptr,value) (ptr.b8 = lay4(ptr.b8, (long)value))
	#else
		#define LAY2(ptr,value) do { int vv=(value); *ptr.b8++ = (char)(vv); *ptr.b8++ = (char)(vv>>8); } while (0)
		#define LAY4(ptr,value) do { int vv=(value); *ptr.b8++ = (char)(vv); *ptr.b8++ = (char)(vv >>= 8); \
    					      *ptr.b8++ = (char)(vv >>= 8); *ptr.b8++ = (char)(vv >>= 8); \
    			    } while (0)
	#endif
	#define PICK1(ptr) (*ptr.b8++)
	#define PICK2(ptr) (ptr.ub8 += 2, ptr.ub8[-2] | (ptr.ub8[-1] << 8))
	#define PICK4(ptr) (ptr.ub8 += 4, ptr.ub8[-4] | (ptr.ub8[-3] << 8) | (ptr.ub8[-2] << 16) | (ptr.ub8[-1] << 24))
	#define AMASK ((1<<ALIGNMENT)-1)
	#define ALIGN(ptr) (((1<<ALIGNMENT) - ((int)(ptr)&AMASK)) & AMASK)
#else
	#define LAY1(ptr,value) *ptr.b8++ = (unsigned char)(value)
	#define LAY2(ptr,value) *ptr.b16++ = (unsigned short)(value)
	#define LAY4(ptr,value) *ptr.b32++ = (unsigned long)(value)
	#define PICK1(ptr) (*ptr.b8++)
	#define PICK2(ptr) (*ptr.b16++)
	#define PICK4(ptr) (*ptr.b32++)
	#define ALIGN(ptr) 0
#endif

static struct tmp_struct
{
	unsigned char tf_type;
	char tf_tag;
	union
	{
		struct tmp_struct *tf_lin;
		long tf_len;
	} tf_ll;
} rtmp,*tmp_ptr = 0;

#define tf_length tf_ll.tf_len
#define tf_link   tf_ll.tf_lin

static struct tmp_struct *tmp_next = 0, *tmp_top = 0, *tmp_pool = 0;
static int tmp_pool_size;
#if 0
extern long total_mem_used;
#endif
static struct ss_struct *last_segment = 0;

char* sqz_it(char *src, int typ, long cnt, int siz)
{
	union
	{
		char *b8;
		short *b16;
		long *b32;
		long l;
	} sqz;
	register long value;
	register int itz;

	sqz.b8 = ((char *)tmp_pool) + 1;   /* setup the squeezer */
	switch (typ & 0xFF)
	{          /* and test for type */
	case TMP_EOF:
		break;      /* this is easy */
	case TMP_ASTNG:           /* these are strings */
	case TMP_BSTNG:
		{
			itz = cnt * siz;
			if ( (value = itz) != 0 )
			{
				if ( itz < 0 )
					value = -itz;
				if ( value < 128 )
				{
					LAY1(sqz, value);
					typ |= TMP_B8;
				}
				else
				{
					LAY2(sqz, value);
					typ |= TMP_B16;
				}
			}
			memcpy(sqz.b8, src, itz);    /* move the items */
			sqz.b8 += itz;     /* adjust the pointer */
			break;         /* done */
		}
	case TMP_TAG:
		{
			LAY1(sqz, *src);    /* stuff in the tag character */
			if ( (value = cnt) != 0 )
			{
				if ( cnt < 0 )
					value = -cnt;  /* get abs(cnt) */
				if ( value < 128 )
				{
					LAY1(sqz, value);    /* fits in a byte */
					typ |= TMP_B8;
				}
				else
				{
					LAY2(sqz, value);  /* fits in a word */
					typ |= TMP_B16;
				}
			}
			break;
		}
	case TMP_ORG:
	case TMP_EXPR:
	case TMP_OOR:
	case TMP_BOFF:
	case TMP_TEST:
	case TMP_START:
		{
			struct expr_token *texp;
			texp = (struct expr_token *)src; /* point to expression area */
			typ |= TMP_B8;     /* always 1 byte count */
			LAY1(sqz, cnt);    /* stuff in the item count */
			while ( cnt-- )
			{    /* do all the elements */
				switch (texp->expr_code)
				{
				case EXPR_OPER:
					{
						LAY1(sqz, EXPR_OPER | TMP_NNUM);
						LAY1(sqz, texp->expr_value);
						if ( (char)texp->expr_value == '!' )
						{
							LAY1(sqz, texp->expr_value >> 8);
						}
						break;
					}
				case EXPR_VALUE:
					{
						unsigned long val;
						val = (texp->expr_value >= 0) ? texp->expr_value : -texp->expr_value;
						if ( val < 64l )
						{
							LAY1(sqz, (texp->expr_value & TMP_NUM));
							break;
						}
						if ( val < 128l )
						{
							LAY1(sqz, EXPR_VALUE | TMP_B8 | TMP_NNUM);
							LAY1(sqz, texp->expr_value);
							break;
						}
						if ( val < 32768l )
						{
							LAY1(sqz, EXPR_VALUE | TMP_B16 | TMP_NNUM);
							LAY2(sqz, texp->expr_value);
							break;
						}
						LAY1(sqz, EXPR_VALUE | TMP_B32 | TMP_NNUM);
						LAY4(sqz, texp->expr_value);
						break;
					}
				case EXPR_IDENT:
				case EXPR_SYM:
					{
						unsigned long v;
						if ( texp->expr_code == EXPR_SYM )
						{
							LAY1(sqz, EXPR_SYM | TMP_B32 | TMP_NNUM);
							LAY4(sqz, (long)texp->expr_ptr);
						}
						else
						{
							v = (unsigned long)texp->expr_ptr;
							if ( v < 128l )
							{
								LAY1(sqz, EXPR_IDENT | TMP_B8 | TMP_NNUM);
								LAY1(sqz, v);
							}
							else if ( v < 32768l )
							{
								LAY1(sqz, EXPR_IDENT | TMP_B16 | TMP_NNUM);
								LAY2(sqz, v);
							}
							else
							{
								LAY1(sqz, EXPR_IDENT | TMP_B32 | TMP_NNUM);
								LAY4(sqz, v);
							}
						}
						v = (texp->expr_value > 0) ? texp->expr_value : -texp->expr_value;
						if ( v < 126 )
						{
							LAY1(sqz, texp->expr_value);
							break;
						}
						if ( v < 32768 )
						{
							LAY1(sqz, 126);
							LAY2(sqz, texp->expr_value);
							break;
						}
						LAY1(sqz, 127);
						LAY4(sqz, texp->expr_value);
						break;
					}        /* -- case */
				case EXPR_L:
				case EXPR_B:
					{
						LAY1(sqz, texp->expr_code | TMP_B32 | TMP_NNUM);
						LAY4(sqz, (long)texp->expr_ptr);
					}        /* -- case */
				}           /* -- switch expr_code */
				++texp;
			}          /* -- while expr */
			break;
		}             /* -- case TMP_EXPR */
	default:
		{
			sprintf(emsg, "Internal error sqz'ing; Unrecognised TMP code of 0x%02X",
					typ);
			err_msg(MSG_ERROR, emsg);
		}
	}                /* -- switch TMP_TYPE */
	tmp_pool->tf_type = typ; /* stuff in the typ code */
	return (sqz.b8);
}

char* unsqz_it(char *src)
{
	union
	{
		unsigned char *ub8;
		char *b8;
		short *b16;
		long *b32;
		struct tmp_struct *tsp;
		long l;
	} sqz;
	int typ, i;

	tmp_ptr = &rtmp;         /* point to local tmp_struct */
	rtmp.tf_length = 0;          /* assume a length of 0 */
	sqz.b8 = src;            /* setup the squeezer */
	typ = PICK1(sqz);
	rtmp.tf_type = typ & ~TMP_SIZE;
	switch (rtmp.tf_type)
	{      /* and test for type */
	default:
		{
			sprintf(emsg, "Internal error unsqz'ing; Unknown code of %02X",
					rtmp.tf_type);
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
	case TMP_EOF:
		return (sqz.b8);     /* this is easy */
	case TMP_ASTNG:           /* these are strings */
	case TMP_BSTNG:
		{
			typ &= TMP_SIZE;
			switch (typ)
			{
			case TMP_ZERO:
				break;
			case TMP_B8:
				{
					rtmp.tf_length = PICK1(sqz);
					break;
				}
			case TMP_B16:
				{
					rtmp.tf_length = PICK2(sqz);
					break;
				}
			case TMP_B32:
				{
					rtmp.tf_length = PICK4(sqz);
					break;
				}
			}
			tmp_pool = sqz.tsp;
			return (sqz.b8 + rtmp.tf_length);
		}
	case TMP_TAG:
		{
			rtmp.tf_tag = PICK1(sqz);  /* pickup the tag code */
			typ &= TMP_SIZE;
			switch (typ)
			{
			case TMP_ZERO:
				break;
			case TMP_B8:
				{
					rtmp.tf_length = PICK1(sqz);
					break;
				}
			case TMP_B16:
				{
					rtmp.tf_length = PICK2(sqz);
					break;
				}
			case TMP_B32:
				{
					rtmp.tf_length = PICK4(sqz);
					break;
				}
			}
			return (sqz.b8);
		}
	case TMP_ORG:
	case TMP_EXPR:
	case TMP_OOR:
	case TMP_BOFF:
	case TMP_TEST:
	case TMP_START:
		{
			register struct expr_token *texp;
			int cnt;
			cnt = rtmp.tf_length = PICK1(sqz); /* get the # of elements */
			expr_stack_ptr = tmp_expr.len = cnt;
			texp = tmp_expr.ptr = expr_stack;  /* point to expression stack */
			for (; cnt; --cnt, ++texp )
			{  /* do all the elements */
				i = PICK1(sqz) & 0xFF;  /* pickup the type code */
				if ( (i & TMP_NNUM) == 0 )
				{
					texp->expr_code = EXPR_VALUE;    /* type is abs value */
					if ( i & (TMP_NNUM / 2) )
						i |= -TMP_NNUM;    /* sign extend */
					texp->expr_value = i;
					continue;
				}
				texp->expr_code = i & ~(TMP_SIZE | TMP_NNUM);
				i &= TMP_SIZE;
				switch (texp->expr_code)
				{
				case EXPR_OPER:
					{
						texp->expr_value = PICK1(sqz);    /* get the operator character */
						if ( texp->expr_value == '!' )
						{
							texp->expr_value |= PICK1(sqz) << 8;
						}
						continue;
					}
				case EXPR_IDENT:
				case EXPR_VALUE:
					{
						switch (i)
						{
						case TMP_ZERO:
							{
								texp->expr_value = 0;
								break;
							}
						case TMP_B8:
							{
								texp->expr_value = PICK1(sqz);
								break;
							}
						case TMP_B16:
							{
								texp->expr_value = PICK2(sqz);
								break;
							}
						case TMP_B32:
							{
								texp->expr_value = PICK4(sqz);
								break;
							}
						}
						if ( texp->expr_code != EXPR_IDENT )
							continue;
						texp->expr_ptr = (struct ss_struct *)texp->expr_value;
						/* fall through to EXPR_SYM */
					}
				case EXPR_SYM:
					{
						int v;
						if ( texp->expr_code == EXPR_SYM )
						{ /* may come here from IDENT */
							texp->expr_ptr = (struct ss_struct *)PICK4(sqz);
						}
						v = PICK1(sqz);
						if ( v == 126 )
						{
							texp->expr_value = PICK2(sqz);
							continue;
						}
						if ( v == 127 )
						{
							texp->expr_value = PICK4(sqz);
							continue;
						}
						texp->expr_value = v;
						continue;
					}        /* -- case */
				case EXPR_L:
				case EXPR_B:
					{
						texp->expr_value = PICK4(sqz);
						continue;
					}        /* -- case */
				}           /* -- switch expr_code */
			}          /* -- for() expr */
		}             /* -- case TMP_EXPR */
	}                /* -- switch TMP_TYPE */
	return (sqz.b8);
}

/********************************************************************
 * Write a bunch of data to the temp file
 */
void write_to_tmp(int typ, long itm_cnt, char *itm_ptr, int itm_siz)
/*
 * At entry:
 *	typ - TMP_xxx value id'ing the block data
 *	itm_cnt - number of items to write
 *	itm_ptr - pointer to items to write (or tag character)
 *	itm_siz - size in bytes of each item (or tag number)
 * At exit:
 *	data written to temp file (exits to VMS if error)
 */
{
	union
	{
		char *c;
		long *l;
		struct tmp_struct *t;
		long lng;
	} src,dst,tmp;       /* mem pointers */
	register int itz;

	if ( !output_files[OUT_FN_ABS].fn_present )
		return; /* nuthin' to do if no ABS wanted */
	if ( tmp_pool_size == 0 )
	{
		tmp_pool_size = MAX_TOKEN * 8;      /* get some memory */
		tmp_pool_used += MAX_TOKEN * 8;
		tmp_pool = (struct tmp_struct *)MEM_alloc(tmp_pool_size);
		tmp_top = tmp_pool;       /* remember where the top starts */
	}
	tmp.t = tmp_pool;
	itz = (typ == TMP_TAG) ? 0 : itm_cnt * itm_siz;
	itz += ALIGN(itz);
	if ( tmp_fp == 0 )
	{
		int tsiz;
		tsiz = 2 * sizeof(struct tmp_struct) + itz;
		if ( tmp_pool_size < tsiz )
		{
			if ( tsiz < MAX_TOKEN * 8 )
				tsiz = MAX_TOKEN * 8;
			tmp.t->tf_type = TMP_LINK; /* link to a new area */
			tmp.t->tf_link = (struct tmp_struct *)MEM_alloc(tsiz);
			tmp_pool_used += tsiz;
			tmp_pool_size = tsiz;
			tmp_pool = tmp.t->tf_link;
#if defined(DEBUG_LINK)
			printf("Writing TMP_LINK at %08lX, align=%d. New pool at %08lX, align=%d\n",
				   tmp.t, (int)tmp.t & 3, tmp_pool, (int)tmp_pool & 3);
#endif
			tmp.t = tmp_pool;
			last_tmp_org = (char *)0;
		}
		if ( typ == TMP_ORG )
		{
			if ( last_tmp_org != (char *)0 )
			{
				tmp_pool_size += tmp.c - last_tmp_org; /* put the bytes back in */
				tmp.c = last_tmp_org;
				tmp_pool = tmp.t;
			}
			else
			{
				last_tmp_org = tmp.c;
			}
		}
		else
		{
			last_tmp_org = (char *)0;
		}
		if ( !options->miser )
		{
			tmp.t->tf_type = typ;      /* set the type */
			tmp.t->tf_length = itm_cnt;    /* set the item count */
			if ( itm_ptr != (char *)0 )
				tmp.t->tf_tag = *itm_ptr;    /* in case type is tag */
			dst.t = tmp.t + 1;
			src.c = itm_ptr;
			memcpy(dst.c, src.c, itz);
			dst.c += itz;
		}
		else
		{
			dst.c = sqz_it((char *)itm_ptr, typ, itm_cnt, itm_siz);
		}
		dst.c += ALIGN(dst.c);
		tmp_pool_size -= dst.c - tmp.c;
		tmp_pool = dst.t;             /* update pointer */
	}
	else
	{
		int t;
		dst.c = sqz_it(itm_ptr, typ, itm_cnt, itm_siz);
		tmp_length = dst.c - (char *)tmp_top;
		t = fwrite((char *)&tmp_length, sizeof(tmp_length), 1, tmp_fp);
		if ( t != 1 )
		{
			sprintf(emsg, "Error fwrite'ing %d bytes (1 elem) to \"%s\", wrote %d: %s",
					(int)sizeof(tmp_length), output_files[OUT_FN_TMP].fn_buff,
					(int)(t * sizeof(tmp_length)), err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
		t = fwrite((char *)tmp_top, (int)tmp_length, 1, tmp_fp);
		if ( t != 1 )
		{
			sprintf(emsg, "Error fwrite'ing %d bytes (1 elem) to \"%s\", wrote %d: %s",
					tmp_length, output_files[OUT_FN_TMP].fn_buff,
					t * tmp_length, err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
	}
	return;
}

/**********************************************************************
 * Read a bunch of data from tmp file
 */
int read_from_tmp(void)
/*
 * At entry:
 * At exit:
 */
{
	struct tmp_struct *ts;
	char *tmps;
	int code;

	if ( tmp_fp != 0 )
	{
		int t;
		t = fread((char *)&tmp_length, sizeof(tmp_length), 1, tmp_fp);
		if ( t != 1 )
		{
			sprintf(emsg, "Error fread'ing \"%s\". Wanted %d, got %d: %s",
					output_files[OUT_FN_TMP].fn_buff, (int)sizeof(tmp_length),
					(int)(t * sizeof(tmp_length)), err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
		t = fread((char *)tmp_top, (int)tmp_length, 1, tmp_fp);
		if ( t != 1 )
		{
			sprintf(emsg, "Error fread'ing \"%s\". Wanted %d, got %d: %s",
					output_files[OUT_FN_TMP].fn_buff, tmp_length,
					t * tmp_length, err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
		tmps = (char *)unsqz_it((char *)tmp_top); /* unpack the text */
		tmps += ALIGN(tmps);
		tmp_next = (struct tmp_struct *)tmps;
		return (rtmp.tf_type);
	}
	else
	{
		ts = tmp_next;  /* point to next tmp element */
		if ( ts->tf_type == TMP_LINK )
		{
			int ferr;
#if defined(DEBUG_LINK)
			printf("Reading  TMP_LINK at %08lX, align=%d. New link at %08lX, align=%d\n",
				   ts, (int)ts & 3, ts->tf_length, (int)ts->tf_length & 3);
#endif
			ts = tmp_next = (struct tmp_struct *)ts->tf_length;
			if ( (ferr = MEM_free(tmp_top)) )
			{ /* give back the memory */
				sprintf(emsg, "Error (%08X) free'ing %d bytes at %08lX from tmp_pool",
						ferr, MAX_TOKEN * 8, (unsigned long)tmp_top);
				err_msg(MSG_WARN, emsg);
			}
			tmp_top = ts;          /* point to next top */
		}
		tmp_ptr = ts;             /* point to tmp pointer */
		if ( options->miser )
		{
			tmps = (char *)unsqz_it((char *)tmp_ptr); /* unpack the text */
			tmps += ALIGN(tmps);
#if defined(DEBUG_LINK)
			if ( (long)tmp_ptr & 3 )
				printf("read_from_tmp: started at unaligned %08lX\n", (char *)tmp_ptr);
			if ( (long)tmps & 3 )
				printf("read_from_tmp: ended unaligned at %08lX\n", (char *)tmps);
#endif
			tmp_next = (struct tmp_struct *)tmps;
			return (rtmp.tf_type);
		}
		else
		{
			++ts;
			tmps = (char *)ts;
			tmp_pool = ts;
			code = tmp_ptr->tf_type;
			switch (code)
			{
			case TMP_TAG:
				{
					++tmp_next;
					break;
				}
			case TMP_START:
			case TMP_EXPR:
			case TMP_OOR:
			case TMP_BOFF:
			case TMP_TEST:
			case TMP_ORG:
				{
					tmp_expr.len = expr_stack_ptr = tmp_ptr->tf_length;
					tmp_expr.ptr = (struct expr_token *)ts;
					tmps += expr_stack_ptr * sizeof(struct expr_token);
					tmp_next = (struct tmp_struct *)tmps;
					break;
				}
			case TMP_BSTNG:
			case TMP_ASTNG:
				{
					tmps += tmp_ptr->tf_length;
					tmps += ALIGN(tmps);
					tmp_next = (struct tmp_struct *)tmps;
					break;
				}               /* -- case */
			case TMP_EOF:
				{
					break;
				}
			default:
				{
					sprintf(emsg, "Internal error: Unrecognised TMP code of %02X",
							tmp_ptr->tf_type);
					err_msg(MSG_ERROR, emsg);
				}
			}              /* -- switch */
		}                 /* -- if miser */
	}                    /* -- if fp */
	return (code);
}

static void rewind_tmp(void)
{
	if ( tmp_fp )
	{
#ifdef VMS
		if ( fseek(tmp_fp, 0, 0) )
		{   /* rewind temp file */
			sprintf(emsg, "Unable to rewind \"%s\": %s",
					output_files[OUT_FN_TMP].fn_buff, err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
#else
		fclose(tmp_fp);       /* close the temp file */
		if ( (tmp_fp = fopen(output_files[OUT_FN_TMP].fn_buff, "rb")) == 0 )
		{
			sprintf(emsg, "Error opening tmp file \"%s\" for output: %s",
					output_files[OUT_FN_TMP].fn_buff, err2str(errno));
			err_msg(MSG_FATAL, emsg);
			EXIT_FALSE;
		}
#endif
	}
	else
	{
		tmp_next = tmp_top;
	}
	return;
}

static unsigned long pass2_pc = 0;

static void disp_offset(void)
{
	char *s1, *s2;
	if ( options->octal )
	{
		s1 = "\t%06lo (%06lo bytes offset from segment {%s} of file %s)\n";
		s2 = "\tat location %010lo\n";
	}
	else
	{
		s1 = "\t%08lX (%04lX bytes offset from segment {%s} of file %s)\n";
		s2 = "\tat location %08lX\n";
	}
	if ( last_segment )
	{
		sprintf(emsg, s1,
				pass2_pc, pass2_pc - last_segment->ss_value, last_segment->ss_string,
				last_segment->ss_fnd->fn_name_only);
	}
	else
	{
		sprintf(emsg, s2, pass2_pc);
	}
	err_msg(MSG_CONT, emsg);
}

/**********************************************************************
 * Display truncation error
 */
static void trunc_err(long lowLimit, long highLimit, long written)
/* 
 * At entry:
 *	mask - mask to compare against
 *	token_value - requested value to write
 *	pass2_pc - current location counter
 *	last_xxx_ref - points to last seg/sym referenced in expression
 */
{
	char *s1,*sign;
	if ( lowLimit < 0 )
	{
		sign = "-";
		lowLimit = -lowLimit;
	}
	else
		sign = "";
	if ( options->octal )
		s1 = "Truncation at location %lo. Expected %s%lo < %lo < %lo. Written: %lo";
	else
		s1 = "Truncation at location %0lX. Expected %s%lX < %0lX < %0lX. Written: %0lX";
	sprintf(emsg, s1, pass2_pc, sign, lowLimit, token_value, highLimit, written);
	err_msg(MSG_WARN, emsg);
	disp_offset();       /* display error offset */
	return;
}

long xfer_addr = 1;
FN_struct *xfer_fnd;
static int noout_flag;

/**********************************************************************
 * Pass2 - generate output
 */
int pass2(void)
/*
 * At entry:
 *	no requirements, called from mainline
 * At exit:
 *	output file written.
 */
{
	int i, r_flg = 1, flip;
	long lc;
#if (0)
	char *src,*dst,c;
#endif
	unsigned char *ubp;

	if ( (outxabs_fp = abs_fp) == 0 )
		return (1); /* no output required */
	write_to_tmp(TMP_EOF, 0, (char *)0, 0); /* make sure that there's an EOF in tmp */
	rewind_tmp();        /* rewind to beginning */
	while ( 1 )
	{
		if ( r_flg )
			read_from_tmp();  /* now read the whole thing in */
		r_flg = 1;        /* gotta read next time */
		switch (tmp_ptr->tf_type)
		{ /* see what we gotta do */
		case TMP_EOF:
			{
				termobj(xfer_addr);     /* terminate and flush the output buffer */
				if ( !tmp_fp )
				{
					int ferr;
					if ( (ferr = MEM_free(tmp_top)) )
					{ /* give back the memory */
						sprintf(emsg, "Error (%08X) free'ing %d bytes at %08lX from tmp_pool",
								ferr, MAX_TOKEN * 8, (unsigned long)tmp_top);
						err_msg(MSG_WARN, emsg);
					}
					tmp_top = 0;
				}
				return (1);     /* done with pass2 */
			}
		case TMP_OOR:
		case TMP_BOFF:
		case TMP_TEST:
			{
				int condit, tmlen, savedType;
				char *tmsg;
				char *cmd_type;

				savedType = tmp_ptr->tf_type;
				cmd_type = "TEST";
				if ( savedType == TMP_BOFF )
					cmd_type = "BOFF";
				else if ( savedType == TMP_OOR )
					cmd_type = "OOR";
				read_from_tmp();
				if ( tmp_ptr->tf_type != TMP_ASTNG )
				{
					sprintf(emsg, "Internal error processing .%s directive", cmd_type);
					err_msg(MSG_ERROR, emsg);
					r_flg = 0;
					break;
				}
				while ( 1 )
				{
					tmlen = tmp_ptr->tf_length;
					tmsg = (char *)MEM_alloc(tmlen + 1);
					strncpy(tmsg, (char *)tmp_pool, tmlen);
					*(tmsg + tmlen) = 0;   /* null terminate the string */
					condit = evaluate_expression(&tmp_expr);
					if ( !condit )
					{
						sprintf(emsg, "Invalid expression in .%s: %s", cmd_type, tmsg);
						err_msg(MSG_ERROR, emsg);
						MEM_free(tmsg);
						break;
					}
					condit = 1;          /* assume true */
					if ( tmp_expr.len == 1 && tmp_expr.ptr->expr_code == EXPR_VALUE )
					{
						condit = tmp_expr.ptr->expr_value;
					}
					else
					{
						if ( options->rel )
						{
							outtstexp(tmp_ptr->tf_type, (char *)tmp_pool, (int)tmp_ptr->tf_length, &tmp_expr);
							MEM_free(tmsg);
							break;
						}
						else
						{
							sprintf(emsg, "Expression .%s not absolute: %s", cmd_type, tmsg);
							err_msg(MSG_ERROR, emsg);
						}
						condit = 0;
					}
					if ( condit )
					{
						if ( savedType == TMP_TEST )
						{
							err_msg(MSG_ERROR, tmsg);
						}
						else
						{
							if ( savedType == TMP_BOFF )
							{
								strcpy(emsg, "Branch offset misaligned or out of range in ");
							}
							else
							{
								strcpy(emsg, "Operand misaligned or out of range in ");
							}
							strcat(emsg, tmsg);
							err_msg(MSG_ERROR, emsg);
						}
					}
					MEM_free(tmsg);
					break;
				}
				break;
			}
		case TMP_EXPR:
			{
				if ( noout_flag == 0 )
				{
					if ( !evaluate_expression(&tmp_expr) )
					{
						disp_offset();
					}
				}
				read_from_tmp();    /* get the next token */
				if ( noout_flag != 0 )
					break;
				lc = 1;     /* assume only 1 thing to output */
				i = 4;      /* assume type l (LONG, low byte first) */
				if ( tmp_ptr->tf_type == TMP_TAG )
				{
					unsigned char mea_buf[4];   /* for endian-agnostic output */
					if ( (lc = tmp_ptr->tf_length) == 0 )
						lc = 1; /* get the count */
					flip = 0;        /* assume not to flip it */
					if ( options->rel )
					{
						if ( tmp_expr.len != 1 || lc != 1
							 || tmp_expr.ptr->expr_code != EXPR_VALUE )
						{
							flushobj();    /* flush the object file */
							if ( options->vldadef )
							{
								union vexp ve;
								ve.vexp_chp = eline;
								*ve.vexp_type++ = VLDA_EXPR;
								outexp(&tmp_expr, ve.vexp_chp, tmp_ptr->tf_tag, lc, eline, abs_fp);
							}
							else
							{
								outexp(&tmp_expr, eline, tmp_ptr->tf_tag, lc, eline + 1, abs_fp);
							}
							break; /* exit the TMP_xxx switch */
						}
						token_value = tmp_expr.ptr->expr_value;
					}
					ubp = mea_buf;
					switch (tmp_ptr->tf_tag)
					{
					case 'j':
						{       /* long, high byte of low word first */
#if (0)
							unsigned char b0,b1,b2,b3; /* bytes in long */
							b0 = token_value&0xFF; /* pickup individual bytes */
							b1 = (token_value>>8)&0xFF;
							b2 = (token_value>>16)&0xFF;
							b3 = (token_value>>24)&0xFF;
							token_value = (b2<<24) | (b3<<16) | (b0<<8) | (b1);
#else
							ubp[0] = (token_value >> 8) & 0xFF;
							ubp[1] = (token_value)&0xFF;
							ubp[2] = (token_value >> 24) & 0xFF;
							ubp[3] = (token_value >> 16) & 0xFF;
#endif
							i = 4;
							break;
						}
					case 'J':
						{       /* long, low byte of high word first */
#if (0)
							unsigned char b0,b1,b2,b3; /* bytes in long */
							b0 = token_value&0xFF; /* pickup individual bytes */
							b1 = (token_value>>8)&0xFF;
							b2 = (token_value>>16)&0xFF;
							b3 = (token_value>>24)&0xFF;
							token_value = (b1<<24) | (b0<<16) | (b3<<8) | (b2);

#else
							ubp[0] = (token_value >> 16) & 0xFF;
							ubp[1] = (token_value >> 24) & 0xFF;
							ubp[2] = (token_value)&0xFF;
							ubp[3] = (token_value >> 8) & 0xFF;
#endif
							i = 4;
							break;
						}
					case 'L':   /* long, high byte first */
#if (0)
						flip=1;
#else
						flip = 3;
#endif
						/* merge with little-endian */
					case 'l':   /* long, low byte first */
						ubp[flip] = (token_value)&0xFF;
						ubp[flip ^ 1] = (token_value >> 8) & 0xFF;
						ubp[flip ^ 2] = (token_value >> 16) & 0xFF;
						ubp[flip ^ 3] = (token_value >> 24) & 0xFF;
						i = 4;     /* 4 bytes */
						break;     /* take easy out */
					case 'W':
						flip = 1;   /* short, high byte first */
					case 'w':
						{
							if ( (token_value > 65535) || (token_value < -65536) )
								trunc_err(-65536L, 65536L, token_value&65535);
							ubp[flip] = (token_value)&0xFF;
							ubp[flip ^ 1] = (token_value >> 8) & 0xFF;
							i = 2;
							break;
						}
					case 'I':
						flip = 1;   /* short, high byte first */
					case 'i':
						{       /* short, low byte first */
							i = 2;     /* 2 bytes */
							if ( (token_value > 32767) || (token_value < -32768) )
								trunc_err(-32678, 32768, token_value&65535L);
							ubp[flip] = (token_value)&0xFF;
							ubp[flip ^ 1] = (token_value >> 8) & 0xFF;
							break;
						}
					case 'U':
						flip = 1;   /* short, high byte first */
					case 'u':
						{       /* short, low byte first */
							i = 2;     /* 2 bytes */
							if ( token_value & 0xFFFF0000 )
								trunc_err(0,65536L,token_value&65535L);
							ubp[flip] = (token_value)&0xFF;
							ubp[flip ^ 1] = (token_value >> 8) & 0xFF;
							break;
						}
					case 'b':
						{       /* byte is the same as signed char */
							i = 1;     /* char */
							if ( (token_value > 255) || (token_value < -128) )
								trunc_err(-128,256,token_value&255);
							*ubp = (token_value)&0xFF;
							break;
						}
					case 'z':
						{
							i = 1;     /* signed char branch offset */
							if ( token_value == 0 || (token_value&1) )
							{
								const char *s1;
								if ( !token_value )
								{
									if ( options->octal )
										s1 = "Illegal branch offset of %lo at location %lo. Replaced with -2";
									else
										s1 = "Illegal branch offset of %lX at location 0x%X. Replaced with -2";
								}
								else
								{
									if ( options->octal )
										s1 = "Illegal odd branch offset of %lo at location %lo. Replaced with -2";
									else
										s1 = "Illegal odd branch offset of 0x%lX at location 0x%X. Replaced with -2";
								}
								sprintf(emsg, s1, token_value, pass2_pc );
								err_msg(MSG_ERROR, emsg);
								disp_offset();       /* display error offset */
								*ubp = -2;
								break;
							}
						}
						/* FALL THROUGH TO 's' */
					case 's':
						{
							i = 1;     /* signed char */
							if ( (token_value > 127) || (token_value < -128) )
								trunc_err(-128,128,token_value&255L);
							*ubp = (token_value)&0xFF;
							break;
						}
					case 'c':
						{       /* char */
							i = 1;
							if ( token_value & 0xFFFFFF00 )
								trunc_err(0,256,token_value&255L);
							*ubp = (token_value)&0xFF;
							break;
						}
					}
					/* Code as written cannot work on Big-endian machine,
					 * so hard-code the option which was always used.
					 * Otherwise, the duplicate open-brace messes up
					 * brace-matching.
					 */
#if (0)
					if (flip == 1)
					{
						dst = (char *)&token_value; /* the cast is so LINT won't gripe */
						src = dst+i-1;
						while (dst < src)
						{
							c = *src;
							*src-- = *dst;
							*dst++ = c;
						}
					}
#endif
					pass2_pc += i * lc;        /* update location counter */
					while ( lc-- > 0 )
					{
#if (0)
						outbstr((unsigned char *)&token_value,i); /* write the text */
#else
						outbstr(ubp, i); /* write the text */
#endif
					}
				}
				else
				{
					r_flg = 0;       /* don't read next time */
					if ( options->rel )
					{
						outexp(&tmp_expr, eline, 0, 0l, eline, abs_fp);
					}
				}
				break;
			}
		case TMP_BSTNG:
		case TMP_ASTNG:
			{
				if ( noout_flag == 0 )
					outbstr((unsigned char *)tmp_pool, (int)tmp_ptr->tf_length);
				pass2_pc += tmp_ptr->tf_length;
				break;
			}
		case TMP_ORG:
			{
				struct seg_spec_struct *seg_ptr;
				last_seg_ref = 0;   /* assume no segment references */
				noout_flag = 0;
				if ( !evaluate_expression(&tmp_expr) )
				{
					if ( options->octal )
					{
						sprintf(emsg, "ORG may be incorrectly set to %010lo\n",
								token_value);
					}
					else
					{
						sprintf(emsg, "ORG may be incorrectly set to %08lX\n",
								token_value);
					}
					err_msg(MSG_CONT, emsg);
				}
				if ( last_seg_ref != 0 )
				{
					last_segment = last_seg_ref; /* remember which segment we're in */
					seg_ptr = last_segment->seg_spec;
					noout_flag = last_segment->flg_noout;
					if ( noout_flag == 0 )
					{
						outorg(pass2_pc = token_value + seg_ptr->seg_offset, &tmp_expr);
					}
				}
				else
				{
					outorg(pass2_pc = token_value, &tmp_expr);
				}
				break;
			}          /* -- case */
		case TMP_START:
			{
				if ( !evaluate_expression(&tmp_expr) )
				{
					if ( options->octal )
					{
						sprintf(emsg, "XFER addr may be incorrectly set to %010lo\n",
								token_value);
					}
					else
					{
						sprintf(emsg, "XFER addr may be incorrectly set to %08lX\n",
								token_value);
					}
					err_msg(MSG_CONT, emsg);
				}
				if ( options->rel )
				{
					outxfer(&tmp_expr, abs_fp);
				}
				else
				{
					xfer_addr = token_value;
				}
				break;
			}          /* -- case */
		default:
			{
				sprintf(emsg, "Undefined code byte found in tmp file: 0x%02X (%d)\n",
						tmp_ptr->tf_type, tmp_ptr->tf_type);
				err_msg(MSG_FATAL, emsg);
				EXIT_FALSE;
			}          /* -- case */
		}             /* -- switch */
	}                /* -- while */
}               /* -- pass2 */
