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

%{
#include "csync2.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fnmatch.h>
#include <ctype.h>

struct csync_group  *csync_group  = 0;
struct csync_prefix *csync_prefix = 0;
struct csync_nossl  *csync_nossl  = 0;

int csync_ignore_uid = 0;
int csync_ignore_gid = 0;
int csync_ignore_mod = 0;

#ifdef __CYGWIN__
int csync_lowercyg_disable = 0;
int csync_lowercyg_used = 0;
#endif

extern void yyerror(char* text);
extern int yylex();
extern int yylineno;

void yyerror(char *text)
{
	csync_fatal("Near line %d: %s\n", yylineno, text);
}

static void new_group(char *name)
{
	int static autonum = 1;
	struct csync_group *t =
		calloc(sizeof(struct csync_group), 1);

	if (name == 0)
		asprintf(&name, "group_%d", autonum++);

	t->next = csync_group;
	t->auto_method = -1;
	t->gname = name;
	t->backup_generations = 3;
	csync_group = t;
}

static void add_host(char *hostname, char *peername, int slave)
{
	int i;
	for (i=0; hostname[i]; i++)
		hostname[i] = tolower(hostname[i]);
	for (i=0; peername[i]; i++)
		peername[i] = tolower(peername[i]);
	if ( strcmp(hostname, myhostname) == 0 ) {
		csync_group->local_slave = slave;
		csync_group->myname = peername;
		free(hostname);
	} else {
		struct csync_group_host *t =
			calloc(sizeof(struct csync_group_host), 1);
		t->hostname = peername;
		t->on_left_side = !csync_group->myname;
		t->slave = slave;
		t->next = csync_group->host;
		csync_group->host = t;
		free(hostname);
	}
}

static void add_patt(int patterntype, char *pattern)
{
	struct csync_group_pattern *t =
		calloc(sizeof(struct csync_group_pattern), 1);
	int i;

#if __CYGWIN__
	if (isalpha(pattern[0]) && pattern[1] == ':' &&
	    (pattern[2] == '/' || pattern[2] == '\\')) {
		char *new_pattern, *p;
		asprintf(&new_pattern, "/cygdrive/%c/%s",
			tolower(pattern[0]), pattern+3);
		for (p = new_pattern; *p; p++)
			if (*p == '\\') *p = '/';
		free(pattern);
		pattern = new_pattern;
	}
#endif

	for (i=strlen(pattern)-1; i>0; i--)
		if (pattern[i] == '/')
			pattern[i] = 0;
		else
			break;

	t->isinclude = patterntype >= 1;
	t->iscompare = patterntype >= 2;
	t->pattern = pattern;
	t->next = csync_group->pattern;
	csync_group->pattern = t;
}

static void set_key(char *keyfilename)
{
	FILE *keyfile;
	char line[1024];
	int i;

	if ( csync_group->key )
		csync_fatal("Config error: a group might only have one key.\n");

	if ( (keyfile = fopen(keyfilename, "r")) == 0 ||
	     fgets(line, 1024, keyfile) == 0 )
		csync_fatal("Config error: Can't read keyfile %s.\n", keyfilename);

	for (i=0; line[i]; i++) {
		if (line[i] == '\n') { line[i]=0; break; }
		if ( !(line[i] >= 'A' && line[i] <= 'Z') &&
		     !(line[i] >= 'a' && line[i] <= 'z') &&
		     !(line[i] >= '0' && line[i] <= '9') &&
		     line[i] != '.' && line[i] != '_' )
			csync_fatal("Unallowed character '%c' in key file %s.\n",
					line[i], keyfilename);
	}

	if ( strlen(line) < 32 )
		csync_fatal("Config error: Key in file %s is too short.\n", keyfilename);

	csync_group->key = strdup(line);
	free(keyfilename);
	fclose(keyfile);
}

static void set_auto(char *auto_method)
{
	int method_id = -1;

	if (csync_group->auto_method >= 0)
		csync_fatal("Config error: a group might only have one auto-setting.\n");

	if (!strcmp(auto_method, "none"))
		method_id = CSYNC_AUTO_METHOD_NONE;

	if (!strcmp(auto_method, "first"))
		method_id = CSYNC_AUTO_METHOD_FIRST;

	if (!strcmp(auto_method, "younger"))
		method_id = CSYNC_AUTO_METHOD_YOUNGER;

	if (!strcmp(auto_method, "older"))
		method_id = CSYNC_AUTO_METHOD_OLDER;

	if (!strcmp(auto_method, "bigger"))
		method_id = CSYNC_AUTO_METHOD_BIGGER;

	if (!strcmp(auto_method, "smaller"))
		method_id = CSYNC_AUTO_METHOD_SMALLER;

	if (!strcmp(auto_method, "left"))
		method_id = CSYNC_AUTO_METHOD_LEFT;

	if (!strcmp(auto_method, "right"))
		method_id = CSYNC_AUTO_METHOD_RIGHT;

	if (method_id < 0)
		csync_fatal("Config error: Unknown auto-setting '%s' (use "
			"'none', 'younger', 'older', 'bigger', 'smaller', "
			"'left' or 'right').\n", auto_method);

	csync_group->auto_method = method_id;
	free(auto_method);
}

static void set_bak_dir(char *dir)
{
	csync_group->backup_directory = dir;
}

static void set_bak_gen(char *gen)
{
	csync_group->backup_generations = atoi(gen);
	free(gen);
}

static void check_group()
{
	if ( ! csync_group->key )
		csync_fatal("Config error: every group must have a key.\n");

	if ( csync_group->auto_method < 0 )
		csync_group->auto_method = CSYNC_AUTO_METHOD_NONE;

	/* re-order hosts and pattern */
	{
		struct csync_group_host *t = csync_group->host;
		csync_group->host = 0;
		while ( t ) {
			struct csync_group_host *next = t->next;
			t->next = csync_group->host;
			csync_group->host = t;
			t = next;
		}
	}
	{
		struct csync_group_pattern *t = csync_group->pattern;
		csync_group->pattern = 0;
		while ( t ) {
			struct csync_group_pattern *next = t->next;
			t->next = csync_group->pattern;
			csync_group->pattern = t;
			t = next;
		}
	}

	if (active_peerlist) {
		struct csync_group_host *h;
		int i=0, thisplen;

		while (active_peerlist[i]) {
			thisplen = strcspn(active_peerlist + i, ",");

			for (h=csync_group->host; h; h=h->next)
				if (strlen(h->hostname) == thisplen && !strncmp(active_peerlist + i, h->hostname, thisplen))
					goto foundactivepeers;

			i += thisplen;
			while (active_peerlist[i] == ',') i++;
		}
	} else
foundactivepeers:
		csync_group->hasactivepeers = 1;

	if (active_grouplist && csync_group->myname)
	{
		int i=0, gnamelen = strlen(csync_group->gname);

		while (active_grouplist[i])
		{
			if ( !strncmp(active_grouplist+i, csync_group->gname, gnamelen) &&
			     (active_grouplist[i+gnamelen] == ',' || !active_grouplist[i+gnamelen]) )
				goto found_asactive;
			while (active_grouplist[i])
				if (active_grouplist[i++]==',') break;
		}

		csync_group->myname = 0;
found_asactive:	;
	}
}

static void new_action()
{
	struct csync_group_action *t =
		calloc(sizeof(struct csync_group_action), 1);
	t->next = csync_group->action;
	t->logfile = "/dev/null";
	csync_group->action = t;
}

static void add_action_pattern(const char *pattern)
{
	struct csync_group_action_pattern *t =
		calloc(sizeof(struct csync_group_action_pattern), 1);
	t->pattern = pattern;
	t->next = csync_group->action->pattern;
	csync_group->action->pattern = t;
}

static void add_action_exec(const char *command)
{
	struct csync_group_action_command *t =
		calloc(sizeof(struct csync_group_action_command), 1);
	t->command = command;
	t->next = csync_group->action->command;
	csync_group->action->command = t;
}

static void set_action_logfile(const char *logfile)
{
	csync_group->action->logfile = logfile;
}

static void set_action_dolocal()
{
	csync_group->action->do_local = 1;
}

static void new_prefix(const char *pname)
{
	struct csync_prefix *p =
		calloc(sizeof(struct csync_prefix), 1);
	p->name = pname;
	p->next = csync_prefix;
	csync_prefix = p;
}

static void new_prefix_entry(char *pattern, char *path)
{
	int i;

	if (path[0] != '/')
		csync_fatal("Config error: Prefix '%s' is not an absolute path.\n", path);

	if (!csync_prefix->path && !fnmatch(pattern, myhostname, 0)) {
#if __CYGWIN__
		if (isalpha(path[0]) && path[1] == ':' &&
		    (path[2] == '/' || path[2] == '\\')) {
			char *new_path, *p;
			asprintf(&new_path, "/cygdrive/%c/%s",
				tolower(path[0]), path+3);
			for (p = new_path; *p; p++)
				if (*p == '\\') *p = '/';
			free(path);
			path = new_path;
		}
#endif
		for (i=strlen(path)-1; i>0; i--)
			if (path[i] == '/')
				path[i] = 0;
			else
				break;

		csync_debug(2, "Prefix '%s' is set to '%s'.\n", csync_prefix->name, path);
		csync_prefix->path = path;
	} else
		free(path);
	free(pattern);
}

static void new_nossl(const char *from, const char *to)
{
	struct csync_nossl *t =
		calloc(sizeof(struct csync_nossl), 1);
	t->pattern_from = from;
	t->pattern_to = to;
	t->next = csync_nossl;
	csync_nossl = t;
}

static void new_ignore(char *propname)
{
	if ( !strcmp(propname, "uid") )
		csync_ignore_uid = 1;
	else
	if ( !strcmp(propname, "gid") )
		csync_ignore_gid = 1;
	else
	if ( !strcmp(propname, "mod") )
		csync_ignore_mod = 1;
	else
		csync_fatal("Config error: Unknown 'ignore' porperty: '%s'.\n", propname);

	free(propname);
}

static void disable_cygwin_lowercase_hack()
{
#ifdef __CYGWIN__
	if (csync_lowercyg_used)
		csync_fatal("Config error: 'nocygwinlowercase' must be at the top of the config file.\n");
	csync_lowercyg_disable = 1;
#else
	csync_fatal("Config error: Found 'nocygwinlowercase' but this is not a cygwin csync2.\n");
#endif
}

%}

