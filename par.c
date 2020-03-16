/*********************/
/* par.c             */
/* for Par 1.41      */
/* Copyright 1993 by */
/* Adam M. Costello  */
/*********************/

/* This is ANSI C code. */


#include "charset.h"   /* Also includes "errmsg.h". */
#include "buffer.h"    /* Also includes <stddef.h>. */
#include "reformat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#undef NULL
#define NULL ((void *) 0)

#ifdef DONTFREE
#define free(ptr)
#endif


static const char * const usagemsg =
"\n"
"Usage:\n"
"\n"
"par [help] [version] [B<op><set>] [P<op><set>] [Q<op><set>] [h[<hang>]]\n"
"    [p[<prefix>]] [r[<repeat>]] [s[<suffix>]] [w[<width>]] [c[<cap>]]\n"
"    [d[<div>]] [E[<Err>]] [e[<expel>]] [f[<fit>]] [g[<guess>]] [i[<invis>]]\n"
"    [j[<just>]] [l[<last>]] [q[<quote>]] [R[<Report>]] [t[<touch>]]\n"
"\n"
"help       print usage message       "
                                 "  ---------- Boolean parameters: ---------\n"
"version    print version number      "
                                 "  Option:   If 1:\n"
"B<op><set> as <op> is =/+/-,         "
                                 "  c<cap>    count all words as capitalized\n"
"           replace/augment/diminish  "
                                 "  d<div>    use indentation as a delimiter\n"
"           body chars by <set>       "
                                 "  E<Err>    send messages to stderr\n"
"P<op><set> ditto for protective chars"
                                 "  e<expel>  discard superfluous lines\n"
"Q<op><set> ditto for quote chars     "
                                 "  f<fit>    narrow paragraph for best fit\n"
"-------- Integer parameters: --------"
                                 "  g<guess>  preserve wide sentence breaks\n"
"h<hang>    skip IP's 1st <hang> lines"
                                 "  i<invis>  hide lines inserted by <quote>\n"
"           in scan for common affixes"
                                 "  j<just>   justify paragraphs\n"
"p<prefix>  prefix length             "
                                 "  l<last>   treat last lines like others\n"
"r<repeat>  if not 0, force bodiless  "
                                 "  q<quote>  supply vacant lines between\n"
"           lines to length <width>   "
                                 "            different quote nesting levels\n"
"s<suffix>  suffix length             "
                                 "  R<Report> print error for too-long words\n"
"w<width>   max output line length    "
                                 "  t<touch>  move suffixes left\n"
"\n"
"See par.doc or par.1 (the man page) for more information.\n"
;


/* Structure for recording properties of lines within segments: */

typedef unsigned char lflag_t;

typedef struct lineprop {
  short p, s;     /* Length of the prefix and suffix of a bodiless */
                  /* line, or the fallback prelen and suflen       */
                  /* of the IP containing a non-bodiless line.     */
  lflag_t flags;  /* Boolean properties (see below).               */
  char rc;        /* The repeated character of a bodiless line.    */
} lineprop;

/* Flags for marking boolean properties: */

static const lflag_t L_BODILESS = 1,  /* Bodiless line.             */
                     L_INVIS    = 2,  /* Invisible line.            */
                     L_FIRST    = 4,  /* First line of a paragraph. */
                     L_SUPERF   = 8;  /* Superfluous line.          */

#define isbodiless(prop) ( (prop)->flags & 1)
#define    isinvis(prop) (((prop)->flags & 2) != 0)
#define    isfirst(prop) (((prop)->flags & 4) != 0)
#define   issuperf(prop) (((prop)->flags & 8) != 0)
#define   isvacant(prop) (isbodiless(prop) && (prop)->rc == ' ')


static int digtoint(char c)

