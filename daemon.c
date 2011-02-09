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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>

#ifdef __CYGWIN__
#include <w32api/windows.h>
#endif

static char *cmd_error;

int csync_setBackupFileStatus(char *filename, int backupDirLength);


int csync_unlink(const char *filename, int ign)
{
	struct stat st;
	int rc;

	if ( lstat_strict(prefixsubst(filename), &st) != 0 ) return 0;
	if ( ign==2 && S_ISREG(st.st_mode) ) return 0;
	rc = S_ISDIR(st.st_mode) ? rmdir(prefixsubst(filename)) : unlink(prefixsubst(filename));

	if ( rc && !ign ) cmd_error = strerror(errno);
	return rc;
}

int csync_check_dirty(const char *filename, const char *peername, int isflush)
{
	int rc = 0;
	csync_check(filename, 0, 0);
	if (isflush) return 0;
	SQL_BEGIN("Check if file is dirty",
		"SELECT 1 FROM dirty WHERE filename = '%s' LIMIT 1",
		url_encode(filename))
	{
		rc = 1;
		cmd_error = "File is also marked dirty here!";
	} SQL_END;
	if (rc && peername)
		csync_mark(filename, peername, 0);
	return rc;
}

void csync_file_update(const char *filename, const char *peername)
{
	struct stat st;
	SQL("Removing file from dirty db",
			"delete from dirty where filename = '%s' and peername = '%s'",
			url_encode(filename), peername);
	if ( lstat_strict(prefixsubst(filename), &st) != 0 || csync_check_pure(filename) ) {
		SQL("Removing file from file db",
			"delete from file where filename = '%s'",
			url_encode(filename));
	} else {
		const char *checktxt = csync_genchecktxt(&st, filename, 0);

		SQL("Deleting old record from file db",
			"DELETE FROM file WHERE filename = '%s'",
			url_encode(filename));

		SQL("Insert record to file db",
			"INSERT INTO file (filename, checktxt) values "
			"('%s', '%s')", url_encode(filename),
			url_encode(checktxt));
	}
}

void csync_file_flush(const char *filename)
{
	SQL("Removing file from dirty db",
		"delete from dirty where filename ='%s'",
		url_encode(filename));
}

int csync_file_backup(const char *filename)
{
	static char error_buffer[1024];
	const struct csync_group *g = NULL;
	struct stat buf;
	int rc;
	while ( (g=csync_find_next(g, filename)) ) {
	  if (g->backup_directory && g->backup_generations > 1) {
	    
	    int bak_dir_len = strlen(g->backup_directory);
	    int filename_len = strlen(filename);
	    char backup_filename[bak_dir_len + filename_len + 10];
	    char backup_otherfilename[bak_dir_len + filename_len + 10];
	    int fd_in, fd_out, i;
	    int lastSlash = 0;
	    mode_t mode;
	    csync_debug(1, "backup\n");
	    // Skip generation of directories
	    rc =  stat(filename, &buf);
	    if (S_ISDIR(buf.st_mode)) {
	      csync_debug(1, "directory. Skip generation \n");
	      return 0;
	    }

	    fd_in = open(filename, O_RDONLY);
	    if (fd_in < 0) 
	      return 0;
			
	    memcpy(backup_filename, g->backup_directory, bak_dir_len);
	    backup_filename[bak_dir_len] = 0;
	    mode = 0777;


	    for (i=filename_len; i> 0; i--)
	      if (filename[i] == '/')  {
		lastSlash = i;
		break;
	      }
			
	    for (i=0; i < filename_len; i++) {
		// Create directories in filename
		// TODO: Get the mode from the orig. dir
	      if (filename[i] == '/' && i <= lastSlash) {

		backup_filename[bak_dir_len+i] = 0;

		csync_debug(1, "mkdir %s \n", backup_filename);

		mkdir(backup_filename, mode);
		// Dont check the empty string.
		if (i!= 0)
		  csync_setBackupFileStatus(backup_filename, bak_dir_len);

	      }
		backup_filename[bak_dir_len+i] = filename[i];
	    }

	    backup_filename[bak_dir_len + filename_len] = 0;
	    backup_filename[bak_dir_len] = '/';
	    memcpy(backup_otherfilename, backup_filename,
		   bak_dir_len + filename_len);
	    
	    //rc = unlink(
	    for (i=g->backup_generations-1; i; i--) {

	      if (i != 1)
		snprintf(backup_filename+bak_dir_len+filename_len, 10, ".%d", i-1);
	      else
		snprintf(backup_filename+bak_dir_len+filename_len, 10, "");
	      snprintf(backup_otherfilename+bak_dir_len+filename_len, 10, ".%d", i);

	      rc = rename(backup_filename, backup_otherfilename);
	      csync_debug(1, "renaming backup files '%s' to '%s'. rc = %d\n", backup_filename, backup_otherfilename, rc);
	      
	    }

	    /* strcpy(backup_filename+bak_dir_len+filename_len, ""); */

	    fd_out = open(backup_filename, O_WRONLY|O_CREAT, 0600);

	    if (fd_out < 0) {
	      snprintf(error_buffer, 1024,
		       "Open error while backing up '%s': %s\n",
		       filename, strerror(errno));
	      cmd_error = error_buffer;
	      close(fd_in);
	      return 1;
	    }

	    csync_debug(1,"Copying data from %s to backup file %s \n", filename, backup_filename);

	    rc  = csync_copy_file(fd_in, fd_out);
	    if (rc != 0) {
		csync_debug(1, "csync_backup error 2\n");

		snprintf(error_buffer, 1024,
			 "Write error while backing up '%s': %s\n",
			 filename, strerror(errno));

		cmd_error = error_buffer;
		// TODO verify file disapeared ? 
		// 
		// return 1;
	    }
	    csync_setBackupFileStatus(backup_filename, bak_dir_len);
	    csync_debug(1, "csync_backup loop end\n");
	  }
	}
	csync_debug(1, "csync_backup end\n");
	return 0;
}

