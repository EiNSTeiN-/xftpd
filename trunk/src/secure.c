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
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "constants.h"
#include "secure.h"
#include "collection.h"
#include "signal.h"
#include "socket.h"

/* a reference to all signals is stored here so we can poll them */
struct collection *secure_signals = NULL;

int secure_init() {
	
	SSL_load_error_strings();
	SSL_library_init();
	
	secure_signals = collection_new(C_CASCADE);
	
	return 1;
}

void secure_free() {
	
	collection_destroy(secure_signals);
	secure_signals = NULL;
	
	return;
}

int secure_poll() {
	
	signal_poll(secure_signals);

	return 1;
}

int secure_setup(struct secure_ctx *secure, int type) {
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	secure->type = type;
	secure->use_secure = 0;
	
	secure->ssl_ctx = NULL;
	secure->ssl = NULL;
	
	secure->fd = -1;
	secure->group = collection_new(C_CASCADE);
	secure->signals = collection_new(C_CASCADE);
	
//	secure->negotiating = 0;
	secure->status = SECURE_STATUS_NONE;
	secure->operation = SECURE_OPERATION_NONE;
	secure->lasterror = 0;
	
	secure->read_signal = signal_get(secure->signals, "secure-read", 1);
	signal_ref(secure->read_signal);
	collection_add(secure_signals, secure->read_signal);
	
	secure->write_signal = signal_get(secure->signals, "secure-write", 1);
	signal_ref(secure->write_signal);
	collection_add(secure_signals, secure->write_signal);
	
	secure->resume_recv_signal = signal_get(secure->signals, "secure-resume-recv", 1);
	signal_ref(secure->resume_recv_signal);
	collection_add(secure_signals, secure->resume_recv_signal);
	
	secure->resume_send_signal = signal_get(secure->signals, "secure-resume-send", 1);
	signal_ref(secure->resume_send_signal);
	collection_add(secure_signals, secure->resume_send_signal);
	
	secure->connect_signal = signal_get(secure->signals, "secure-connect", 1);
	signal_ref(secure->connect_signal);
	collection_add(secure_signals, secure->connect_signal);
	
	secure->error_signal = signal_get(secure->signals, "secure-error", 1);
	signal_ref(secure->error_signal);
	collection_add(secure_signals, secure->error_signal);
	
	if(secure->type == SECURE_TYPE_SERVER) {
		//SECURE_DBG("Using server methods");
		secure->ssl_ctx = SSL_CTX_new(SSLv23_server_method());
	}
	else if(secure->type == SECURE_TYPE_CLIENT) {
		//SECURE_DBG("Using client methods");
		secure->ssl_ctx = SSL_CTX_new(SSLv23_client_method());
	}
	else if(secure->type == SECURE_TYPE_AUTO) {
		//SECURE_DBG("Using auto methods");
		secure->ssl_ctx = SSL_CTX_new(SSLv23_method());
	}
	
	if(!secure->ssl_ctx) {
		SECURE_DBG("memory error with SSL_CTX_new");
		return 0;
	}
	
	/* set some default options */
	SSL_CTX_set_verify(secure->ssl_ctx, SSL_VERIFY_NONE, NULL);
	SSL_CTX_set_options(secure->ssl_ctx, SSL_OP_ALL);
	
	SSL_CTX_set_cipher_list(secure->ssl_ctx, "ALL");
	
	return 1;
}

int secure_print_error(struct secure_ctx *secure, int err) {
	FILE *f;

	switch(err) {
	case SSL_ERROR_NONE:
		SECURE_DBG("SSL error none");
		break;
	case SSL_ERROR_ZERO_RETURN:
		SECURE_DBG("SSL ERROR zero return");
		break;
	case SSL_ERROR_WANT_READ:
		SECURE_DBG("SSL ERROR want read");
		break;
	case SSL_ERROR_WANT_WRITE:
		SECURE_DBG("SSL ERROR want write");
		break;
	case SSL_ERROR_WANT_CONNECT:
		SECURE_DBG("SSL ERROR want connect");
		break;
	case SSL_ERROR_WANT_ACCEPT:
		SECURE_DBG("SSL ERROR want accept");
		break;
	case SSL_ERROR_WANT_X509_LOOKUP:
		SECURE_DBG("SSL ERROR x509 lookup");
		break;
	case SSL_ERROR_SYSCALL:
		SECURE_DBG("SSL ERROR syscall");
		break;
	case SSL_ERROR_SSL:
		SECURE_DBG("SSL ERROR ssl");
		break;
	default:
		SECURE_DBG("SSL ERROR (unknown)");
		break;
	}
	
	f = fopen("debug.log", "a");
	ERR_print_errors_fp(f);
	fclose(f);
	
	return 1;
}

