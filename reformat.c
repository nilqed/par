/*
reformat.c
last touched in Par 1.53.0
last meaningful change in Par 1.53.0
Copyright 1993, 2001, 2020 Adam M. Costello

This is ANSI C code (C89).

The issues regarding char and unsigned char are relevant to the use of
the ctype.h functions.  See the comments near the beginning of par.c.

*/


#include "reformat.h"  /* Makes sure we're consistent with the prototype. */

#include "buffer.h"
#include "charset.h"
#include "errmsg.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef NULL
#define NULL ((void *) 0)

#ifdef DONTFREE
#define free(ptr)
#endif


typedef unsigned char wflag_t;

typedef struct word {
  const char *chrs;       /* Pointer to the characters in the word */
                          /* (NOT terminated by '\0').             */
  struct word *prev,      /* Pointer to previous word.             */
              *next,      /* Pointer to next word.                 */
                          /* Supposing this word were the first... */
              *nextline;  /*   Pointer to first word in next line. */
  int score,              /*   Value of the objective function.    */
      length;             /* Length of this word.                  */
  wflag_t flags;          /* Notable properties of this word.      */
} word;

/* The following may be bitwise-OR'd together */
/* to set the flags field of a word:          */

static const wflag_t
  W_SHIFTED = 1,  /* This word should have an extra space before */
                  /* it unless it's the first word in the line.  */
  W_CURIOUS = 2,  /* This is a curious word (see par.doc).       */
  W_CAPITAL = 4;  /* This is a capitalized word (see par.doc).   */

#define isshifted(w) ( (w)->flags & 1)
#define iscurious(w) (((w)->flags & 2) != 0)
#define iscapital(w) (((w)->flags & 4) != 0)


static int checkcapital(word *w)
/* Returns 1 if *w is capitalized according to the definition */
/* in par.doc (assuming <cap> is 0), or 0 if not.             */
{
  const char *p, *end;

  for (p = w->chrs, end = p + w->length;
       p < end && !isalnum(*(unsigned char *)p);
       ++p);
  return p < end && !islower(*(unsigned char *)p);
}


static int checkcurious(word *w, const charset *terminalchars)
/* Returns 1 if *w is curious according to */
/* the definition in par.doc, or 0 if not. */
{
  const char *start, *p;
  char ch;

  for (start = w->chrs, p = start + w->length;  p > start;  --p) {
    ch = p[-1];
    if (isalnum(*(unsigned char *)&ch)) return 0;
    if (csmember(ch,terminalchars)) break;
  }

  if (p <= start + 1) return 0;

  --p;
  do if (isalnum(*(unsigned char *)--p)) return 1;
  while (p > start);

  return 0;
}


static int simplebreaks(word *head, word *tail, int L, int last)

/* Chooses line breaks in a list of words which maximize the length of the   */
/* shortest line.  L is the maximum line length.  The last line counts as a  */
/* line only if last is non-zero. _head must point to a dummy word, and tail */
/* must point to the last word, whose next field must be NULL.  Returns the  */
/* length of the shortest line on success, -1 if there is a word of length   */
/* greater than L, or L if there are no lines.                               */
{
  word *w1, *w2;
  int linelen, score;

  if (!head->next) return L;

  for (w1 = tail, linelen = w1->length;
       w1 != head && linelen <= L;
       linelen += isshifted(w1), w1 = w1->prev, linelen += 1 + w1->length) {
    w1->score = last ? linelen : L;
    w1->nextline = NULL;
  }

  for ( ;  w1 != head;  w1 = w1->prev) {
    w1->score = -1;
    for (linelen = w1->length,  w2 = w1->next;
         linelen <= L;
         linelen += 1 + isshifted(w2) + w2->length,  w2 = w2->next) {
      score = w2->score;
      if (linelen < score) score = linelen;
      if (score >= w1->score) {
        w1->nextline = w2;
        w1->score = score;
      }
    }
  }

  return head->next->score;
}