/* Returns the value represented by the digit c, or -1 if c is not a digit. */
{
  const char *p, * const digits = "0123456789";

  if (!c) return -1;
  p = strchr(digits,c);
  return  p  ?  p - digits  :  -1;

  /* We can't simply return c - '0' because this is ANSI C code,  */
  /* so it has to work for any character set, not just ones which */
  /* put the digits together in order.  Also, an array that could */
  /* be referenced as digtoint[c] might be bad because there's no */
  /* upper limit on CHAR_MAX.                                     */
}


static int strtoudec(const char *s, int *pn)

/* Converts the longest prefix of string s consisting of decimal   */
/* digits to an integer, which is stored in *pn.  Normally returns */
/* 1.  If *s is not a digit, then *pn is not changed, but 1 is     */
/* still returned.  If the integer represented is greater than     */
/* 9999, then *pn is not changed and 0 is returned.                */
{
  int n = 0, d;

  d = digtoint(*s);
  if (d < 0) return 1;

  do {
    if (n >= 1000) return 0;
    n = 10 * n + d;
    d = digtoint(*++s);
  } while (d >= 0);

  *pn = n;

  return 1;
}


static void parsearg(
  const char *arg, int *phelp, int *pversion, charset *bodychars,
  charset *protectchars, charset *quotechars, int *phang, int *pprefix,
  int *prepeat, int *psuffix, int *pwidth, int *pcap, int *pdiv, int *pErr,
  int *pexpel, int *pfit, int *pguess, int *pinvis, int *pjust, int *plast,
  int *pquote, int *pReport, int *ptouch, errmsg_t errmsg
)
/* Parses the command line argument in *arg, setting the objects pointed to */
/* by the other pointers as appropriate.  *phelp and *pversion are boolean  */
/* flags indicating whether the help and version options were supplied.     */
{
  const char *savearg = arg;
  charset *chars, *change;
  char oc;
  int n;

  *errmsg = '\0';

  if (*arg == '-') ++arg;

  if (!strcmp(arg, "help")) {
    *phelp = 1;
    return;
  }

  if (!strcmp(arg, "version")) {
    *pversion = 1;
    return;
  }

  if (*arg == 'B' || *arg == 'P' || *arg == 'Q' ) {
    chars =  *arg == 'B'  ?  bodychars    :
             *arg == 'P'  ?  protectchars :
          /* *arg == 'Q' */  quotechars   ;
    ++arg;
    if (*arg != '='  &&  *arg != '+'  &&  *arg != '-') goto badarg;
    change = parsecharset(arg + 1, errmsg);
    if (change) {
      if      (*arg == '=')   csswap(chars,change);
      else if (*arg == '+')   csadd(chars,change,errmsg);
      else  /* *arg == '-' */ csremove(chars,change,errmsg);
      freecharset(change);
    }
    return;
  }

  if (isdigit(*arg)) {
    if (!strtoudec(arg, &n)) goto badarg;
    if (n <= 8) *pprefix = n;
    else *pwidth = n;
  }

  for (;;) {
    while (isdigit(*arg)) ++arg;
    oc = *arg;
    if (!oc) break;
    n = -1;
    if (!strtoudec(++arg, &n)) goto badarg;
    if (oc == 'h' || oc == 'p' || oc == 'r' || oc == 's' || oc == 'w') {
      if      (oc == 'h')   *phang   =  n >= 0 ? n :  1;
      else if (oc == 'w')   *pwidth  =  n >= 0 ? n : 79;
      else if (oc == 'p')   *pprefix =  n;
      else if (oc == 'r')   *prepeat =  n >= 0 ? n :  3;
      else  /* oc == 's' */ *psuffix =  n;
    }
    else {
      if (n < 0) n = 1;
      if (n > 1) goto badarg;
      if      (oc == 'c') *pcap    = n;
      else if (oc == 'd') *pdiv    = n;
      else if (oc == 'E') *pErr    = n;
      else if (oc == 'e') *pexpel  = n;
      else if (oc == 'f') *pfit    = n;
      else if (oc == 'g') *pguess  = n;
      else if (oc == 'i') *pinvis  = n;
      else if (oc == 'j') *pjust   = n;
      else if (oc == 'l') *plast   = n;
      else if (oc == 'q') *pquote  = n;
      else if (oc == 'R') *pReport = n;
      else if (oc == 't') *ptouch  = n;
      else goto badarg;
    }
  }

  return;

badarg:

  sprintf(errmsg, "Bad argument: %.*s\n", errmsg_size - 16, savearg);
  *phelp = 1;
}