int secure_do_handshake(struct secure_ctx *secure) {
	int i;

	i = SSL_do_handshake(secure->ssl);
	secure->operation = SECURE_OPERATION_HANDSHAKE;
	
	if(i == 1) {
		//SECURE_DBG("SSL Negotitation: Connected !");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_CONNECTED;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->connect_signal, (void *)secure->fd);
	}
	else if(i == 0) {
		SECURE_DBG("The TLS/SSL handshake was not successful but was shut down controlled and by the specifications of the TLS/SSL protocol.");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
		return 1;
	}
	else if(i < 0) {
		int err = SSL_get_error(secure->ssl, i);
		switch(err) {
			case SSL_ERROR_WANT_READ:
			{
				//SECURE_DBG("SSL Negotitation: want read");
				secure->status = SECURE_STATUS_WANT_READ;
				break;
			}
			case SSL_ERROR_WANT_WRITE:
			{
				//SECURE_DBG("SSL Negotitation: want write");
				secure->status = SECURE_STATUS_WANT_WRITE;
				break;
			}
			default:
			{
				SECURE_DBG("The TLS/SSL handshake was not successful, because a fatal "
					"error occurred either at the protocol level or a connection failure occurred.");
				secure_print_error(secure, err);
				//secure->negotiating = 0;
				secure->status = SECURE_STATUS_ERROR;
				secure->lasterror = i;
				secure->operation = SECURE_OPERATION_NONE;
				signal_raise(secure->error_signal, (void *)secure->fd);
				break;
			}
		}
	} else {
		SECURE_DBG("Unknown return value of SSL_connect().");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
		return 0;
	}
	
	return 1;
}

static int secure_do_shutdown_1(struct secure_ctx *secure) {
	int i;
	
	i = SSL_shutdown(secure->ssl);
	secure->operation = SECURE_OPERATION_SHUTDOWN_1;
	
	if(i > 0) {
		SECURE_DBG("SSL Shutdown succeeded on first try");
		secure->operation = SECURE_OPERATION_NONE;
		secure->status = SECURE_STATUS_CONNECTED;
		
		if(secure->ssl) {
			/* simply free it and continue unprotected */
			SSL_free(secure->ssl);
			secure->ssl = NULL;
		}
		
		secure->use_secure = 0;
		return 1;
	}
	else if(i == 0) {
		SECURE_DBG("The shutdown is not yet finished.");
		
		/* first step is finished. wait for second shutdown step. */
		secure->operation = SECURE_OPERATION_SHUTDOWN_2;
		secure->status = SECURE_STATUS_WANT_WRITE;
	}
	else if(i < 0) {
		switch(SSL_get_error(secure->ssl, i)) {
			case SSL_ERROR_WANT_READ:
			{
				SECURE_DBG("Error: shutdown(1) want read");
				secure->status = SECURE_STATUS_WANT_READ;
				return 1;
			}
			case SSL_ERROR_WANT_WRITE:
			{
				SECURE_DBG("Error: shutdown(1) want write");
				secure->status = SECURE_STATUS_WANT_READ;
				return 1;
			}
			default:
			{
				SECURE_DBG("The shutdown was not successful because a fatal error occurred "
					"either at the protocol level or a connection failure occurred.");
				secure->status = SECURE_STATUS_ERROR;
				secure->lasterror = i;
				secure->operation = SECURE_OPERATION_NONE;
				signal_raise(secure->error_signal, (void *)secure->fd);
				break;
			}
		}
	}
	
	return 1;
}

static int secure_do_shutdown_2(struct secure_ctx *secure) {
	int i;
	
	i = SSL_shutdown(secure->ssl);
	secure->operation = SECURE_OPERATION_SHUTDOWN_2;
	
	if(i > 0) {
		SECURE_DBG("SSL Shutdown succeeded on second try");
		secure->operation = SECURE_OPERATION_NONE;
		secure->status = SECURE_STATUS_CONNECTED;
		
		if(secure->ssl) {
			/* simply free it and continue unprotected */
			SSL_free(secure->ssl);
			secure->ssl = NULL;
		}
		
		secure->use_secure = 0;
		return 1;
	}
	else if(i == 0) {
		SECURE_DBG("Zero on second call, wierd.");
		
		/* zero on second call is no good. */
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
	}
	else if(i < 0) {
		switch(SSL_get_error(secure->ssl, i)) {
			case SSL_ERROR_WANT_READ:
			{
				SECURE_DBG("Error: shutdown(1) want read");
				secure->status = SECURE_STATUS_WANT_READ;
				return 1;
			}
			case SSL_ERROR_WANT_WRITE:
			{
				SECURE_DBG("Error: shutdown(2) want write");
				secure->status = SECURE_STATUS_WANT_READ;
				return 1;
			}
			default:
			{
				SECURE_DBG("The shutdown was not successful because a fatal error occurred "
					"either at the protocol level or a connection failure occurred.");
				secure->status = SECURE_STATUS_ERROR;
				secure->lasterror = i;
				secure->operation = SECURE_OPERATION_NONE;
				signal_raise(secure->error_signal, (void *)secure->fd);
				break;
			}
		}
	}
	
	return 1;
}