static void normalbreaks(
  word *head, word *tail, int L, int fit, int last, errmsg_t errmsg
)
/* Chooses line breaks in a list of words according to the policy   */
/* in "par.doc" for <just> = 0 (L is <L>, fit is <fit>, and last is */
/* <last>).  head must point to a dummy word, and tail must point   */
/* to the last word, whose next field must be NULL.                 */
{
  word *w1, *w2;
  int tryL, shortest, score, target, linelen, extra, minlen;

  *errmsg = '\0';
  if (!head->next) return;

  target = L;

/* Determine minimum possible difference between  */
/* the lengths of the shortest and longest lines: */

  if (fit) {
    score = L + 1;
    for (tryL = L;  ;  --tryL) {
      shortest = simplebreaks(head,tail,tryL,last);
      if (shortest < 0) break;
      if (tryL - shortest < score) {
        target = tryL;
        score = target - shortest;
      }
    }
  }

/* Determine maximum possible length of the shortest line: */

  shortest = simplebreaks(head,tail,target,last);
  if (shortest < 0) {
    sprintf(errmsg,impossibility,1);
    return;
  }

/* Minimize the sum of the squares of the differences */
/* between target and the lengths of the lines:       */

  w1 = tail;
  do {
    w1->score = -1;
    for (linelen = w1->length,  w2 = w1->next;
         linelen <= target;
         linelen += 1 + isshifted(w2) + w2->length,  w2 = w2->next) {
      extra = target - linelen;
      minlen = shortest;
      if (w2)
        score = w2->score;
      else {
        score = 0;
        if (!last) extra = minlen = 0;
      }
      if (linelen >= minlen  &&  score >= 0) {
        score += extra * extra;
        if (w1->score < 0  ||  score <= w1->score) {
          w1->nextline = w2;
          w1->score = score;
        }
      }
      if (!w2) break;
    }
    w1 = w1->prev;
  } while (w1 != head);

  if (head->next->score < 0)
    sprintf(errmsg,impossibility,2);
}


static void justbreaks(
  word *head, word *tail, int L, int last, errmsg_t errmsg
)
/* Chooses line breaks in a list of words according to the  */
/* policy in "par.doc" for <just> = 1 (L is <L> and last is */
/* <last>).  head must point to a dummy word, and tail must */
/* point to the last word, whose next field must be NULL.   */
{
  word *w1, *w2;
  int numgaps, extra, score, gap, maxgap, numbiggaps;

  *errmsg = '\0';
  if (!head->next) return;

/* Determine the minimum possible largest inter-word gap: */

  w1 = tail;
  do {
    w1->score = L;
    for (numgaps = 0, extra = L - w1->length, w2 = w1->next;
         extra >= 0;
         ++numgaps, extra -= 1 + isshifted(w2) + w2->length, w2 = w2->next) {
      gap = numgaps ? (extra + numgaps - 1) / numgaps : L;
      if (w2)
        score = w2->score;
      else {
        score = 0;
        if (!last) gap = 0;
      }
      if (gap > score) score = gap;
      if (score < w1->score) {
        w1->nextline = w2;
        w1->score = score;
      }
      if (!w2) break;
    }
    w1 = w1->prev;
  } while (w1 != head);

  maxgap = head->next->score;
  if (maxgap >= L) {
    strcpy(errmsg, "Cannot justify.\n");
    return;
  }

/* Minimize the sum of the squares of the numbers   */
/* of extra spaces required in each inter-word gap: */

  w1 = tail;
  do {
    w1->score = -1;
    for (numgaps = 0, extra = L - w1->length, w2 = w1->next;
         extra >= 0;
         ++numgaps, extra -= 1 + isshifted(w2) + w2->length, w2 = w2->next) {
      gap = numgaps ? (extra + numgaps - 1) / numgaps : L;
      if (w2)
        score = w2->score;
      else {
        if (!last) {
          w1->nextline = NULL;
          w1->score = 0;
          break;
        }
        score = 0;
      }
      if (gap <= maxgap && score >= 0) {
        numbiggaps = extra % numgaps;
        score += (extra / numgaps) * (extra + numbiggaps) + numbiggaps;
        /* The above may not look like the sum of the squares of the numbers */
        /* of extra spaces required in each inter-word gap, but trust me, it */
        /* is.  It's easier to prove graphically than algebraicly.           */
        if (w1->score < 0  ||  score <= w1->score) {
          w1->nextline = w2;
          w1->score = score;
        }
      }
      if (!w2) break;
    }
    w1 = w1->prev;
  } while (w1 != head);

  if (head->next->score < 0)
    sprintf(errmsg,impossibility,3);
}