static char **readlines(
  lineprop **pprops, const charset *protectchars,
  const charset *quotechars, int invis, int quote, errmsg_t errmsg
)
/* Reads lines from stdin until EOF, or until a line beginning with a   */
/* protective character is encountered (in which case the protective    */
/* character is pushed back onto the input stream), or until a blank    */
/* line is encountered (in which case the newline is pushed back onto   */
/* the input stream).  Returns a NULL-terminated array of pointers to   */
/* individual lines, stripped of their newline characters.  Every NUL   */
/* character is stripped, and every white character is changed to a     */
/* space unless it is a newline.  If quote is 1, vacant lines will be   */
/* supplied as described for the q option in par.doc.  *pprops is set   */
/* to an array of lineprop structures, one for each line, each of whose */
/* flags field is either 0 or L_INVIS (the other fields are 0).  If     */
/* there are no lines, *pprops is set to NULL.  The returned array may  */
/* be freed with freelines().  *pprops may be freed with free() if      */
/* it's not NULL.  On failure, returns NULL and sets *pprops to NULL.   */
{
  buffer *cbuf = NULL, *lbuf = NULL, *lpbuf = NULL;
  int c, empty, blank, firstline, qsonly, oldqsonly = 0, vlnlen;
  char ch, *ln = NULL, nullchar = '\0', *nullline = NULL, *qpend,
       *oldln = NULL, *oldqpend = NULL, *p, *op, *vln = NULL, **lines = NULL;
  lineprop vprop = { 0, 0, 0, '\0' }, iprop = { 0, 0, 0, '\0' };

  /* oldqsonly, oldln, and oldquend don't really need to be initialized.   */
  /* They are initialized only to appease compilers that try to be helpful */
  /* by issuing warnings about unitialized automatic variables.            */

  iprop.flags = L_INVIS;
  *errmsg = '\0';

  *pprops = NULL;

  cbuf = newbuffer(sizeof (char), errmsg);
  if (*errmsg) goto rlcleanup;
  lbuf = newbuffer(sizeof (char *), errmsg);
  if (*errmsg) goto rlcleanup;
  lpbuf = newbuffer(sizeof (lineprop), errmsg);
  if (*errmsg) goto rlcleanup;

  for (empty = blank = firstline = 1;  ; ) {
    c = getchar();
    if (c == EOF) break;
    if (c == '\n') {
      if (blank) {
        ungetc(c,stdin);
        break;
      }
      additem(cbuf, &nullchar, errmsg);
      if (*errmsg) goto rlcleanup;
      ln = copyitems(cbuf,errmsg);
      if (*errmsg) goto rlcleanup;
      if (quote) {
        for (qpend = ln;
             *qpend && csmember(*qpend, quotechars);
             ++qpend);
        for (p = qpend;  *p == ' ' || csmember(*p, quotechars);  ++p);
        qsonly =  *p == '\0';
        while (qpend > ln && qpend[-1] == ' ') --qpend;
        if (!firstline) {
          for (p = ln, op = oldln;
               p < qpend && op < oldqpend && *p == *op;
               ++p, ++op);
          if (!(p == qpend && op == oldqpend))
            if (!invis && (oldqsonly || qsonly)) {
              if (oldqsonly) {
                *op = '\0';
                oldqpend = op;
              }
              if (qsonly) {
                *p = '\0';
                qpend = p;
              }
            }
            else {
              vlnlen = p - ln;
              vln = malloc((vlnlen + 1) * sizeof (char));
              if (!vln) {
                strcpy(errmsg,outofmem);
                goto rlcleanup;
              }
              strncpy(vln,ln,vlnlen);
              vln[vlnlen] = '\0';
              additem(lbuf, &vln, errmsg);
              if (*errmsg) goto rlcleanup;
              additem(lpbuf,  invis ? &iprop : &vprop,  errmsg);
              if (*errmsg) goto rlcleanup;
              vln = NULL;
            }
        }
        oldln = ln;
        oldqpend = qpend;
        oldqsonly = qsonly;
      }
      additem(lbuf, &ln, errmsg);
      if (*errmsg) goto rlcleanup;
      ln = NULL;
      additem(lpbuf, &vprop, errmsg);
      if (*errmsg) goto rlcleanup;
      clearbuffer(cbuf);
      empty = blank = 1;
      firstline = 0;
    }
    else {
      if (empty) {
        if (csmember((char) c, protectchars)) {
          ungetc(c,stdin);
          break;
        }
        empty = 0;
      }
      if (!c) continue;
      if (isspace(c)) c = ' ';
      else blank = 0;
      ch = c;
      additem(cbuf, &ch, errmsg);
      if (*errmsg) goto rlcleanup;
    }
  }

  if (!blank) {
    additem(cbuf, &nullchar, errmsg);
    if (*errmsg) goto rlcleanup;
    ln = copyitems(cbuf,errmsg);
    if (*errmsg) goto rlcleanup;
    additem(lbuf, &ln, errmsg);
    if (*errmsg) goto rlcleanup;
    ln = NULL;
    additem(lpbuf, &vprop, errmsg);
    if (*errmsg) goto rlcleanup;
  }

  additem(lbuf, &nullline, errmsg);
  if (*errmsg) goto rlcleanup;
  *pprops = copyitems(lpbuf,errmsg);
  if (*errmsg) goto rlcleanup;
  lines = copyitems(lbuf,errmsg);

rlcleanup:

  if (cbuf) freebuffer(cbuf);
  if (lpbuf) freebuffer(lpbuf);
  if (lbuf) {
    if (!lines)
      for (;;) {
        lines = nextitem(lbuf);
        if (!lines) break;
        free(*lines);
      }
    freebuffer(lbuf);
  }
  if (ln) free(ln);
  if (vln) free(vln);

  return lines;
}


