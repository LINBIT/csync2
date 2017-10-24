#include "cap.h"
#include "config.h"

/* Capability list advertised after CONFIG. Empty for now; each new
 * capability adds its own #ifdef block here and a matching CAP_FOO
 * in cap.h. Old peers see "OK (" and accept; new peers parse the
 * +TOKEN list. */
const char *get_caps(void) {
	return "OK ("
		").";
}
