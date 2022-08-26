/*
    err2str.c - Part of llf, a cross linker. Part of the macxx tool chain.
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

#include <errno.h>
#include <stdio.h>

#if defined(VMS)
typedef struct desc
{
    unsigned short len;
    unsigned char type;
    unsigned char class;
    char *ptr;
} Desc;

int err2str$stv=0;
static char undef_msg[256];
static Desc gmsgdesc = {sizeof(undef_msg),0,0,undef_msg};

#else
static char undef_msg[64];
#endif

char *err2str( int num )        /* modeled after VMS's strerror() */
{
#ifndef __TURBOC__
    switch (num)
    {
    case EPERM: return "not owner";
    case ENOENT: return "no such file or directory";
    case EIO: return "i/o error";
    case ENXIO: return "no such device or address";
    case EBADF: return "bad file number";
    case EACCES: return "permission denied";
#ifndef WIN32
    case ENOTBLK: return "block device required";
#endif
    case EBUSY: return "mount device busy";
    case EEXIST: return "file exists";
    case EISDIR: return "is a directory";
    case EXDEV: return "cross-device link";
    case ENODEV: return "no such device";
    case ENOTDIR: return "not a directory";
    case ENFILE: return "file table overflow";
    case EMFILE: return "too many open files";
    case EFBIG: return "file too large";
    case ENOSPC: return "no space left on device";
    case ESPIPE: return "illegal seek";
    case EROFS: return "read-only file system";
    case EMLINK: return "too many links";
#ifdef VMS
    case EWOULDBLOCK: "I/O operation would block channel";
#endif
    case ESRCH: return "no such process";
    case EINTR: return "interrupted system call";
    case E2BIG: return "arg list too long";
    case ENOEXEC: return "exec format error";
    case ECHILD: return "no children";
    case EAGAIN: return "no more processes";
    case ENOMEM: return "not enough core";
    case EFAULT: return "bad address";
    case EINVAL: return "invalid argument";
    case ENOTTY: return "not a typewriter";
#ifndef WIN32
    case ETXTBSY: return "text file busy";
#endif
    case EPIPE: return "broken pipe";
    case EDOM: return "math argument";
    case ERANGE: return "result too large";
#ifdef VMS
    case EVMSERR: {
            int retlen;
#if 0
            struct
            {
                unsigned short count;
                unsigned short options;
                unsigned long code;
                unsigned long stv;
            } msgvec;
            msgvec.count = sizeof(msgvec)/sizeof(long)-1;
            msgvec.options = 0;
            msgvec.code = vaxc$errno;
            msgvec.stv = err2str$stv;
#endif
            if ((vaxc$errno&0x07FF0000) <= 0x10000)
            {
                sys$getmsg(vaxc$errno,&retlen,&gmsgdesc,1,(char *)0);
                undef_msg[retlen] = 0;
            }
            else
            {
                sprintf(undef_msg,"Untranslatable VMS error code of: 0x%08X",vaxc$errno);
            }
            return undef_msg;
        }
#endif
    default: {
#endif
            sprintf(undef_msg,"Undefined error code: 0x%0X",num);
            return undef_msg;
#ifndef __TURBOC__
        }
    }
#endif
}