static void compresuflen(
  const char * const *lines, const char * const *endline,
  const charset *bodychars, int pre, int suf, int *ppre, int *psuf
)
/* lines is an array of strings, up to but not including endline.  */
/* Writes into *ppre and *psuf the comprelen and comsuflen of the  */
/* lines in lines.  Assumes that they have already been determined */
/* to be at least pre and suf.  endline must not equal lines.      */
{
  const char *start, *end, * const *line, *p1, *p2, *start2;

  start = *lines;
  for (end = start + pre;  *end && !csmember(*end, bodychars);  ++end);
  for (line = lines + 1;  line < endline;  ++line) {
    for (p1 = start + pre, p2 = *line + pre;
         p1 < end && *p1 == *p2;
         ++p1, ++p2);
    end = p1;
  }
  *ppre = end - start;

  start2 = *lines + *ppre;
  for (end = start2;  *end;  ++end);
  for (start = end - suf;
       start > start2 && !csmember(start[-1], bodychars);
       --start);
  for (line = lines + 1;  line < endline;  ++line) {
    start2 = *line + *ppre;
    for (p2 = start2;  *p2;  ++p2);
    for (p1 = end - suf, p2 -= suf;
         p1 > start && p2 > start2 && p1[-1] == p2[-1];
         --p1, --p2);
    start = p1;
  }
  while (end - start >= 2 && *start == ' ' && start[1] == ' ') ++start;
  *psuf = end - start;
}


