/*
 *  csync2 - cluster synchronisation tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004  Clifford Wolf <clifford@clifford.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csync2.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <errno.h>


int conn_fd_in  = -1;
int conn_fd_out = -1;
int conn_clisok = 0;
int conn_usessl = 0;

SSL_METHOD *conn_ssl_meth;
SSL_CTX *conn_ssl_ctx;
SSL *conn_ssl;

int conn_open(const char *peername)
{
        struct sockaddr_in sin;
        struct hostent *hp;

        hp = gethostbyname(peername);
        if ( ! hp ) {
                csync_debug(1, "Can't resolve peername.\n");
                return -1;
        }

        conn_fd_in = socket(hp->h_addrtype, SOCK_STREAM, 0);
        if (conn_fd_in < 0) {
                csync_debug(1, "Can't create socket.\n");
                return -1;
        }

        sin.sin_family = hp->h_addrtype;
        bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);
        sin.sin_port = htons(CSYNC_PORT);

        if (connect(conn_fd_in, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
                csync_debug(1, "Can't connect to remote host.\n");
		close(conn_fd_in); conn_fd_in = -1;
                return -1;
        }

	conn_fd_out = conn_fd_in;
	conn_clisok = 1;
	conn_usessl = 0;

	return 0;
}

int conn_set(int infd, int outfd)
{
	conn_fd_in  = infd;
	conn_fd_out = outfd;
	conn_clisok = 1;
	conn_usessl = 0;

	return 0;
}

char *ssl_keyfile = ETCDIR "/csync2_ssl_key.pem";
char *ssl_certfile = ETCDIR "/csync2_ssl_cert.pem";

int conn_activate_ssl(int role)
{
	static sslinit = 0;

	if (conn_usessl)
		return 0;

	if (!sslinit) {
		SSL_load_error_strings();
		SSL_library_init();
		sslinit=1;
	}

	conn_ssl_meth = SSLv23_method();
	conn_ssl_ctx = SSL_CTX_new(conn_ssl_meth);

	if (role) {
		if (SSL_CTX_use_PrivateKey_file(conn_ssl_ctx, ssl_keyfile, SSL_FILETYPE_PEM) <= 0)
			csync_fatal("SSL: failed to use key file %s.\n", ssl_keyfile);

		if (SSL_CTX_use_certificate_file(conn_ssl_ctx, ssl_certfile, SSL_FILETYPE_PEM) <= 0)
			csync_fatal("SSL: failed to use certificate file %s.\n", ssl_certfile);
	}

	if (! (conn_ssl = SSL_new(conn_ssl_ctx)) )
		csync_fatal("Creating a new SSL handle failed.\n");

	SSL_set_rfd(conn_ssl, conn_fd_in);
	SSL_set_wfd(conn_ssl, conn_fd_out);

	if ( (role ? SSL_accept : SSL_connect)(conn_ssl) < 1 )
		csync_fatal("Establishing SSL connection failed.\n");

	conn_usessl = 1;

	return 0;
}

int conn_close()
{
	if ( !conn_clisok ) return -1;

	if ( conn_usessl ) SSL_free(conn_ssl);

	if ( conn_fd_in != conn_fd_out) close(conn_fd_in);
	close(conn_fd_out);

	conn_fd_in  = -1;
	conn_fd_out = -1;
	conn_clisok =  0;

	return 0;
}

static inline READ(void *buf, size_t count)
{
	if (conn_usessl)
		return SSL_read(conn_ssl, buf, count);
	else
		return read(conn_fd_in, buf, count);
}

static inline WRITE(const void *buf, size_t count)
{
	if (conn_usessl)
		return SSL_write(conn_ssl, buf, count);
	else
		return write(conn_fd_out, buf, count);
}

int conn_raw_read(void *buf, size_t count)
{
	static char buffer[512];
	static int buf_start=0, buf_end=0;

	if ( buf_start == buf_end ) {
		if (count > 128)
			return READ(buf, count);
		else {
			buf_start = 0;
			buf_end = READ(buffer, 512);
			if (buf_end < 0) { buf_end=0; return -1; }
		}
	}
	
	if ( buf_start < buf_end ) {
		size_t real_count = buf_end - buf_start;
		if ( real_count > count ) real_count = count;

		memcpy(buf, buffer+buf_start, real_count);
		buf_start += real_count;

		return real_count;
	}

	return 0;
}

void conn_debug(const char *name, const unsigned char*buf, size_t count)
{
	int i;

	if ( csync_debug_level < 3 ) return;

	fprintf(csync_debug_out, "%s> ", name);
	for (i=0; i<count; i++) {
		switch (buf[i]) {
			case '\n':
				fprintf(csync_debug_out, "\\n");
				break;
			case '\r':
				fprintf(csync_debug_out, "\\r");
				break;
			default:
				if (buf[i] < 32 || buf[i] >= 127)
					fprintf(csync_debug_out, "\\%03o", buf[i]);
				else
					fprintf(csync_debug_out, "%c", buf[i]);
				break;
		}
	}
	fprintf(csync_debug_out, "\n");
}

int conn_read(void *buf, size_t count)
{
	int pos, rc;

	for (pos=0; pos < count; pos+=rc) {
		rc = conn_raw_read(buf+pos, count-pos);
		if (rc <= 0) return pos;
	}

	conn_debug("Peer", buf, pos);
	return pos;
}

int conn_write(const void *buf, size_t count)
{
	conn_debug("Local", buf, count);
	return WRITE(buf, count);
}

void conn_printf(const char *fmt, ...)
{
	char dummy, *buffer;
	va_list ap;
	int size;

	va_start(ap, fmt);
	size = vsnprintf(&dummy, 1, fmt, ap);
	buffer = alloca(size+1);
	va_end(ap);

	va_start(ap, fmt);
	vsnprintf(buffer, size+1, fmt, ap);
	va_end(ap);

	conn_write(buffer, size);
}

int conn_gets(char *s, int size)
{
	int i=0;

	while (i<size-1) {
		int rc = conn_raw_read(s+i, 1);
		if (rc != 1) break;
		if (s[i++] == '\n') break;
	}
	s[i] = 0;

	conn_debug("Peer", s, i);
	return i;
}

