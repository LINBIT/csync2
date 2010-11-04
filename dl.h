#ifndef DL_H
#define DL_H

#include <dlfcn.h>

#define LOOKUP_SYMBOL(dl_handle, sym) \
	f.sym ## _fn = dlsym(dl_handle, #sym); \
	if ((f.sym ## _fn) == NULL) { \
		csync_fatal ("Could not lookup %s in shared library: %s\n", #sym, dlerror()); \
	}

#endif