static void delimit(
  const char * const *lines, const char * const *endline,
  const charset *bodychars, int repeat, int div,
  int pre, int suf, lineprop *props
)
/* lines is an array of strings, up to but not including     */
/* endline.  Sets fields in each lineprop in the parallel    */
/* array props as appropriate, except for the L_SUPERF flag, */
/* which is never set.  It is assumed that the comprelen     */
/* and comsuflen of the lines in lines have already been     */
/* determined to be at least pre and suf, respectively.      */
{
  const char * const *line, *end, *p, * const *nextline;
  char rc;
  lineprop *prop, *nextprop;
  int anybodiless = 0, status;

  if (endline == lines) return;

  if (endline == lines + 1) {
    props->flags |= L_FIRST;
    props->p = pre, props->s = suf;
    return;
  }

  compresuflen(lines, endline, bodychars, pre, suf, &pre, &suf);

  line = lines, prop = props;
  do {
    prop->flags |= L_BODILESS;
    prop->p = pre, prop->s = suf;
    for (end = *line;  *end;  ++end);
    end -= suf;
    p = *line + pre;
    rc =  p < end  ?  *p  :  ' ';
    if (rc != ' ' && (!repeat || end - p < repeat))
      prop->flags &= ~L_BODILESS;
    else
      while (p < end) {
        if (*p != rc) {
          prop->flags &= ~L_BODILESS;
          break;
        }
        ++p;
      }
    if (isbodiless(prop)) {
      anybodiless = 1;
      prop->rc = rc;
    }
    ++line, ++prop;
  } while (line < endline);

  if (anybodiless) {
    line = lines, prop = props;
    do {
      if (isbodiless(prop)) {
        ++line, ++prop;
        continue;
      }

      for (nextline = line + 1, nextprop = prop + 1;
           nextline < endline && !isbodiless(nextprop);
           ++nextline, ++nextprop);

      delimit(line,nextline,bodychars,repeat,div,pre,suf,prop);

      line = nextline, prop = nextprop;
    } while (line < endline);

    return;
  }

  if (!div) {
    props->flags |= L_FIRST;
    return;
  }

  line = lines, prop = props;
  status = ((*lines)[pre] == ' ');
  do {
    if (((*line)[pre] == ' ') == status)
      prop->flags |= L_FIRST;
    ++line, ++prop;
  } while (line < endline);
}


static void marksuperf(
  const char * const * lines, const char * const * endline, lineprop *props
)
/* lines points to the first line of a segment, and endline to one  */
/* line beyond the last line in the segment.  Sets L_SUPERF bits in */
/* the flags fields of the props array whenever the corresponding   */
/* line is superfluous.  L_BODILESS bits must already be set.       */
{
  const char * const *line, *p;
  lineprop *prop, *mprop, dummy;
  int inbody, num, mnum;

  for (line = lines, prop = props;  line < endline;  ++line, ++prop)
    if (isvacant(prop))
      prop->flags |= L_SUPERF;

  inbody = mnum = 0;
  mprop = &dummy;
  for (line = lines, prop = props;  line < endline;  ++line, ++prop)
    if (isvacant(prop)) {
      for (num = 0, p = *line;  *p;  ++p)
        if (*p != ' ') ++num;
      if (inbody || num < mnum)
        mnum = num, mprop = prop;
      inbody = 0;
    } else {
      if (!inbody) mprop->flags &= ~L_SUPERF;
      inbody = 1;
    }
} 


