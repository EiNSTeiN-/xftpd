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

#include <windows.h>

#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/blowfish.h>

#include "crypto.h"
#include "base64.h"

int crypto_init() {
	//rand_init();

	return 1;
}

void crypto_free() {
	//rand_free();
	
	return;
}

unsigned int crypto_rand(char *buffer, unsigned int size) {
	return RAND_bytes(buffer, size);
}

char *crypto_keyhash(struct keypair *k) {
	unsigned char digest[SHA_DIGEST_LENGTH];
	unsigned int len;
	char *buffer;

	buffer = crypto_export_keypair(k, 0, &len);
	if(buffer == NULL) {
		CRYPTO_DBG("Memory error");
		return NULL;
	}
	SHA1(buffer, len, digest);
	free(buffer);

	return b64_encode(digest, SHA_DIGEST_LENGTH);
}

unsigned int crypto_initialize_keypair(struct keypair *k, unsigned int priv_present) {

	k->priv_present = 0;

	k->key = RSA_new();
	if(!k->key) {
		CRYPTO_DBG("Memory error");
		return 0;
	}

	k->priv_present = priv_present;

	return 1;
}

unsigned int crypto_generate_keypair(struct keypair *k, unsigned int bits) {

	k->priv_present = 1;

	k->key = RSA_generate_key(bits, 35537, NULL, NULL);
	if(!k->key) {
		CRYPTO_DBG("Memory error");
		return 0;
	}

	return 1;
}

unsigned int crypto_BN_export(BIGNUM *num, char *buffer, unsigned int len) {
	memcpy(&buffer[0], &len, 4);
	return BN_bn2bin(num, &buffer[4]);
}

char *crypto_export_keypair(struct keypair *k, unsigned int export_priv, unsigned int *export_length) {
	//char *n, *e, *d, *p, *q, *dmp1, *dmq1, *iqmp;
	unsigned int n_len, e_len, d_len=0, p_len=0, q_len=0, dmp1_len=0, dmq1_len=0, iqmp_len=0;
	char *buffer;

	if(export_priv && !k->priv_present) {
		CRYPTO_DBG("Cannot export private because it is not present");
		return NULL;
	}

	n_len = BN_num_bytes(k->key->n);
	e_len = BN_num_bytes(k->key->e);
	(*export_length) = n_len + e_len + (2 * 4);

	if(export_priv) {
		d_len = BN_num_bytes(k->key->d);
		p_len = BN_num_bytes(k->key->p);
		q_len = BN_num_bytes(k->key->q);
		dmp1_len = BN_num_bytes(k->key->dmp1);
		dmq1_len = BN_num_bytes(k->key->dmq1);
		iqmp_len = BN_num_bytes(k->key->iqmp);
		(*export_length) += d_len + p_len + q_len + dmp1_len + dmq1_len + iqmp_len + (6 * 4);
	}

	buffer = malloc(*export_length);
	if(!buffer) {
		CRYPTO_DBG("Memory error");
		return NULL;
	}

	memset(buffer, 0, *export_length);

	if(!crypto_BN_export(k->key->n, &buffer[0], n_len)) {
		CRYPTO_DBG("Can't export key(n)");
		free(buffer);
		return NULL;
	}
	if(!crypto_BN_export(k->key->e, &buffer[n_len+4], e_len)) {
		CRYPTO_DBG("Can't export key(e)");
		free(buffer);
		return NULL;
	}

	if(export_priv) {
		if(!crypto_BN_export(k->key->d, &buffer[n_len+4+e_len+4], d_len)) {
			CRYPTO_DBG("Can't export key(d)");
			free(buffer);
			return NULL;
		}
		if(!crypto_BN_export(k->key->p, &buffer[n_len+4+e_len+4+d_len+4], p_len)) {
			CRYPTO_DBG("Can't export key(p)");
			free(buffer);
			return NULL;
		}
		if(!crypto_BN_export(k->key->q, &buffer[n_len+4+e_len+4+d_len+4+p_len+4], q_len)) {
			CRYPTO_DBG("Can't export key(q)");
			free(buffer);
			return NULL;
		}
		if(!crypto_BN_export(k->key->dmp1, &buffer[n_len+4+e_len+4+d_len+4+p_len+4+q_len+4], dmp1_len)) {
			CRYPTO_DBG("Can't export key(dmp1)");
			free(buffer);
			return NULL;
		}
		if(!crypto_BN_export(k->key->dmq1, &buffer[n_len+4+e_len+4+d_len+4+p_len+4+q_len+4+dmp1_len+4], dmq1_len)) {
			CRYPTO_DBG("Can't export key(dmq1)");
			free(buffer);
			return NULL;
		}
		if(!crypto_BN_export(k->key->iqmp, &buffer[n_len+4+e_len+4+d_len+4+p_len+4+q_len+4+dmp1_len+4+dmq1_len+4], iqmp_len)) {
			CRYPTO_DBG("Can't export key(iqmp)");
			free(buffer);
			return NULL;
		}
	}

	return buffer;
}

