/*
 *  csync2 - cluster syncronisation tool, 2nd generation
 *  LINBIT Information Technologies <http://www.linbit.com>
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

%{
#include "csync2.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct csync_group *csync_group = 0;

extern void yyerror(char* text);
extern int yylex();

void yyerror(char *text)
{
	csync_fatal("Parse error: %s\n", text);
}

static void new_group()
{
	struct csync_group *t =
		calloc(sizeof(struct csync_group), 1);
	t->next = csync_group;
	csync_group = t;
}

static void add_host(const char *hostname, const char *peername)
{
	if ( strcmp(hostname, myhostname) == 0 ) {
		csync_group->myname = peername;
		free((void*)hostname);
	} else {
		struct csync_group_host *t =
			calloc(sizeof(struct csync_group_host), 1);
		t->hostname = peername;
		t->next = csync_group->host;
		csync_group->host = t;
		free((void*)hostname);
	}
}

static void add_patt(int isinclude, const char *pattern)
{
	struct csync_group_pattern *t =
		calloc(sizeof(struct csync_group_pattern), 1);
	t->isinclude = isinclude;
	t->pattern = pattern;
	t->next = csync_group->pattern;
	csync_group->pattern = t;
}

static void set_key(const char *key)
{
	if ( csync_group->key )
		csync_fatal("Config error: a group might only have one key.\n");
	csync_group->key = key;
}

static void check_group()
{
	if ( ! csync_group->key )
		csync_fatal("Config error: every group must have a key.\n");

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

	/* dump config for debugging */
	if ( csync_debug_level >= 2 ) {
		struct csync_group_host *h = csync_group->host;
		struct csync_group_pattern *p = csync_group->pattern;
		csync_debug(2, "group {\n\tkey\t%s;\n", csync_group->key);
		while (h) {
			csync_debug(2, "\thost\twhocares@%s;\n",
				h->hostname);
			h = h->next;
		}
		if ( csync_group->myname )
			csync_debug(2, "\thost\t%s@%s;\n",
				myhostname, csync_group->myname);
		while (p) {
			csync_debug(2, "\t%s\t%s;\n",
				p->isinclude ? "include" : "exclude",
				p->pattern);
			p = p->next;
		}
		csync_debug(2, "}\n");
	}
}

%}

%union {
	char *txt;
}

%token TK_BLOCK_BEGIN TK_BLOCK_END TK_STEND TK_AT
%token TK_GROUP TK_HOST TK_EXCL TK_INCL TK_KEY
%token <txt> TK_STRING

%%

config:		/* empty */
	|	config_block config
		;

config_block:	config_block_header config_block_body
		;
		
config_block_header:
		TK_GROUP		{ new_group(); }
		;

config_block_body:
		TK_BLOCK_BEGIN config_stmts TK_BLOCK_END
					{ check_group(); }
		;

config_stmts:	/* empty */
	|	config_stmt TK_STEND config_stmts
		;

config_stmt:	TK_HOST host_list
	|	TK_EXCL excl_list
	|	TK_INCL incl_list
	|	TK_KEY  TK_STRING	{ set_key($2); }
		;

host_list:	/* empty */
	|	host_list TK_STRING	{ add_host($2, $2); }
	|	host_list TK_STRING TK_AT TK_STRING
					{ add_host($2, $4); }
		;
		
excl_list:	/* empty */
	|	excl_list TK_STRING	{ add_patt(0, $2); }
		;
		
incl_list:	/* empty */
	|	incl_list TK_STRING	{ add_patt(1, $2); }
		;