int secure_socket_read(int s, struct secure_ctx *secure) {
	
	if(!secure->use_secure ||
		((secure->status != SECURE_STATUS_ERROR) &&
		 (secure->status != SECURE_STATUS_NONE) &&
		 (secure->operation == SECURE_OPERATION_NONE))) {
		
		//SECURE_DBG("Propagating read signal");
		
		/* not using secure connection: propagate the signal */
		signal_raise(secure->read_signal, (void *)s);
		return 1;
	}
	
	/* there's an operation started, so check if we can give it what it needs */
	else if(secure->status == SECURE_STATUS_WANT_READ) {
		if(secure->operation == SECURE_OPERATION_HANDSHAKE) {
			//SECURE_DBG("Resuming handshake because of want read");
			secure_do_handshake(secure);
		}
		else if(secure->operation == SECURE_OPERATION_RECV) {
			//SECURE_DBG("Resuming recv because of want read");
			signal_raise(secure->resume_recv_signal, (void *)s);
		}
		else if(secure->operation == SECURE_OPERATION_SEND) {
			//SECURE_DBG("Resuming send because of want read");
			signal_raise(secure->resume_send_signal, (void *)s);
		}
		else if(secure->operation == SECURE_OPERATION_SHUTDOWN_1) {
			SECURE_DBG("Resuming shutdown(1) because of want read");
			secure_do_shutdown_1(secure);
		}
		else if(secure->operation == SECURE_OPERATION_SHUTDOWN_2) {
			SECURE_DBG("Resuming shutdown(2) because of want read");
			secure_do_shutdown_2(secure);
		}
		else {
			SECURE_DBG("Unknown operation in read ?");
		}
	}
	
	/*else {
		SECURE_DBG("Nothing to do in secure_socket_read !");
	}*/
	
	return 1;
}

int secure_socket_write(int s, struct secure_ctx *secure) {
	
	if(!secure->use_secure ||
		((secure->status != SECURE_STATUS_ERROR) &&
		 (secure->status != SECURE_STATUS_NONE) &&
		 (secure->operation == SECURE_OPERATION_NONE))) {
		
		//SECURE_DBG("Propagating write signal");
		
		/* not using secure connection: propagate the signal */
		signal_raise(secure->write_signal, (void *)s);
		return 1;
	}
	
	/* there's an operation started, so check if we can give it what it needs */
	else if(secure->status == SECURE_STATUS_WANT_WRITE) {
		if(secure->operation == SECURE_OPERATION_HANDSHAKE) {
			//SECURE_DBG("Resuming handshake because of want write");
			secure_do_handshake(secure);
		}
		else if(secure->operation == SECURE_OPERATION_RECV) {
			//SECURE_DBG("Resuming recv because of want write");
			signal_raise(secure->resume_recv_signal, (void *)s);
		}
		else if(secure->operation == SECURE_OPERATION_SEND) {
			//SECURE_DBG("Resuming send because of want write");
			signal_raise(secure->resume_send_signal, (void *)s);
		}
		else if(secure->operation == SECURE_OPERATION_SHUTDOWN_1) {
			SECURE_DBG("Resuming shutdown(1) because of want write");
			secure_do_shutdown_1(secure);
		}
		else if(secure->operation == SECURE_OPERATION_SHUTDOWN_2) {
			SECURE_DBG("Resuming shutdown(2) because of want write");
			secure_do_shutdown_2(secure);
		}
		else {
			SECURE_DBG("Unknown operation in write ?");
		}
	}
	
	/*else {
		SECURE_DBG("Nothing to do in secure_socket_write !");
	}*/
	
	return 1;
}

int secure_connect(struct secure_ctx *secure, int s) {
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	secure->fd = s;
	
	/* these two events must not be hooked by the caller */
	socket_monitor_signal_add(secure->fd, secure->group, "socket-read", (signal_f)secure_socket_read, secure);
	socket_monitor_signal_add(secure->fd, secure->group, "socket-write", (signal_f)secure_socket_write, secure);
	
	return 1;
}

