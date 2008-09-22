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

#ifndef __SCRIPTS_H
#define __SCRIPTS_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_SCRIPTS)
# define SCRIPTS_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define SCRIPTS_DBG(format, arg...)
#endif

#include "luainit.h"

extern struct collection *scripts;

typedef struct script_ctx script_ctx;
struct script_ctx {
  struct obj o;
  struct collectible c;
  
  lua_State *L;
  char *filename;
  
  /* those collections will hold a copy of the pointers to 
    all objects created specifically for this script. when the
    script is unloaded and the collections are destroyed, all
    objects in those collections will be destroyed as well,
    effectively cleaning up everything that has to do with this
    script (callbacks, handlers, timers, etc). */
  struct collection *events; /* struct event_callback */
  struct collection *irchandlers; /* struct irc_handler */
  struct collection *mirrors; /* struct mirror_ctx */
  struct collection *sitehandlers; /* struct site_handler */
  struct collection *timers; /* struct timer_ctx */
} __attribute__((packed));

int scripts_init();
int scripts_reload();
void scripts_free();

/* get a script_ctx structure that correspond to the specified lua State. */
struct script_ctx *script_resolve(lua_State *L);

int scripts_load_directory(const char *dir);
int scripts_load_file(const char *filename);

#endif /* __SCRIPTS_H */
