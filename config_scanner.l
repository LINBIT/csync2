%{
#include "y.tab.h"
%}

%option noyywrap

%%
"{"		{ return TK_BLOCK_BEGIN; }
"}"		{ return TK_BLOCK_END; }
";"		{ return TK_STEND; }

"group"		{ return TK_GROUP; }
"host"		{ return TK_HOST; }
"exclude"	{ return TK_EXCL; }
"include"	{ return TK_INCL; }
"key"		{ return TK_KEY; }

[ \n\t]+	/* whitespaces are just delimiters */
[^ \n\t;]+	{ yylval.txt=strdup(yytext); return TK_STRING; }
%%
