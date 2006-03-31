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
#include "cfgfile_parser.h"
#include <string.h>

#define MAX_INCLUDE_DEPTH 10
YY_BUFFER_STATE include_stack[MAX_INCLUDE_DEPTH];
int include_stack_ptr = 0;
%}

%option noyywrap yylineno
%x STRING INCL

%%
"{"		{ return TK_BLOCK_BEGIN; }
"}"		{ return TK_BLOCK_END; }
"("		{ return TK_POPEN; }
")"		{ return TK_PCLOSE; }

";"		{ return TK_STEND; }
":"		{ return TK_COLON; }
"@"		{ return TK_AT; }

"nossl"		{ return TK_NOSSL; }
"ignore"	{ return TK_IGNORE; }

"group"		{ return TK_GROUP; }
"host"		{ return TK_HOST; }
"exclude"	{ return TK_EXCL; }
"include"	{ return TK_INCL; }
"compare"	{ return TK_COMP; }
"key"		{ return TK_KEY; }
"auto"		{ return TK_AUTO; }

"action"	{ return TK_ACTION; }
"pattern"	{ return TK_PATTERN; }
"exec"		{ return TK_EXEC; }
"logfile"	{ return TK_LOGFILE; }
"do-local"	{ return TK_DOLOCAL; }

"prefix"	{ return TK_PREFIX; }
"on"		{ return TK_ON; }

"backup-directory"	{ return TK_BAK_DIR; }
"backup-generations"	{ return TK_BAK_GEN; }

"no-cygwin-lowercase"	{ return TK_NOCYGLOWER; }

"config"	BEGIN(INCL);
<INCL>[ \t]*	/* eat the whitespaces */
<INCL>[^ \t\r\n;]+ {
			if ( include_stack_ptr >= MAX_INCLUDE_DEPTH ) {
				fprintf(stderr, "Config includes nested too deeply.\n");
				exit(1);
			}
			include_stack[include_stack_ptr++] = YY_CURRENT_BUFFER;
			yyin = fopen(yytext, "r");
			if ( !yyin ) {
				fprintf(stderr, "Can't open included config file '%s'.\n", yytext);
				exit(1);
			}
			yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
			BEGIN(0);
		}
<INCL>";"	BEGIN(0);
<<EOF>>		{
			if ( !include_stack_ptr )
				yyterminate();
			else
			{
				yy_delete_buffer(YY_CURRENT_BUFFER);
				yy_switch_to_buffer(include_stack[--include_stack_ptr]);
				BEGIN(INCL);
			}
		}

\"		BEGIN(STRING);
<STRING>[^\"]*	{ yylval.txt=strdup(yytext); return TK_STRING; }
<STRING>\"	BEGIN(0);

[ \r\n\t]+	/* whitespaces are just delimiters */
#[^\r\n]*	/* this is a comment */

[^ \r\n\t@;\(\)#"]*[^ \r\n\t@:;\(\)#"] { yylval.txt=strdup(yytext); return TK_STRING; }
%%