%expect 2

%union {
	char *txt;
}

%token TK_BLOCK_BEGIN TK_BLOCK_END TK_STEND TK_AT TK_AUTO
%token TK_NOSSL TK_IGNORE TK_GROUP TK_HOST TK_EXCL TK_INCL TK_COMP TK_KEY
%token TK_ACTION TK_PATTERN TK_EXEC TK_DOLOCAL TK_LOGFILE TK_NOCYGLOWER
%token TK_PREFIX TK_ON TK_COLON TK_POPEN TK_PCLOSE
%token TK_BAK_DIR TK_BAK_GEN
%token <txt> TK_STRING

%%

config:
	/* empty */
|	block config
;

block:
	block_header block_body
|	TK_PREFIX TK_STRING
		{ new_prefix($2); }
		TK_BLOCK_BEGIN prefix_list TK_BLOCK_END
		{ }
|	TK_NOSSL TK_STRING TK_STRING TK_STEND
		{ new_nossl($2, $3); }
|	TK_IGNORE ignore_list TK_STEND
|	TK_NOCYGLOWER TK_STEND
		{ disable_cygwin_lowercase_hack(); }
;

ignore_list:
	/* empty */
|	TK_STRING ignore_list
		{ new_ignore($1); }
;

prefix_list:
	/* empty */