int secure_type(struct secure_ctx *secure, int secure_type) {
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	secure->type = secure_type;
	
	return 1;
}
	
int secure_negotiate(struct secure_ctx *secure) {
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	if((secure->type != SECURE_TYPE_SERVER) &&
			(secure->type != SECURE_TYPE_CLIENT)) {
		SECURE_DBG("Cannot negotiate, secure type is not set !");
		return 0;
	}
	
	secure->use_secure = 1;
	
	/* create the ssl context */
	secure->ssl = SSL_new(secure->ssl_ctx);
	if(!secure->ssl) {
		SECURE_DBG("memory error with SSL_new");
		return 0;
	}
	
	SSL_set_fd(secure->ssl, secure->fd);
	if(secure->type == SECURE_TYPE_SERVER) {
		SSL_set_accept_state(secure->ssl);
	}
	else if(secure->type == SECURE_TYPE_CLIENT) {
		SSL_set_connect_state(secure->ssl);
	}
	
	return secure_do_handshake(secure);
}

int secure_drop(struct secure_ctx *secure) {
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	if(!secure->ssl) {
		SECURE_DBG("No SSL to drop !");
		return 0;
	}
	
	return secure_do_shutdown_1(secure);
}

int secure_recv(struct secure_ctx *secure, char *buf, int len, int *tryagain) {
	int i;
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	*tryagain = 0;
	
	/* not using secure connection: behaves like normal recv() */
	if(!secure->use_secure) {
		
		return recv(secure->fd, buf, len, 0);
	}
	
	if(((secure->status != SECURE_STATUS_WANT_READ) &&
		(secure->status != SECURE_STATUS_WANT_WRITE) &&
		(secure->status != SECURE_STATUS_CONNECTED)) ||
	   ((secure->operation != SECURE_OPERATION_RECV) &&
		(secure->operation != SECURE_OPERATION_NONE))) {
		
		SECURE_DBG("secure_recv was called in an invalid state");
		return -1;
	}
	
	i = SSL_read(secure->ssl, buf, len);
	secure->operation = SECURE_OPERATION_RECV;
	
	if(i > 0) {
		//SECURE_DBG("SSL Recv: OK on first try !");
		//secure->negotiating = 0;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		return i;
	}
	else if(i == 0) {
		SECURE_DBG("The TLS/SSL handshake was not successful but was shut down controlled and by the specifications of the TLS/SSL protocol.");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
	}
	else if(i < 0) {
		switch(SSL_get_error(secure->ssl, i)) {
			case SSL_ERROR_WANT_READ:
			{
				if(SSL_state(secure->ssl) == SSL_ST_RENEGOTIATE) {
					SECURE_DBG("SSL Recv: want read, renegotiate");
					secure->status = SECURE_STATUS_WANT_READ;
				} else {
					//SECURE_DBG("SSL Recv: want read, no more data buffered");
					secure->operation = SECURE_OPERATION_NONE;
				}
				*tryagain = 1;
				return -1;
			}
			case SSL_ERROR_WANT_WRITE:
			{
				if(SSL_state(secure->ssl) == SSL_ST_RENEGOTIATE) {
					SECURE_DBG("SSL Recv: want write, renegotiate");
					secure->status = SECURE_STATUS_WANT_READ;
				} else {
					//SECURE_DBG("SSL Recv: want write, no more data buffered");
					secure->operation = SECURE_OPERATION_NONE;
				}
				*tryagain = 1;
				return -1;
			}
			default:
			{
				SECURE_DBG("The TLS/SSL handshake was not successful, because a fatal "
					"error occurred either at the protocol level or a connection failure occurred.");
				//secure->negotiating = 0;
				secure->status = SECURE_STATUS_ERROR;
				secure->lasterror = i;
				secure->operation = SECURE_OPERATION_NONE;
				signal_raise(secure->error_signal, (void *)secure->fd);
				break;
			}
		}
	} /* is this superfluous ?
	else {
		SECURE_DBG("Unknown return value of SSL_recv().");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
	}*/
	
	return -1;
}

