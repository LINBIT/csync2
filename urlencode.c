#include "csync2.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#define RINGBUFF_LEN 10

static char *ringbuff[RINGBUFF_LEN];
int ringbuff_counter = 0;

// perl -e 'printf("\\%03o", $_) for(1..040)' | fold -64; echo
static char badchars[] =
	"\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020"
	"\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037\040"
	"\177\"'%$:|";

const char *url_encode(const char * in)
{
	char *out;
	int i, j, k, len;

	for (i=len=0; in[i]; i++, len++)
		for (j=0; badchars[j]; j++)
			if ( in[i] == badchars[j] ) { len+=2; break; }

	out = malloc(len + 1);

	for (i=k=0; in[i]; i++) {
		for (j=0; badchars[j]; j++)
			if ( in[i] == badchars[j] ) break;
		if ( badchars[j] ) {
			snprintf(out+k, 4, "%%%02X", in[i]);
			k += 3;
		} else
			out[k++] = in[i];
	}
	assert(k==len);
	out[k] = 0;

	if ( ringbuff[ringbuff_counter] )
		free(ringbuff[ringbuff_counter]);
	ringbuff[ringbuff_counter++] = out;
	if ( ringbuff_counter == RINGBUFF_LEN ) ringbuff_counter=0;

	return out;
}

const char *url_decode(const char * in)
{
	char *out, num[3]="XX";
	int i, k, len;

	for (i=len=0; in[i]; i++, len++)
		if ( in[i] == '%' && in[i+1] && in[i+2] ) i+=2;

	out = malloc(len + 1);

	for (i=k=0; in[i]; i++)
		if ( in[i] == '%' && in[i+1] && in[i+2] ) {
			num[0] = in[++i];
			num[1] = in[++i];
			out[k++] = strtol(num, 0, 16);
		} else
			out[k++] = in[i];
	assert(k==len);
	out[k] = 0;

	if ( ringbuff[ringbuff_counter] )
		free(ringbuff[ringbuff_counter]);
	ringbuff[ringbuff_counter++] = out;
	if ( ringbuff_counter == RINGBUFF_LEN ) ringbuff_counter=0;

	return out;
}