|	prefix_list TK_ON TK_STRING TK_COLON TK_STRING TK_STEND
		{ new_prefix_entry($3, on_cygwin_lowercase($5)); }
;

block_header:
	TK_GROUP
		{ new_group(0);  }
|	TK_GROUP TK_STRING
		{ new_group($2); }
;

block_body:
	TK_BLOCK_BEGIN stmts TK_BLOCK_END
		{ check_group(); }
;

stmts:
	/* empty */
|	stmt TK_STEND stmts
|	action stmts
;

stmt:
	TK_HOST host_list
|	TK_EXCL excl_list
|	TK_INCL incl_list
|	TK_COMP comp_list
|	TK_KEY TK_STRING
		{ set_key($2); }
|	TK_AUTO TK_STRING
		{ set_auto($2); }
|	TK_BAK_DIR TK_STRING
		{ set_bak_dir($2); }
|	TK_BAK_GEN TK_STRING
		{ set_bak_gen($2); }
;

host_list:
	/* empty */
|	host_list TK_STRING
		{ add_host($2, strdup($2), 0); }
|	host_list TK_STRING TK_AT TK_STRING
		{ add_host($2, $4, 0); }
|	host_list TK_POPEN host_list_slaves TK_PCLOSE host_list
;

host_list_slaves:
	/* empty */
|	host_list_slaves TK_STRING
		{ add_host($2, strdup($2), 1); }
|	host_list_slaves TK_STRING TK_AT TK_STRING
		{ add_host($2, $4, 1); }
;

excl_list:
	/* empty */
|	excl_list TK_STRING
		{ add_patt(0, on_cygwin_lowercase($2)); }
;

incl_list:
	/* empty */
|	incl_list TK_STRING
		{ add_patt(1, on_cygwin_lowercase($2)); }
;

comp_list:
	/* empty */
|	incl_list TK_STRING
		{ add_patt(2, on_cygwin_lowercase($2)); }
;

action:
	TK_ACTION
		{ new_action(); }
	TK_BLOCK_BEGIN action_stmts TK_BLOCK_END
;


action_stmts:
	/* empty */
|	action_stmt TK_STEND action_stmts
;

action_stmt:
	TK_PATTERN action_pattern_list
|	TK_EXEC action_exec_list
|	TK_LOGFILE TK_STRING
		{ set_action_logfile($2); }
|	TK_DOLOCAL
		{ set_action_dolocal(); }
;

action_pattern_list:
	/* empty */
|	action_pattern_list TK_STRING
		{ add_action_pattern(on_cygwin_lowercase($2)); }
;

action_exec_list:
	/* empty */
|	action_exec_list TK_STRING
		{ add_action_exec($2); }
;

