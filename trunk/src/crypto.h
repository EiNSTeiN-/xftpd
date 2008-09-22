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

#ifndef __CRYPTO_H
#define __CRYPTO_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_CRYPTO
#endif

#ifdef DEBUG_CRYPTO
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define CRYPTO_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define CRYPTO_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define CRYPTO_DBG(format, arg...)
#endif

#include <openssl/rsa.h>
#include <openssl/blowfish.h>

int crypto_init();
void crypto_free();


struct keypair {
	RSA *key;
	unsigned int priv_present;
} __attribute__((packed));

unsigned int crypto_rand(char *buffer, unsigned int size);
char *crypto_keyhash(struct keypair *k);
unsigned int crypto_initialize_keypair(struct keypair *k, unsigned int priv_present);
unsigned int crypto_generate_keypair(struct keypair *k, unsigned int bits);
char *crypto_export_keypair(struct keypair *k, unsigned int export_priv, unsigned int *export_length);
unsigned int crypto_import_keypair(struct keypair *k, unsigned int import_priv, char *data, unsigned int data_length);
char *crypto_sha1_sign_data(struct keypair *k, char *data, unsigned int data_length, unsigned int *sign_length);
unsigned int crypto_sha1_verify_sign(struct keypair *k, char *data, unsigned int data_length, char *sign, unsigned int sign_length);
unsigned int crypto_destroy_keypair(struct keypair *k);


unsigned int crypto_max_public_encryption_length(struct keypair *k);
char *crypto_public_encrypt(struct keypair *k, void *data, unsigned int data_length, unsigned int *encrypted_size);
char *crypto_private_decrypt(struct keypair *k, void *data, unsigned int data_length, unsigned int *decrypted_size);
unsigned int crypto_set_cipher_key(BF_KEY *schedule, char *buffer, unsigned int length);
char *crypto_cipher_encrypt(BF_KEY *schedule, void *data, unsigned int data_length, unsigned int *retsize, int enc);

#endif /* __CRYPTO_H */
