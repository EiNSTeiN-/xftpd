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

#ifndef __BLOWFISH_H
#define __BLOWFISH_H

#include <windows.h>
#include <stdio.h>

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_BLOWFISH
#endif

#ifdef DEBUG_BLOWFISH
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define BLOWFISH_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  include "logging.h"
#  define BLOWFISH_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define BLOWFISH_DBG(format, arg...)
#endif

#define bf_N			16
#define MAXKEYBYTES		56

typedef unsigned int		u_32bit_t;
typedef unsigned short int	u_16bit_t;
typedef unsigned char		u_8bit_t;

union aword {
  u_32bit_t word;
  u_8bit_t byte[4];
  struct {
#ifdef WORDS_BIGENDIAN
    unsigned int byte0:8;
    unsigned int byte1:8;
    unsigned int byte2:8;
    unsigned int byte3:8;
#else				/* !WORDS_BIGENDIAN */
    unsigned int byte3:8;
    unsigned int byte2:8;
    unsigned int byte1:8;
    unsigned int byte0:8;
#endif				/* !WORDS_BIGENDIAN */
  } w;
};

struct bf_state {
	u_32bit_t bf_P[bf_N+2];
	u_32bit_t bf_S[4][256];
} __attribute__((packed));

char *blowfish_decrypt(struct bf_state *state, char *str);
char *blowfish_encrypt(struct bf_state *state, char *str);
int blowfish_init(struct bf_state *state, u_8bit_t * key, int keybytes);

#endif /* __BLOWFISH_H */