int csync_copy_file(int fd_in, int fd_out) 
{
  char buffer[512];
  int read_len = read(fd_in, buffer, 512);

  while (read_len > 0) {
    int write_len = 0;
    
    while (write_len < read_len) {
      int rc = write(fd_out, buffer+write_len, read_len-write_len);
      if (rc == -1) {
	close(fd_in);
	close(fd_out);
	//TODO verify return code. 
	return errno;
      }
      write_len += rc;
    }
    read_len = read(fd_in, buffer, 512);
  }	
  close(fd_in);
  close(fd_out);
  return 0;
}

/* get the mode from the orig directory.
   Looking from the back_dir_len should produce the original dir.
*/
int csync_setBackupFileStatus(char *filename, int backupDirLength) {

  struct stat buf;
  int rc = stat((filename + backupDirLength), &buf);
  if (rc == 0 ) {
    csync_debug(0, "Stating original file %s rc: %d mode: %o", (filename + backupDirLength), rc, buf.st_mode);

    rc = chown(filename, buf.st_uid, buf.st_gid);
    csync_debug(0, "Changing owner of %s to user %d and group %d, rc= %d \n", 
		filename, buf.st_uid, buf.st_gid, rc);

    rc = chmod(filename, buf.st_mode);
    csync_debug(0, "Changing mode of %s to mode %d, rc= %d \n", 
		filename, buf.st_mode, rc);

  }
  else {
    csync_debug(0, "Error getting mode and owner ship from %s \n", (filename + backupDirLength));
    return -1;
  }
  return 0;
};

struct csync_command {
	char *text;
	int check_perm;
	int check_dirty;
	int unlink;
	int update;
	int need_ident;
	int action;
};

enum {
	A_SIG, A_FLUSH, A_MARK, A_TYPE, A_GETTM, A_GETSZ, A_DEL, A_PATCH,
	A_MKDIR, A_MKCHR, A_MKBLK, A_MKFIFO, A_MKLINK, A_MKSOCK,
	A_SETOWN, A_SETMOD, A_SETIME, A_LIST, A_GROUP,
	A_DEBUG, A_HELLO, A_BYE
};