int secure_send(struct secure_ctx *secure, const char *buf, int len, int *tryagain) {
	int i;
	
	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}
	
	*tryagain = 0;
	
	/* not using secure connection: behaves like normal send() */
	if(!secure->use_secure) {
		
		return send(secure->fd, buf, len, 0);
	}
	
	if(((secure->status != SECURE_STATUS_WANT_READ) &&
		(secure->status != SECURE_STATUS_WANT_WRITE) &&
		(secure->status != SECURE_STATUS_CONNECTED)) ||
	   ((secure->operation != SECURE_OPERATION_SEND) &&
		(secure->operation != SECURE_OPERATION_NONE))) {
		
		SECURE_DBG("secure_send was called in an invalid state");
		return -1;
	}
	
	i = SSL_write(secure->ssl, buf, len);
	secure->operation = SECURE_OPERATION_SEND;
	
	if(i > 0) {
		//SECURE_DBG("SSL Send: OK on first try !");
		//secure->negotiating = 0;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		return i;
	}
	else if(i == 0) {
		SECURE_DBG("The TLS/SSL handshake was not successful but was shut down controlled and by the specifications of the TLS/SSL protocol.");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
	}
	else if(i < 0) {
		switch(SSL_get_error(secure->ssl, i)) {
			case SSL_ERROR_WANT_READ:
			{
				if(SSL_state(secure->ssl) == SSL_ST_RENEGOTIATE) {
					SECURE_DBG("SSL Send: want read, renegotiate");
					secure->status = SECURE_STATUS_WANT_READ;
				} else {
					//SECURE_DBG("SSL Send: want read, no more data buffered");
					secure->operation = SECURE_OPERATION_NONE;
				}
				*tryagain = 1;
				return -1;
			}
			case SSL_ERROR_WANT_WRITE:
			{
				if(SSL_state(secure->ssl) == SSL_ST_RENEGOTIATE) {
					SECURE_DBG("SSL Send: want write, renegotiate");
					secure->status = SECURE_STATUS_WANT_READ;
				} else {
					//SECURE_DBG("SSL Send: want write, no more data buffered");
					secure->operation = SECURE_OPERATION_NONE;
				}
				*tryagain = 1;
				return -1;
			}
			default:
			{
				SECURE_DBG("The TLS/SSL handshake was not successful, because a fatal "
					"error occurred either at the protocol level or a connection failure occurred.");
				//secure->negotiating = 0;
				secure->status = SECURE_STATUS_ERROR;
				secure->lasterror = i;
				secure->operation = SECURE_OPERATION_NONE;
				signal_raise(secure->error_signal, (void *)secure->fd);
				break;
			}
		}
	} else {
		SECURE_DBG("Unknown return value of SSL_write().");
		//secure->negotiating = 0;
		secure->status = SECURE_STATUS_ERROR;
		secure->lasterror = i;
		secure->operation = SECURE_OPERATION_NONE;
		signal_raise(secure->error_signal, (void *)secure->fd);
	}
	
	return -1;
}

struct signal_callback *secure_signal_add(struct secure_ctx *secure, struct collection *group, char *name, int (*callback)(void *obj, void *param), void *param) {
	struct signal_callback *s;

	if(!secure) {
		SECURE_DBG("params error");
		return 0;
	}

	s = signal_add(secure->signals, group, name, callback, param);
	if(!s) {
		SECURE_DBG("signal_add failed!");
		return NULL;
	}
	signal_filter(s, (void *)secure->fd);

	return s;
}

int secure_close(struct secure_ctx *secure) {
	
	if(secure->ssl) {
		SSL_free(secure->ssl);
		secure->ssl = NULL;
	}
	
	if(secure->group) {
		signal_clear(secure->group);
	}
	
	secure->use_secure = 0;
	
	return 1;
}

int secure_destroy(struct secure_ctx *secure) {
	
	if(secure->ssl_ctx) {
		SSL_CTX_free(secure->ssl_ctx);
		secure->ssl_ctx = NULL;
	}
	
	if(secure->ssl) {
		SSL_free(secure->ssl);
		secure->ssl = NULL;
	}
	
	if(secure->group) {
		collection_destroy(secure->group);
		secure->group = NULL;
	}
	
	if(secure->read_signal) {
		signal_unref(secure->read_signal);
		secure->read_signal = NULL;
	}
	
	if(secure->write_signal) {
		signal_unref(secure->write_signal);
		secure->write_signal = NULL;
	}
	
	if(secure->resume_recv_signal) {
		signal_unref(secure->resume_recv_signal);
		secure->resume_recv_signal = NULL;
	}
	
	if(secure->resume_send_signal) {
		signal_unref(secure->resume_send_signal);
		secure->resume_send_signal = NULL;
	}
	
	if(secure->connect_signal) {
		signal_unref(secure->connect_signal);
		secure->connect_signal = NULL;
	}
	
	if(secure->error_signal) {
		signal_unref(secure->error_signal);
		secure->error_signal = NULL;
	}
	
	if(secure->signals) {
		collection_destroy(secure->signals);
		secure->signals = NULL;
	}
	
	return 1;
}




