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

#ifndef __DEBUG_H
#define __DEBUG_H

/* Uncomment the following line to globally disable all debugging,
	even if DEBUG_TO_CONSOLE or DEBUG_TO_FILE are not commented */
//#define DEBUG_DISABLE

/* Uncomment to log to console, else the logging goes to file */
#define DEBUG_TO_CONSOLE
#define DEBUG_TO_FILE

/* select specific files to debug */
#define DEBUG_ADIO
#define DEBUG_ASYNCH
#define DEBUG_BASE64
#define DEBUG_BLOWFISH
#define DEBUG_COLLECTION
#define DEBUG_CONFIG
#define DEBUG_CRYPTO
#define DEBUG_EVENTS
#define DEBUG_EVENTS_CALLS //*
#define DEBUG_SLAVE
#define DEBUG_SLAVE_DIALOG //*
#define DEBUG_FTPD
#define DEBUG_FTPD_DIALOG //*
#define DEBUG_IO
#define DEBUG_IRCCORE
#define DEBUG_LUAINIT
#define DEBUG_MAIN
#define DEBUG_MIRROR
#define DEBUG_NUKE
#define DEBUG_OBJ
#define DEBUG_PACKET //*
#define DEBUG_PROXY
#define DEBUG_SCRIPTS
#define DEBUG_SECURE
#define DEBUG_SFV
#define DEBUG_SIGNAL
#define DEBUG_SITE
#define DEBUG_SLAVES
#define DEBUG_SLAVES_DIALOG
#define DEBUG_SLAVESELECTION
#define DEBUG_SOCKET
#define DEBUG_SOCKET_SIGNALS
#define DEBUG_STATS
#define DEBUG_TIMER
#define DEBUG_TREE
#define DEBUG_UPDATE
#define DEBUG_USERS
#define DEBUG_VFS
#define DEBUG_SECTIONS
#define DEBUG_SKINS

#if defined(DEBUG_TO_CONSOLE) && !defined(DEBUG_DISABLE)
# define _DEBUG_CONSOLE(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg);
#else
# define _DEBUG_CONSOLE(format, arg...) 
#endif

#if defined(DEBUG_TO_FILE) && !defined(DEBUG_DISABLE)
# include "logging.h"
# define _DEBUG_FILE(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg);
#else
# define _DEBUG_FILE(format, arg...) 
#endif

#endif /* __DEBUG_H */