static void setaffixes(
  const char * const *inlines, const char * const *endline,
  const lineprop *props, const charset *bodychars, const charset *quotechars,
  int hang, int quote, int *pafp, int *pfs, int *pprefix, int *psuffix
)
/* inlines is an array of strings, up to but not including endline,    */
/* representing an IP.  inlines and endline must not be equal.  props  */
/* is the the parallel array of lineprop structures.  *pafp and *pfs   */
/* are set to the augmented fallback prelen and fallback suflen of the */
/* IP.  If either of *pprefix, *psuffix is less than 0, it is set to a */
/* default value as specified in "par.doc".                            */
{
  int numin, pre, suf;
  const char *p;

  numin = endline - inlines;

  if ((*pprefix < 0 || *psuffix < 0)  &&  numin > hang + 1)
    compresuflen(inlines + hang, endline, bodychars, 0, 0, &pre, &suf);

  p = *inlines + props->p;
  if (numin == 1 && quote)
    while (*p && csmember (*p, quotechars))
      ++p;
  *pafp = p - *inlines;
  *pfs = props->s;

  if (*pprefix < 0)
    *pprefix  =  numin > hang + 1  ?  pre  :  *pafp;

  if (*psuffix < 0)
    *psuffix  =  numin > hang + 1  ?  suf  :  *pfs;
}


static void freelines(char **lines)
/* Frees the elements of lines, and lines itself. */
/* lines is a NULL-terminated array of strings.   */
{
  char **line;

  for (line = lines;  *line;  ++line)
    free(*line);

  free(lines);
}


