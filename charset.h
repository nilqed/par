/*
charset.h
last touched in Par 1.53.0
last meaningful change in Par 1.31
Copyright 1993 Adam M. Costello

This is ANSI C code (C89).

Note: Those functions declared here which do not use errmsg
always succeed, provided that they are passed valid arguments.

*/


#ifndef CHARSET_H
#define CHARSET_H

#include "errmsg.h"


typedef struct charset charset;


charset *parsecharset(const char *str, errmsg_t errmsg);

  /* parsecharset(str,errmsg) returns the set of characters defined by */
  /* str using charset syntax (see par.doc).  Returns NULL on failure. */


void freecharset(charset *cset);

  /* freecharset(cset) frees any memory associated with */
  /* *cset.  cset may not be used after this call.      */


int csmember(char c, const charset *cset);

  /* csmember(c,cset) returns 1 if c is a member of *cset, 0 otherwise. */


charset *csunion(const charset *cset1, const charset *cset2, errmsg_t errmsg);

  /* csunion(cset1,cset2) returns a pointer to the   */
  /* union of *cset1 and *cset2, or NULL on failure. */


charset *csdiff(const charset *cset1, const charset *cset2, errmsg_t errmsg);

  /* csdiff(cset1,cset2) returns a pointer to the set */
  /* difference *cset1 - *cset2 , or NULL on failure. */


void csadd(charset *cset1, const charset *cset2, errmsg_t errmsg);

  /* csadd(cset1,cset2) adds the members of *cset2  */
  /* to *cset1.  On failure, *cset1 is not changed. */


void csremove(charset *cset1, const charset *cset2, errmsg_t errmsg);

  /* csremove(cset1,cset2) removes the members of *cset2 */
  /* from *cset1.  On failure, *cset1 is not changed.    */


charset *cscopy(const charset *cset, errmsg_t errmsg);

  /* cscopy(cset) returns a copy of cset, or NULL on failure. */


void csswap(charset *cset1, charset *cset2);

  /* csswap(cset1,cset2) swaps the contents of *cset1 and *cset2. */


#endif
