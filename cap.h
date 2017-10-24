#ifndef CAP_H
#define CAP_H

/* CAP_<NAME> bit assignments live here, one per advertised feature.
 * None defined yet; consumers add them alongside the get_caps() and
 * parse-side handling for the new flag. */

#define CAP_HAS(c, x) (!!((x) & CAP_##c))
#define CAP_SET(c, x) ((x) |= CAP_##c)
#define CAP_STR(c)    "+"#c

const char *get_caps(void);
#endif /* CAP_H */