char **reformat(
  const char * const *inlines, const char * const *endline, int afp, int fs,
  int hang, int prefix, int suffix, int width, int cap, int fit, int guess,
  int just, int last, int Report, int touch, const charset *terminalchars,
  errmsg_t errmsg
)
{
  int numin, affix, L, onfirstword = 1, linelen, numout, numgaps, extra, phase;
  const char * const *line, **suffixes = NULL, **suf, *end, *p1, *p2;
  char *q1, *q2, **outlines = NULL;
  word dummy, *head, *tail, *w1, *w2;
  buffer *pbuf = NULL;

/* Initialization: */

  *errmsg = '\0';
  dummy.next = dummy.prev = NULL;
  dummy.flags = 0;
  head = tail = &dummy;
  numin = endline - inlines;
  if (numin <= 0) {
    sprintf(errmsg,impossibility,4);
    goto rfcleanup;
  }
  numgaps = extra = 0;  /* unnecessary, but quiets compiler warnings */

/* Allocate space for pointers to the suffixes: */

  suffixes = malloc(numin * sizeof (const char *));
  if (!suffixes) {
    strcpy(errmsg,outofmem);
    goto rfcleanup;
  }

/* Set the pointers to the suffixes, and create the words: */

  affix = prefix + suffix;
  L = width - prefix - suffix;

  line = inlines, suf = suffixes;
  do {
    for (end = *line;  *end;  ++end);
    if (end - *line < affix) {
      sprintf(errmsg,
              "Line %ld shorter than <prefix> + <suffix> = %d + %d = %d\n",
              (long)(line - inlines + 1), prefix, suffix, affix);
      goto rfcleanup;
    }
    end -= suffix;
    *suf = end;
    p1 = *line + prefix;
    for (;;) {
      while (p1 < end && *p1 == ' ') ++p1;
      if (p1 == end) break;
      p2 = p1;
      if (onfirstword) {
        p1 = *line + prefix;
        onfirstword = 0;
      }
      while (p2 < end && *p2 != ' ') ++p2;
      w1 = malloc(sizeof (word));
      if (!w1) {
        strcpy(errmsg,outofmem);
        goto rfcleanup;
      }
      w1->next = NULL;
      w1->prev = tail;
      tail = tail->next = w1;
      w1->chrs = p1;
      w1->length = p2 - p1;
      w1->flags = 0;
      p1 = p2;
    }
    ++line, ++suf;
  } while (line < endline);

/* If guess is 1, set flag values and merge words: */

  if (guess) {
    for (w1 = head, w2 = head->next;  w2;  w1 = w2, w2 = w2->next) {
      if (checkcurious(w2,terminalchars)) w2->flags |= W_CURIOUS;
      if (cap || checkcapital(w2)) {
        w2->flags |= W_CAPITAL;
        if (iscurious(w1)) {
          if (w1->chrs[w1->length] && w1->chrs + w1->length + 1 == w2->chrs) {
            w2->length += w1->length + 1;
            w2->chrs = w1->chrs;
            w2->prev = w1->prev;
            w2->prev->next = w2;
            if (iscapital(w1)) w2->flags |= W_CAPITAL;
            else w2->flags &= ~W_CAPITAL;
            if (isshifted(w1)) w2->flags |= W_SHIFTED;
            else w2->flags &= ~W_SHIFTED;
            free(w1);
          }
          else w2->flags |= W_SHIFTED;
        }
      }
    }
    tail = w1;
  }

/* Check for too-long words: */

  if (Report)
    for (w2 = head->next;  w2;  w2 = w2->next) {
      if (w2->length > L) {
        linelen = w2->length;
        if (linelen > errmsg_size - 17)
          linelen = errmsg_size - 17;
        sprintf(errmsg, "Word too long: %.*s\n", linelen, w2->chrs);
        goto rfcleanup;
      }
    }
  else
    for (w2 = head->next;  w2;  w2 = w2->next)
      while (w2->length > L) {
        w1 = malloc(sizeof (word));
        if (!w1) {
          strcpy(errmsg,outofmem);
          goto rfcleanup;
        }
        w1->next = w2;
        w1->prev = w2->prev;
        w1->prev->next = w1;
        w2->prev = w1;
        w1->chrs = w2->chrs;
        w2->chrs += L;
        w1->length = L;
        w2->length -= L;
        w1->flags = 0;
        if (iscapital(w2)) {
          w1->flags |= W_CAPITAL;
          w2->flags &= ~W_CAPITAL;
        }
        if (isshifted(w2)) {
          w1->flags |= W_SHIFTED;
          w2->flags &= ~W_SHIFTED;
        }
      }

/* Choose line breaks according to policy in "par.doc": */

  if (just) justbreaks(head,tail,L,last,errmsg);
  else normalbreaks(head,tail,L,fit,last,errmsg);
  if (*errmsg) goto rfcleanup;

/* Change L to the length of the longest line if required: */

  if (!just && touch) {
    L = 0;
    w1 = head->next;
    while (w1) {
      for (linelen = w1->length, w2 = w1->next;
           w2 != w1->nextline;
           linelen += 1 + isshifted(w2) + w2->length, w2 = w2->next);
      if (linelen > L) L = linelen;
      w1 = w2;
    }
  }

/* Construct the lines: */

  pbuf = newbuffer(sizeof (char *), errmsg);
  if (*errmsg) goto rfcleanup;

  numout = 0;
  w1 = head->next;
  while (numout < hang || w1) {
    if (w1)
      for (w2 = w1->next, numgaps = 0, extra = L - w1->length;
           w2 != w1->nextline;
           ++numgaps, extra -= 1 + isshifted(w2) + w2->length, w2 = w2->next);
    linelen = suffix || (just && (w2 || last)) ?
                L + affix :
                w1 ? prefix + L - extra : prefix;
    q1 = malloc((linelen + 1) * sizeof (char));
    if (!q1) {
      strcpy(errmsg,outofmem);
      goto rfcleanup;
    }
    additem(pbuf, &q1, errmsg);
    if (*errmsg) goto rfcleanup;
    ++numout;
    q2 = q1 + prefix;
    if      (numout <= numin) memcpy(q1, inlines[numout - 1], prefix);
    else if (numin  >  hang ) memcpy(q1, endline[-1],         prefix);
    else {
      if (afp > prefix) afp = prefix;
      memcpy(q1, endline[-1], afp);
      q1 += afp;
      while (q1 < q2) *q1++ = ' ';
    }
    q1 = q2;
    if (w1) {
      phase = numgaps / 2;
      for (w2 = w1;  ;  ) {
        memcpy(q1, w2->chrs, w2->length);
        q1 += w2->length;
        w2 = w2->next;
        if (w2 == w1->nextline) break;
        *q1++ = ' ';
        if (just && (w1->nextline || last)) {
          phase += extra;
          while (phase >= numgaps) {
            *q1++ = ' ';
            phase -= numgaps;
          }
        }
        if (isshifted(w2)) *q1++ = ' ';
      }
    }
    q2 += linelen - affix;
    while (q1 < q2) *q1++ = ' ';
    q2 = q1 + suffix;
    if      (numout <= numin) memcpy(q1, suffixes[numout - 1], suffix);
    else if (numin  >  hang ) memcpy(q1, suffixes[numin  - 1], suffix);
    else {
      if (fs > suffix) fs = suffix;
      memcpy(q1, suffixes[numin - 1], fs);
      q1 += fs;
      while(q1 < q2) *q1++ = ' ';
    }
    *q2 = '\0';
    if (w1) w1 = w1->nextline;
  }

  q1 = NULL;
  additem(pbuf, &q1, errmsg);
  if (*errmsg) goto rfcleanup;

  outlines = copyitems(pbuf,errmsg);

rfcleanup:

  if (suffixes) free(suffixes);

  while (tail != head) {
    tail = tail->prev;
    free(tail->next);
  }

  if (pbuf) {
    if (!outlines)
      for (;;) {
        outlines = nextitem(pbuf);
        if (!outlines) break;
        free(*outlines);
      }
    freebuffer(pbuf);
  }

  return outlines;
}