struct csync_command cmdtab[] = {
	{ "sig",	1, 0, 0, 0, 1, A_SIG	},
	{ "mark",	1, 0, 0, 0, 1, A_MARK	},
	{ "type",	2, 0, 0, 0, 1, A_TYPE	},
	{ "gettm",	1, 0, 0, 0, 1, A_GETTM	},
	{ "getsz",	1, 0, 0, 0, 1, A_GETSZ	},
	{ "flush",	1, 1, 0, 0, 1, A_FLUSH	},
	{ "del",	1, 1, 0, 1, 1, A_DEL	},
	{ "patch",	1, 1, 2, 1, 1, A_PATCH	},
	{ "mkdir",	1, 1, 1, 1, 1, A_MKDIR	},
	{ "mkchr",	1, 1, 1, 1, 1, A_MKCHR	},
	{ "mkblk",	1, 1, 1, 1, 1, A_MKBLK	},
	{ "mkfifo",	1, 1, 1, 1, 1, A_MKFIFO	},
	{ "mklink",	1, 1, 1, 1, 1, A_MKLINK	},
	{ "mksock",	1, 1, 1, 1, 1, A_MKSOCK	},
	{ "setown",	1, 1, 0, 2, 1, A_SETOWN	},
	{ "setmod",	1, 1, 0, 2, 1, A_SETMOD	},
	{ "setime",	1, 0, 0, 2, 1, A_SETIME	},
	{ "list",	0, 0, 0, 0, 1, A_LIST	},
#if 0
	{ "debug",	0, 0, 0, 0, 1, A_DEBUG	},
#endif
	{ "group",	0, 0, 0, 0, 0, A_GROUP	},
	{ "hello",	0, 0, 0, 0, 0, A_HELLO	},
	{ "bye",	0, 0, 0, 0, 0, A_BYE	},
	{ 0,		0, 0, 0, 0, 0, 0	}
};

typedef union address {
	struct sockaddr sa;
	struct sockaddr_in sa_in;
	struct sockaddr_in6 sa_in6;
	struct sockaddr_storage ss;
} address_t;

const char *csync_inet_ntop(address_t *addr)
{
	char buf[INET6_ADDRSTRLEN];
	sa_family_t af = addr->sa.sa_family;
	return inet_ntop(af,
		af == AF_INET  ? (void*)&addr->sa_in.sin_addr :
		af == AF_INET6 ? (void*)&addr->sa_in6.sin6_addr : NULL,
		buf, sizeof(buf));
}

/*
 * Loops (to cater for multihomed peers) through the address list returned by
 * gethostbyname(), returns 1 if any match with the address obtained from
 * getpeername() during session startup.
 * Otherwise returns 0 (-> identification failed).
 *
 * TODO switch to a getnameinfo in conn_open.
 * TODO add a "pre-authenticated" pipe mode for use over ssh */
int verify_peername(const char *name, address_t *peeraddr)
{
	sa_family_t af = peeraddr->sa.sa_family;
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int try_mapped_ipv4;
	int s;

	/* Obtain address(es) matching host */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */

	s = getaddrinfo(name, NULL, &hints, &result);
	if (s != 0) {
		csync_debug(1, "getaddrinfo: %s\n", gai_strerror(s));
		return 0;
	}

	try_mapped_ipv4 =
		af == AF_INET6 &&
		!memcmp(&peeraddr->sa_in6.sin6_addr,
			"\0\0\0\0" "\0\0\0\0" "\0\0\xff\xff", 12);

	/* getaddrinfo() returns a list of address structures.
	 * Try each address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		/* both IPv4 */
		if (af == AF_INET && rp->ai_family == AF_INET &&
		    !memcmp(&((struct sockaddr_in*)rp->ai_addr)->sin_addr,
			    &peeraddr->sa_in.sin_addr, sizeof(struct in_addr)))
			break;
		/* both IPv6 */
		if (af == AF_INET6 && rp->ai_family == AF_INET6 &&
		    !memcmp(&((struct sockaddr_in6*)rp->ai_addr)->sin6_addr,
			    &peeraddr->sa_in6.sin6_addr, sizeof(struct in6_addr)))
			break;
		/* peeraddr IPv6, but actually ::ffff:I.P.v.4,
		 * and forward lookup returned IPv4 only */
		if (af == AF_INET6 && rp->ai_family == AF_INET &&
		    try_mapped_ipv4 &&
		    !memcmp(&((struct sockaddr_in*)rp->ai_addr)->sin_addr,
			    (unsigned char*)&peeraddr->sa_in6.sin6_addr + 12,
			    sizeof(struct in_addr)))
			break;
	}
	freeaddrinfo(result);
	if (rp != NULL) /* memcmp found a match */
		return conn_check_peer_cert(name, 0);
	return 0;
}