unsigned int crypto_import_keypair(struct keypair *k, unsigned int import_priv, char *data, unsigned int data_length) {
	unsigned int num_len;

	if(import_priv && !k->priv_present) {
		CRYPTO_DBG("Cannot export private because it is not present");
		return 0;
	}

	if(data_length < 4) {
		CRYPTO_DBG("Invalid data length");
		return 0;
	}
	num_len = *(unsigned int*)data;
	data_length -= 4;
	if(num_len > data_length) {
		CRYPTO_DBG("Invalid data length");
		return 0;
	}
	data += 4;
	k->key->n = BN_bin2bn(data, num_len, NULL);
	data += num_len;
	data_length -= num_len;

	if(data_length < 4) {
		CRYPTO_DBG("Invalid data length");
		return 0;
	}
	num_len = *(unsigned int*)data;
	data_length -= 4;
	if(num_len > data_length) {
		CRYPTO_DBG("Invalid data length");
		return 0;
	}
	data += 4;
	k->key->e = BN_bin2bn(data, num_len, NULL);
	data += num_len;
	data_length -= num_len;

	if(import_priv) {

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->d = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->p = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->q = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->dmp1 = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->dmq1 = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;

		if(data_length < 4) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		num_len = *(unsigned int*)data;
		data_length -= 4;
		if(num_len > data_length) {
			CRYPTO_DBG("Invalid data length");
			return 0;
		}
		data += 4;
		k->key->iqmp = BN_bin2bn(data, num_len, NULL);
		data += num_len;
		data_length -= num_len;
	}

	return 1;
}

char *crypto_sha1_sign_data(struct keypair *k, char *data, unsigned int data_length, unsigned int *sign_length) {
	char digest[SHA_DIGEST_LENGTH];
	char *sign;

	(*sign_length) = 0;

	if(!k->priv_present) {
		CRYPTO_DBG("Cannot sign because private part is not present");
		return NULL;
	}

	/* make a sha1 digest of the data */
	SHA1(data, data_length, digest);

	/* sign the digest */
	sign = malloc(RSA_size(k->key));
	if(!sign) {
		CRYPTO_DBG("Memory error");
		return NULL;
	}

	if(!RSA_sign(NID_sha1, digest, SHA_DIGEST_LENGTH, sign, sign_length, k->key)) {
		CRYPTO_DBG("RSA sign failed.");
		(*sign_length) = 0;
		free(sign);
		return NULL;
	}

	return sign;
}

unsigned int crypto_sha1_verify_sign(struct keypair *k, char *data, unsigned int data_length, char *sign, unsigned int sign_length) {
	char digest[SHA_DIGEST_LENGTH];
	
	/* make a sha1 digest of the data */
	SHA1(data, data_length, digest);
	
	return RSA_verify(NID_sha1, digest, SHA_DIGEST_LENGTH, sign, sign_length, k->key);
}

unsigned int crypto_destroy_keypair(struct keypair *k) {

	if(k->key) RSA_free(k->key);
	k->key = NULL;
	k->priv_present = 0;

	return 1;
}

/* simple wrapper */
unsigned int crypto_max_public_encryption_length(struct keypair *k) {

	/* RSA_PKCS1_OAEP_PADDING specific */
	return (RSA_size(k->key) - 42);
}

/* must be used to encrypt the cipher key */
char *crypto_public_encrypt(struct keypair *k, void *data, unsigned int data_length, unsigned int *encrypted_size) {
	char *encrypted;

	(*encrypted_size) = 0;

	encrypted = malloc(RSA_size(k->key));
	if(!encrypted) {
		CRYPTO_DBG("Memory error");
		return NULL;
	}

	(*encrypted_size) = RSA_public_encrypt(data_length, data, encrypted, k->key, RSA_PKCS1_OAEP_PADDING);
	if((*encrypted_size) == -1) {
		CRYPTO_DBG("Public encryption failed");
		memset(encrypted, 0, RSA_size(k->key));
		(*encrypted_size) = 0;
		free(encrypted);
		return NULL;
	}

	return encrypted;
}

/* must be used to decrypt data encrypted with crypto_public_encrypt() */
char *crypto_private_decrypt(struct keypair *k, void *data, unsigned int data_length, unsigned int *decrypted_size) {
	char *decrypted;

	(*decrypted_size) = 0;

	if(!k->priv_present) {
		CRYPTO_DBG("Cannot decrypt because private part is not present");
		return NULL;
	}

	decrypted = malloc(RSA_size(k->key));
	if(!decrypted) return NULL;

	(*decrypted_size) = RSA_private_decrypt(data_length, data, decrypted, k->key, RSA_PKCS1_OAEP_PADDING);
	if((*decrypted_size) == -1) {
		CRYPTO_DBG("RSA decryption failed.");
		memset(decrypted, 0, RSA_size(k->key));
		(*decrypted_size) = 0;
		free(decrypted);
		return NULL;
	}

	return decrypted;
}

unsigned int crypto_set_cipher_key(BF_KEY *schedule, char *buffer, unsigned int length) {

	BF_set_key(schedule, length, buffer);

	return 1;
}

char *crypto_cipher_encrypt(BF_KEY *schedule, void *data, unsigned int data_length, unsigned int *retsize, int enc) {
	unsigned char *buffer;
	unsigned int num = 0;

	/* 64 bits initialization vector: must be the same on client and backdoor side */
	unsigned long long int ivec = 0xdeadbeefbaadf00dLL;

	(*retsize) = 0;

	buffer = malloc(data_length);
	if(!buffer) {
		CRYPTO_DBG("Memory error");
		return NULL;
	}

	BF_cfb64_encrypt(data, buffer, data_length, schedule, (unsigned char *)&ivec, &num, enc ? BF_ENCRYPT : BF_DECRYPT);

	(*retsize) = data_length;

	return buffer;
}
