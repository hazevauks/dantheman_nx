/* dtm_libc.c -- the few bionic functions libmortargame.so imports that the
 * runtime's shims do not have (tools/gen_imports.py binds them by their b_
 * names): bionic's <math.h> classification helpers, rewind and umask.
 *
 * bionic's fpclassify values are bit flags, newlib's are an enumeration: the
 * engine compares against bionic's, so they are translated. MIT.
 */
#include <math.h>
#include <stdio.h>

#include "bionic.h"

int b_fseek(void *fp, long off, int whence);
void b_clearerr(void *fp);

#define B_FP_INFINITE 0x01
#define B_FP_NAN 0x02
#define B_FP_NORMAL 0x04
#define B_FP_SUBNORMAL 0x08
#define B_FP_ZERO 0x10

int b___fpclassifyd(double d) {
  switch (fpclassify(d)) {
  case FP_INFINITE: return B_FP_INFINITE;
  case FP_NAN: return B_FP_NAN;
  case FP_SUBNORMAL: return B_FP_SUBNORMAL;
  case FP_ZERO: return B_FP_ZERO;
  default: return B_FP_NORMAL;
  }
}

int b___isfinite(double d) { return isfinite(d); }
int b___signbit(double d) { return signbit(d) != 0; }

/* Through the runtime's fseek, which knows the engine's FILE pointers
 * (bionic's stdin / stdout / stderr are not newlib's). */
void b_rewind(void *fp) {
  b_fseek(fp, 0, SEEK_SET);
  b_clearerr(fp);
}

/* Files on the SD card have no permission bits: the mask is kept, as the
 * previous one must be returned. */
b_mode_t b_umask(b_mode_t mask) {
  static b_mode_t current = 022;
  const b_mode_t old = current;
  current = mask & 0777;
  return old;
}
