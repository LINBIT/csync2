/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005, 2006  Clifford Wolf <clifford@clifford.at>
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
#include <netinet/tcp.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>

#ifdef HAVE_LIBGNUTLS_OPENSSL
#  include <gnutls/gnutls.h>
#  include <gnutls/openssl.h>
#endif

int conn_fd_in  = -1;
int conn_fd_out = -1;
int conn_clisok = 0;

#ifdef HAVE_LIBGNUTLS_OPENSSL
int csync_conn_usessl = 0;

SSL_METHOD *conn_ssl_meth;
SSL_CTX *conn_ssl_ctx;
SSL *conn_ssl;
#endif


/* getaddrinfo stuff mostly copied from its manpage */
int conn_connect(const char *peername)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s, on = 1;

	/* Obtain address(es) matching host/port */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;	/* Any protocol */

	s = getaddrinfo(peername, csync_port, &hints, &result);
	if (s != 0) {
		csync_debug(1, "Cannot resolve peername, getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;	/* Success */

		close(sfd);
	}
	freeaddrinfo(result);	/* No longer needed */

	if (rp == NULL)	/* No address succeeded */
		return -1;

	return sfd;
}

int conn_open(const char *peername)
{
	int on = 1;

        conn_fd_in = conn_connect(peername);
        if (conn_fd_in < 0) {
                csync_debug(1, "Can't create socket.\n");
                return -1;
        }

	if (setsockopt(conn_fd_in, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on) ) < 0) {
                csync_debug(1, "Can't set TCP_NODELAY option on TCP socket.\n");
		close(conn_fd_in); conn_fd_in = -1;
                return -1;
	}

	conn_fd_out = conn_fd_in;
	conn_clisok = 1;
#ifdef HAVE_LIBGNUTLS_OPENSSL
	csync_conn_usessl = 0;
#endif
	return 0;
}

int conn_set(int infd, int outfd)
{
	int on = 1;

	conn_fd_in  = infd;
	conn_fd_out = outfd;
	conn_clisok = 1;
#ifdef HAVE_LIBGNUTLS_OPENSSL
	csync_conn_usessl = 0;
#endif

	// when running in server mode, this has been done already
	// in csync2.c with more restrictive error handling..
	// FIXME don't even try in "ssh" mode
	if ( setsockopt(conn_fd_out, IPPROTO_TCP, TCP_NODELAY, &on, (socklen_t) sizeof(on)) < 0 )
                csync_debug(1, "Can't set TCP_NODELAY option on TCP socket.\n");

	return 0;
}


#ifdef HAVE_LIBGNUTLS_OPENSSL

char *ssl_keyfile = ETCDIR "/csync2_ssl_key.pem";
char *ssl_certfile = ETCDIR "/csync2_ssl_cert.pem";

int conn_activate_ssl(int server_role)
{
	static int sslinit = 0;

	if (csync_conn_usessl)
		return 0;

	if (!sslinit) {
		SSL_load_error_strings();
		SSL_library_init();
		sslinit=1;
	}

	conn_ssl_meth = (server_role ? SSLv23_server_method : SSLv23_client_method)();
	conn_ssl_ctx = SSL_CTX_new(conn_ssl_meth);

	if (SSL_CTX_use_PrivateKey_file(conn_ssl_ctx, ssl_keyfile, SSL_FILETYPE_PEM) <= 0)
		csync_fatal("SSL: failed to use key file %s.\n", ssl_keyfile);

	if (SSL_CTX_use_certificate_file(conn_ssl_ctx, ssl_certfile, SSL_FILETYPE_PEM) <= 0)
		csync_fatal("SSL: failed to use certificate file %s.\n", ssl_certfile);

	if (! (conn_ssl = SSL_new(conn_ssl_ctx)) )
		csync_fatal("Creating a new SSL handle failed.\n");

	gnutls_certificate_server_set_request(conn_ssl->gnutls_state, GNUTLS_CERT_REQUIRE);

	SSL_set_rfd(conn_ssl, conn_fd_in);
	SSL_set_wfd(conn_ssl, conn_fd_out);

	if ( (server_role ? SSL_accept : SSL_connect)(conn_ssl) < 1 )
		csync_fatal("Establishing SSL connection failed.\n");

	csync_conn_usessl = 1;

	return 0;
}

int conn_check_peer_cert(const char *peername, int callfatal)
{
	const X509 *peercert;
	int i, cert_is_ok = -1;

	if (!csync_conn_usessl)
		return 1;

	peercert = SSL_get_peer_certificate(conn_ssl);

	if (!peercert || peercert->size <= 0) {
		if (callfatal)
			csync_fatal("Peer did not provide an SSL X509 cetrificate.\n");
		csync_debug(1, "Peer did not provide an SSL X509 cetrificate.\n");
		return 0;
	}

	{
		char certdata[peercert->size*2 + 1];

		for (i=0; i<peercert->size; i++)
			sprintf(certdata+i*2, "%02X", peercert->data[i]);
		certdata[peercert->size*2] = 0;

		SQL_BEGIN("Checking peer x509 certificate.",
			"SELECT certdata FROM x509_cert WHERE peername = '%s'",
			url_encode(peername))
		{
			if (!strcmp(SQL_V(0), certdata))
				cert_is_ok = 1;
			else
				cert_is_ok = 0;
		} SQL_END;

		if (cert_is_ok < 0) {
			csync_debug(1, "Adding peer x509 certificate to db: %s\n", certdata);
			SQL("Adding peer x509 sha1 hash to database.",
				"INSERT INTO x509_cert (peername, certdata) VALUES ('%s', '%s')",
				url_encode(peername), url_encode(certdata));
			return 1;
		}

		csync_debug(2, "Peer x509 certificate is: %s\n", certdata);

		if (!cert_is_ok) {
			if (callfatal)
				csync_fatal("Peer did provide a wrong SSL X509 cetrificate.\n");
			csync_debug(1, "Peer did provide a wrong SSL X509 cetrificate.\n");
			return 0;
		}
	}

	return 1;
}

#else

int conn_check_peer_cert(const char *peername, int callfatal)
{
	return 1;
}

#endif /* HAVE_LIBGNUTLS_OPENSSL */

int conn_close()
{
	if ( !conn_clisok ) return -1;

#ifdef HAVE_LIBGNUTLS_OPENSSL
	if ( csync_conn_usessl ) SSL_free(conn_ssl);
#endif

	if ( conn_fd_in != conn_fd_out) close(conn_fd_in);
	close(conn_fd_out);

	conn_fd_in  = -1;
	conn_fd_out = -1;
	conn_clisok =  0;

	return 0;
}

static inline int READ(void *buf, size_t count)
{
#ifdef HAVE_LIBGNUTLS_OPENSSL
	if (csync_conn_usessl)
		return SSL_read(conn_ssl, buf, count);
	else
#endif
		return read(conn_fd_in, buf, count);
}

static inline int WRITE(const void *buf, size_t count)
{
	static int n, total;

#ifdef HAVE_LIBGNUTLS_OPENSSL
	if (csync_conn_usessl)
		return SSL_write(conn_ssl, buf, count);
	else
#endif
	{
		total = 0;

		while (count > total) {
			n = write(conn_fd_out, ((char *) buf) + total, count - total);

			if (n >= 0)
				total += n;
			else {
				if (errno == EINTR)
					continue;
				else
					return -1;
			}
		}

		return total;
	}
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