int main(int argc, const char * const *argv)
{
  int help = 0, version = 0, hang = 0, prefix = -1, repeat = 0, suffix = -1,
      width = 72, cap = 0, div = 0, Err = 0, expel = 0, fit = 0, guess = 0,
      invis = 0, just = 0, last = 0, quote = 0, Report = 0, touch = -1,
      prefixbak, suffixbak, c, sawnonblank, oweblank, n, i, afp, fs;
  charset *bodychars = NULL, *protectchars = NULL, *quotechars = NULL;
  char *parinit = NULL, *arg, **inlines = NULL, **endline, **firstline, *end,
       **nextline, **outlines = NULL, **line;
  const char *env, * const whitechars = " \f\n\r\t\v";
  errmsg_t errmsg = { '\0' };
  lineprop *props = NULL, *firstprop, *nextprop;
  FILE *errout;

/* Process environment variables: */

  env = getenv("PARBODY");
  if (!env) env = "";
  bodychars = parsecharset(env,errmsg);
  if (*errmsg) {
    help = 1;
    goto parcleanup;
  }

  env = getenv("PARPROTECT");
  if (!env) env = "";
  protectchars = parsecharset(env,errmsg);
  if (*errmsg) {
    help = 1;
    goto parcleanup;
  }

  env = getenv("PARQUOTE");
  if (!env) env = "> ";
  quotechars = parsecharset(env,errmsg);
  if (*errmsg) {
    help = 1;
    goto parcleanup;
  }

  env = getenv("PARINIT");
  if (env) {
    parinit = malloc((strlen(env) + 1) * sizeof (char));
    if (!parinit) {
      strcpy(errmsg,outofmem);
      goto parcleanup;
    }
    strcpy(parinit,env);
    arg = strtok(parinit,whitechars);
    while (arg) {
      parsearg(arg, &help, &version, bodychars, protectchars,
               quotechars, &hang, &prefix, &repeat, &suffix,
               &width, &cap, &div, &Err, &expel, &fit, &guess,
               &invis, &just, &last, &quote, &Report, &touch, errmsg);
      if (*errmsg || help || version) goto parcleanup;
      arg = strtok(NULL,whitechars);
    }
    free(parinit);
    parinit = NULL;
  }

/* Process command line arguments: */

  while (*++argv) {
    parsearg(*argv, &help, &version, bodychars, protectchars,
             quotechars, &hang, &prefix, &repeat, &suffix,
             &width, &cap, &div, &Err, &expel, &fit, &guess,
             &invis, &just, &last, &quote, &Report, &touch, errmsg);
    if (*errmsg || help || version) goto parcleanup;
  }

  if (touch < 0) touch = fit || last;
  prefixbak = prefix;
  suffixbak = suffix;

/* Main loop: */

  for (sawnonblank = 0, oweblank = 0; ; ) {
    for (;;) {
      c = getchar();
      if (expel && c == '\n') {
        oweblank = sawnonblank;
        continue;
      }
      if (csmember((char) c, protectchars)) {
        sawnonblank = 1;
        if (oweblank) {
          putchar('\n');
          oweblank = 0;
        }
        while (c != '\n' && c != EOF) {
          putchar(c);
          c = getchar();
        }
      }
      if (c != '\n') break;
      putchar(c);
    }
    if (c == EOF) break;
    ungetc(c,stdin);

    inlines =
      readlines(&props, protectchars, quotechars, invis, quote, errmsg);
    if (*errmsg) goto parcleanup;

    for (endline = inlines;  *endline;  ++endline);
    if (endline == inlines) {
      free(inlines);
      inlines = NULL;
      continue;
    }

    sawnonblank = 1;
    if (oweblank) {
      putchar('\n');
      oweblank = 0;
    }

    delimit((const char * const *) inlines,
            (const char * const *) endline,
            bodychars, repeat, div, 0, 0, props);

    if (expel)
      marksuperf((const char * const *) inlines,
                 (const char * const *) endline, props);

    firstline = inlines, firstprop = props;
    do {
      if (isbodiless(firstprop)) {
        if (!isinvis(firstprop) && !(expel && issuperf(firstprop))) {
          for (end = *firstline;  *end;  ++end);
          if (!repeat  ||  firstprop->rc == ' ' && !firstprop->s) {
            while (end > *firstline && end[-1] == ' ') --end;
            *end = '\0';
            puts(*firstline);
          }
          else {
            n = width - firstprop->p - firstprop->s;
            if (n < 0) {
              sprintf(errmsg,impossibility,5);
              goto parcleanup;
            }
            printf("%.*s", firstprop->p, *firstline);
            for (i = n;  i;  --i)
              putchar(firstprop->rc);
            puts(end - firstprop->s);
          }
        }
        ++firstline, ++firstprop;
        continue;
      }

      for (nextline = firstline + 1, nextprop = firstprop + 1;
           nextline < endline && !isbodiless(nextprop) && !isfirst(nextprop);
           ++nextline, ++nextprop);

      prefix = prefixbak, suffix = suffixbak;
      setaffixes((const char * const *) firstline,
                 (const char * const *) nextline, firstprop, bodychars,
                 quotechars, hang, quote, &afp, &fs, &prefix, &suffix);
      if (width <= prefix + suffix) {
        sprintf(errmsg,
                "<width> (%d) <= <prefix> (%d) + <suffix> (%d)\n",
                width, prefix, suffix);
        goto parcleanup;
      }

      outlines =
        reformat((const char * const *) firstline,
                 (const char * const *) nextline,
                 afp, fs, hang, prefix, suffix, width, cap,
                 fit, guess, just, last, Report, touch, errmsg);
      if (*errmsg) goto parcleanup;

      for (line = outlines;  *line;  ++line)
        puts(*line);

      freelines(outlines);
      outlines = NULL;

      firstline = nextline, firstprop = nextprop;
    } while (firstline < endline);

    freelines(inlines);
    inlines = NULL;

    free(props);
    props = NULL;
  }

parcleanup:

  if (bodychars) freecharset(bodychars);
  if (protectchars) freecharset(protectchars);
  if (quotechars) freecharset(quotechars);
  if (parinit) free(parinit);
  if (inlines) freelines(inlines);
  if (props) free(props);
  if (outlines) freelines(outlines);

  errout = Err ? stderr : stdout;
  if (*errmsg) fprintf(errout, "par error:\n%.*s", errmsg_size, errmsg);
  if (version) fputs("par 1.41\n",errout);
  if (help)    fputs(usagemsg,errout);

  return *errmsg ? EXIT_FAILURE : EXIT_SUCCESS;
}