/* Why do all this fuzz, and not simply --assume-authenticated?
 * To limit the impact of an accidental misconfiguration.
 */
void set_peername_from_env(address_t *p, const char *env)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV,
	};
	struct addrinfo *result;
	char *c;
	int s;

	char *val = getenv(env);
	csync_debug(3, "getenv(%s): >>%s<<\n", env, val ?: "");
	if (!val)
		return;
	val = strdup(val);
	if (!val)
		return;

	c = strchr(val, ' ');
	if (!c)
		return;
	*c = '\0';

	s = getaddrinfo(val, NULL, &hints, &result);
	if (s != 0) {
		csync_debug(1, "getaddrinfo: %s\n", gai_strerror(s));
		return;
	}

	/* getaddrinfo() may return a list of address structures.
	 * Use the first one. */
	if (result)
		memcpy(p, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);
}

void csync_daemon_session()
{
	struct stat sb;
	address_t peername = { .sa.sa_family = AF_UNSPEC, };
	socklen_t peerlen = sizeof(peername);
	char line[4096], *peer=0, *tag[32];
	int i;


	if (fstat(0, &sb))
		csync_fatal("Can't run fstat on fd 0: %s", strerror(errno));

	switch (sb.st_mode & S_IFMT) {
	case S_IFSOCK:
		if ( getpeername(0, &peername.sa, &peerlen) == -1 )
			csync_fatal("Can't run getpeername on fd 0: %s", strerror(errno));
		break;
	case S_IFIFO:
		set_peername_from_env(&peername, "SSH_CLIENT");
		break;
		/* fall through */
	default:
		csync_fatal("I'm only talking to sockets or pipes! %x\n", sb.st_mode & S_IFMT);
		break;
	}

	while ( conn_gets(line, 4096) ) {
		int cmdnr;

		tag[i=0] = strtok(line, "\t \r\n");
		while ( tag[i] && i < 31 )
			tag[++i] = strtok(0, "\t \r\n");
		while ( i < 32 )
			tag[i++] = "";

		if ( !tag[0][0] ) continue;

		for (i=0; i<32; i++)
			tag[i] = strdup(url_decode(tag[i]));

		for (cmdnr=0; cmdtab[cmdnr].text; cmdnr++)
			if ( !strcasecmp(cmdtab[cmdnr].text, tag[0]) ) break;

		if ( !cmdtab[cmdnr].text ) {
			cmd_error = "Unkown command!";
			goto abort_cmd;
		}

		cmd_error = 0;

		if ( cmdtab[cmdnr].need_ident && !peer ) {
			conn_printf("Dear %s, please identify first.\n",
				    csync_inet_ntop(&peername) ?: "stranger");
			goto next_cmd;
		}

		if ( cmdtab[cmdnr].check_perm )
			on_cygwin_lowercase(tag[2]);

		if ( cmdtab[cmdnr].check_perm ) {
			if ( cmdtab[cmdnr].check_perm == 2 )
				csync_compare_mode = 1;
			int perm = csync_perm(tag[2], tag[1], peer);
			if ( cmdtab[cmdnr].check_perm == 2 )
				csync_compare_mode = 0;
			if ( perm ) {
				if ( perm == 2 ) {
					csync_mark(tag[2], peer, 0);
					cmd_error = "Permission denied for slave!";
				} else
					cmd_error = "Permission denied!";
				goto abort_cmd;
			}
		}

		if ( cmdtab[cmdnr].check_dirty && csync_check_dirty(tag[2], peer,
				cmdtab[cmdnr].action == A_FLUSH) ) goto abort_cmd;

		if ( cmdtab[cmdnr].unlink )
				csync_unlink(tag[2], cmdtab[cmdnr].unlink);

		switch ( cmdtab[cmdnr].action )
		{
		case A_SIG:
			{
				struct stat st;

				if ( lstat_strict(prefixsubst(tag[2]), &st) != 0 ) {
					if ( errno == ENOENT )
						conn_printf("OK (not_found).\n---\noctet-stream 0\n");
					else
						cmd_error = strerror(errno);
					break;
				} else
					if ( csync_check_pure(tag[2]) ) {
						conn_printf("OK (not_found).\n---\noctet-stream 0\n");
						break;
					}
				conn_printf("OK (data_follows).\n");
				conn_printf("%s\n", csync_genchecktxt(&st, tag[2], 1));

				if ( S_ISREG(st.st_mode) )
					csync_rs_sig(tag[2]);
				else
					conn_printf("octet-stream 0\n");
			}
			break;
		case A_MARK:
			csync_mark(tag[2], peer, 0);
			break;
		case A_TYPE:
			{
				FILE *f = fopen(prefixsubst(tag[2]), "rb");

				if (!f && errno == ENOENT)
					f = fopen("/dev/null", "rb");

				if (f) {
					char buffer[512];
					size_t rc;

					conn_printf("OK (data_follows).\n");
					while ( (rc=fread(buffer, 1, 512, f)) > 0 )
						if ( conn_write(buffer, rc) != rc ) {
							conn_printf("[[ %s ]]", strerror(errno));
							break;
						}
					fclose(f);
					return;
				}
				cmd_error = strerror(errno);
			}
			break;
		case A_GETTM:
		case A_GETSZ:
			{
				struct stat sbuf;
				conn_printf("OK (data_follows).\n");
				if (!lstat_strict(prefixsubst(tag[2]), &sbuf))
					conn_printf("%ld\n", cmdtab[cmdnr].action == A_GETTM ?
							(long)sbuf.st_mtime : (long)sbuf.st_size);
				else
					conn_printf("-1\n");
				goto next_cmd;
			}
			break;
		case A_FLUSH:
			SQL("Flushing dirty entry (if any) for file",
				"DELETE FROM dirty WHERE filename = '%s'",
				url_encode(tag[2]));
			break;
		case A_DEL:
			if (!csync_file_backup(tag[2]))
				csync_unlink(tag[2], 0);
			break;
		case A_PATCH:
			if (!csync_file_backup(tag[2])) {
				conn_printf("OK (send_data).\n");
				csync_rs_sig(tag[2]);
				if (csync_rs_patch(tag[2]))
					cmd_error = strerror(errno);
			}
			break;
		case A_MKDIR:
			/* ignore errors on creating directories if the
			 * directory does exist already. we don't need such
			 * a check for the other file types, because they
			 * will simply be unlinked if already present.
			 */
#ifdef __CYGWIN__
			// This creates the file using the native windows API, bypassing
			// the cygwin wrappers and so making sure that we do not mess up the
			// permissions..
			{
				char winfilename[MAX_PATH];
				cygwin_conv_to_win32_path(prefixsubst(tag[2]), winfilename);

				if ( !CreateDirectory(TEXT(winfilename), NULL) ) {
					struct stat st;
					if ( lstat_strict(prefixsubst(tag[2]), &st) != 0 || !S_ISDIR(st.st_mode)) {
						csync_debug(1, "Win32 I/O Error %d in mkdir command: %s\n",
								(int)GetLastError(), winfilename);
						cmd_error = "Win32 I/O Error on CreateDirectory()";
					}
				}
			}
#else
			if ( mkdir(prefixsubst(tag[2]), 0700) ) {
				struct stat st;
				if ( lstat_strict((prefixsubst(tag[2])), &st) != 0 || !S_ISDIR(st.st_mode))
					cmd_error = strerror(errno);
			}
#endif
			break;
		case A_MKCHR:
			if ( mknod(prefixsubst(tag[2]), 0700|S_IFCHR, atoi(tag[3])) )
				cmd_error = strerror(errno);
			break;
		case A_MKBLK:
			if ( mknod(prefixsubst(tag[2]), 0700|S_IFBLK, atoi(tag[3])) )
				cmd_error = strerror(errno);
			break;
		case A_MKFIFO:
			if ( mknod(prefixsubst(tag[2]), 0700|S_IFIFO, 0) )
				cmd_error = strerror(errno);
			break;
		case A_MKLINK:
			if ( symlink(tag[3], prefixsubst(tag[2])) )
				cmd_error = strerror(errno);
			break;
		case A_MKSOCK:
			/* just ignore socket files */
			break;
		case A_SETOWN:
			if ( !csync_ignore_uid || !csync_ignore_gid ) {
				int uid = csync_ignore_uid ? -1 : atoi(tag[3]);
				int gid = csync_ignore_gid ? -1 : atoi(tag[4]);
				if ( lchown(prefixsubst(tag[2]), uid, gid) )
					cmd_error = strerror(errno);
			}
			break;
		case A_SETMOD:
			if ( !csync_ignore_mod ) {
				if ( chmod(prefixsubst(tag[2]), atoi(tag[3])) )
					cmd_error = strerror(errno);
			}
			break;
		case A_SETIME:
			{
				struct utimbuf utb;
				utb.actime = atoll(tag[3]);
				utb.modtime = atoll(tag[3]);
				if ( utime(prefixsubst(tag[2]), &utb) )
					cmd_error = strerror(errno);
			}
			break;
		case A_LIST:
			SQL_BEGIN("DB Dump - Files for sync pair",
				"SELECT checktxt, filename FROM file %s%s%s ORDER BY filename",
					strcmp(tag[2], "-") ? "WHERE filename = '" : "",
					strcmp(tag[2], "-") ? url_encode(tag[2]) : "",
					strcmp(tag[2], "-") ? "'" : "")
			{
				if ( csync_match_file_host(url_decode(SQL_V(1)), tag[1], peer, (const char **)&tag[3]) )
					conn_printf("%s\t%s\n", SQL_V(0), SQL_V(1));
			} SQL_END;
			break;

		case A_DEBUG:
			csync_debug_out = stdout;
			if ( tag[1][0] )
				csync_debug_level = atoi(tag[1]);
			break;
		case A_HELLO:
			if (peer) {
				free(peer);
				peer = NULL;
			}
			if (verify_peername(tag[1], &peername)) {
				peer = strdup(tag[1]);
			} else {
				peer = NULL;
				cmd_error = "Identification failed!";
				break;
			}
#ifdef HAVE_LIBGNUTLS
			if (!csync_conn_usessl) {
				struct csync_nossl *t;
				for (t = csync_nossl; t; t=t->next) {
					if ( !fnmatch(t->pattern_from, myhostname, 0) &&
					     !fnmatch(t->pattern_to, peer, 0) )
						goto conn_without_ssl_ok;
				}
				cmd_error = "SSL encrypted connection expected!";
			}
conn_without_ssl_ok:;
#endif
			break;
		case A_GROUP:
			if (active_grouplist) {
				cmd_error = "Group list already set!";
			} else {
				const struct csync_group *g;
				int i, gnamelen;

				active_grouplist = strdup(tag[1]);
				for (g=csync_group; g; g=g->next) {
					if (!g->myname) continue;

					i=0;
					gnamelen = strlen(csync_group->gname);
					while (active_grouplist[i])
					{
						if ( !strncmp(active_grouplist+i, csync_group->gname, gnamelen) &&
								(active_grouplist[i+gnamelen] == ',' ||
								 !active_grouplist[i+gnamelen]) )
							goto found_asactive;
						while (active_grouplist[i])
							if (active_grouplist[i++]==',') break;
					}
					csync_group->myname = 0;
found_asactive: ;
				}
			}
			break;
		case A_BYE:
			for (i=0; i<32; i++)
				free(tag[i]);
			conn_printf("OK (cu_later).\n");
			return;
		}

		if ( cmdtab[cmdnr].update )
			csync_file_update(tag[2], peer);

		if ( cmdtab[cmdnr].update == 1 ) {
			csync_debug(1, "Updated %s from %s.\n",
					tag[2], peer ? peer : "???");
			csync_schedule_commands(tag[2], 0);
		}

abort_cmd:
		if ( cmd_error )
			conn_printf("%s\n", cmd_error);
		else
			conn_printf("OK (cmd_finished).\n");

next_cmd:
		for (i=0; i<32; i++)
			free(tag[i]);
	}
}
