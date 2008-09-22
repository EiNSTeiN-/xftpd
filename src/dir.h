/*
 * Copyright (c) 2007, The xFTPd Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 *     * Neither the name of the xFTPd Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *     * Redistributions of this project or parts of this project in any form
 *       must retain the following aknowledgment:
 *       "This product includes software developed by the xFTPd Project.
 *        http://www.xftpd.com/ - http://www.xftpd.org/"
 *
 * THIS SOFTWARE IS PROVIDED BY THE xFTPd PROJECT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE xFTPd PROJECT BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DIR_H
#define __DIR_H

#ifdef WIN32
#include <io.h>

typedef intptr_t dir_info;
typedef struct _finddata_t dir_entry;

#define dir_name(e) ((e)->entry.name)
#define dir_attrib(e) ((e)->entry.attrib)

#define DIR_SUBDIR _A_SUBDIR

#else /* WIN32 */
# include <dirent.h>
# include "wild.h"

typedef DIR* dir_info;
typedef struct dirent* dir_entry;

#define dir_name(e) ((e)->entry->d_name)
#define dir_attrib(e) ((e)->entry->d_type)

#define DIR_SUBDIR DT_DIR

#endif /* WIN32 */

struct dir_ctx {
  dir_info info;
  dir_entry entry;
  char *filespec;
} __attribute__((packed));

struct dir_ctx *dir_open(const char *path, const char *filespec);
int dir_next(struct dir_ctx *dir);
int dir_close(struct dir_ctx *dir);

char *dir_fullpath(const char *path);

#endif /* __DIR_H */
