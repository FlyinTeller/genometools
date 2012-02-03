/*
  Copyright (c) 2011 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2011 Center for Bioinformatics, University of Hamburg

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "core/assert_api.h"
#include "core/stack-inlined.h"
#include "core/types_api.h"
#include "core/radix-intsort.h"
#include "core/range_api.h"

#define GT_RADIX_KEY(MASK,SHIFT,VALUE)    (((VALUE) >> (SHIFT)) & (MASK))
#define GT_RADIX_KEY_PTR(MASK,SHIFT,PTR)  GT_RADIX_KEY(MASK,SHIFT,*(PTR))
#define GT_RADIX_KEY_REF(MASK,SHIFT,PTR)  GT_RADIX_KEY(MASK,SHIFT,arr[*(PTR)])
#define GT_RADIX_KEY_PAIR(MASK,SHIFT,PTR) GT_RADIX_KEY(MASK,SHIFT,(PTR)->a)
#define GT_RADIX_KEY_UINT8(SHIFT,PTR)     GT_RADIX_KEY_PTR(UINT8_MAX,SHIFT,PTR)

#ifdef SKDEBUG
static void gt_radix_showbytewise(unsigned long value)
{
  int shift;

  for (shift = GT_INTWORDSIZE - 8; shift >= 0; shift-=8)
  {
    printf("%lu ",GT_RADIX_KEY_UINT8(shift,&value));
    if (shift > 0)
    {
      printf(" ");
    }
  }
}
#endif

/* if sorting for table large than UINT_MAX is required, set the following
   type to unsigned long */

typedef unsigned int Countbasetype;

struct GtRadixsortinfo
{
  Countbasetype *count;
  GtUlong *arr, *temp;
  GtUlongPair *arrpair, *temppair;
  bool ownarr, pair;
  unsigned long maxlen, tempalloc;
  unsigned int parts;
  GtRange *ranges;
  size_t basesize, maxvalue;
  GtRadixreader *radixreader;
};

GtRadixsortinfo *gt_radixsort_new(bool pair,
                                  bool smalltables,
                                  unsigned long maxlen,
                                  unsigned int parts,
                                  void *arr)
{
  GtRadixsortinfo *radixsort = gt_malloc(sizeof (*radixsort));

  gt_assert(maxlen <= (unsigned long) UINT_MAX);
  if (smalltables)
  {
    radixsort->basesize = sizeof (uint8_t);
    radixsort->maxvalue = UINT8_MAX;
  } else
  {
    radixsort->basesize = sizeof (uint16_t);
    radixsort->maxvalue = UINT16_MAX;
  }
  radixsort->pair = pair;
  radixsort->count = gt_malloc(sizeof (*radixsort->count) *
                               (radixsort->maxvalue+1));
  if (arr == NULL)
  {
    if (pair)
    {
      radixsort->arr = NULL;
      radixsort->arrpair = gt_malloc(sizeof (*radixsort->arrpair) * maxlen);
    } else
    {
      radixsort->arr = gt_malloc(sizeof (*radixsort->arr) * maxlen);
      radixsort->arrpair = NULL;
    }
    radixsort->ownarr = true;
  } else
  {
    if (pair)
    {
      radixsort->arr = NULL;
      radixsort->arrpair = (GtUlongPair *) arr;
    } else
    {
      radixsort->arr = (GtUlong *) arr;
      radixsort->arrpair = NULL;
    }
    radixsort->ownarr = false;
  }
  gt_assert(parts >= 1U);
  radixsort->parts = parts;
  if (parts == 1U)
  {
    radixsort->tempalloc = maxlen;
    radixsort->radixreader = NULL;
  } else
  {
    radixsort->tempalloc = maxlen/parts + 1;
    radixsort->radixreader = gt_malloc(sizeof (*radixsort->radixreader));
    if (parts == 2U)
    {
      radixsort->radixreader->ptrtab = NULL;
      radixsort->radixreader->priorityqueue = NULL;
    } else
    {
      radixsort->radixreader->ptrtab
        = gt_malloc(sizeof (*radixsort->radixreader->ptrtab) * parts);
      radixsort->radixreader->priorityqueue
        = gt_priorityqueue_new((unsigned long) radixsort->parts);
    }
  }
  radixsort->ranges = gt_malloc(sizeof(*radixsort->ranges) * parts);
  if (pair)
  {
    radixsort->temp = NULL;
    radixsort->temppair = gt_malloc(sizeof (*radixsort->temppair) *
                                    radixsort->tempalloc);
  } else
  {
    radixsort->temp = gt_malloc(sizeof (*radixsort->temp) *
                                radixsort->tempalloc);
    radixsort->temppair = NULL;
  }
  radixsort->maxlen = maxlen;
  return radixsort;
}

GtUlong *gt_radixsort_arr(GtRadixsortinfo *radixsort)
{
  gt_assert(!radixsort->pair && radixsort->ownarr);
  return radixsort->arr;
}

GtUlongPair *gt_radixsort_arrpair(GtRadixsortinfo *radixsort)
{
  gt_assert(radixsort->pair && radixsort->ownarr);
  return radixsort->arrpair;
}

static size_t gt_radixsort_basicsize(size_t maxvalue)
{
  return sizeof (GtRadixsortinfo) + sizeof (Countbasetype) * maxvalue;
}

static size_t gt_radixsort_elemsize(bool pair)
{
  return pair ? sizeof (GtUlongPair) : sizeof (GtUlong);
}

size_t gt_radixsort_size(const GtRadixsortinfo *radixsort)
{
  size_t elemsize = gt_radixsort_elemsize(radixsort->pair);
  return gt_radixsort_basicsize(radixsort->maxvalue) +
         elemsize * (radixsort->maxlen + radixsort->tempalloc);
}

unsigned long gt_radixsort_entries(bool pair,unsigned int parts,
                                   size_t memlimit)
{
  double factor;

  gt_assert(parts >= 1U);
  /* Note that calculation includes data and temp. The space for temp
     depends on the number of parts. */
  factor = 1.0 + 1.0/(double) parts;
  return (unsigned long) memlimit/(gt_radixsort_elemsize(pair) * factor);
}

void gt_radixsort_delete(GtRadixsortinfo *radixsort)
{
  if (radixsort != NULL)
  {
    gt_free(radixsort->count);
    gt_free(radixsort->temp);
    gt_free(radixsort->temppair);
    gt_free(radixsort->ranges);
    if (radixsort->ownarr)
    {
      gt_free(radixsort->arr);
      gt_free(radixsort->arrpair);
    }
    if (radixsort->radixreader != NULL)
    {
      gt_free(radixsort->radixreader->ptrtab);
      gt_priorityqueue_delete(radixsort->radixreader->priorityqueue);
      gt_free(radixsort->radixreader);
    }
    gt_free(radixsort);
  }
}

static void gt_radixsort_GtUlong_linear_phase(GtRadixsortinfo *radixsort,
                                              GtUlong *source,
                                              GtUlong *dest,
                                              unsigned long len,
                                              size_t shift)
{
  Countbasetype *cptr, idx;
  GtUlong *sptr;
  Countbasetype *countptr;

  /* count occurences of every byte value */
  countptr = radixsort->count;
  for (cptr = countptr; cptr <= countptr + radixsort->maxvalue; cptr++)
  {
    *cptr = 0;
  }
  for (sptr = source; sptr < source + len; sptr++)
  {
    countptr[GT_RADIX_KEY_PTR(radixsort->maxvalue,shift,sptr)]++;
  }

  /* compute partial sums */
  for (cptr = countptr+1; cptr <= countptr + radixsort->maxvalue; cptr++)
  {
    *cptr += *(cptr-1);
  }

  /* fill dest with the right values in the right place */
  for (sptr = source + len - 1; sptr >= source; sptr--)
  {
    idx = --countptr[GT_RADIX_KEY_PTR(radixsort->maxvalue,shift,sptr)];
    dest[idx] = *sptr;
  }
}

static void gt_radixsort_GtUlong_linear(GtRadixsortinfo *radixsort,
                                        unsigned long offset,
                                        unsigned long len)
{
  unsigned int iter;
  GtUlong *source, *dest;

  gt_assert(radixsort != NULL &&
            !radixsort->pair &&
            len <= radixsort->maxlen &&
            radixsort->arr != NULL &&
            radixsort->temp != NULL);
  source = radixsort->arr + offset;
  dest = radixsort->temp;
  for (iter = 0; iter < (unsigned int) (sizeof(unsigned long)/
                                       radixsort->basesize);
       iter++)
  {
    GtUlong *ptr;

    gt_radixsort_GtUlong_linear_phase (radixsort, source, dest, len,
                                       iter * CHAR_BIT *
                                       radixsort->basesize);
    ptr = source;
    source = dest;
    dest = ptr;
  }
}

static void gt_radixsort_GtUlongPair_linear_phase(GtRadixsortinfo *radixsort,
                                                  GtUlongPair *source,
                                                  GtUlongPair *dest,
                                                  unsigned long len,
                                                  size_t shift)
{
  Countbasetype *cptr, idx;
  GtUlongPair *sptr;
  Countbasetype *countptr;

  /* count occurences of every byte value */
  countptr = radixsort->count;
  for (cptr = countptr; cptr <= countptr + radixsort->maxvalue; cptr++)
  {
    *cptr = 0;
  }
  for (sptr = source; sptr < source + len; sptr++)
  {
    countptr[GT_RADIX_KEY_PAIR(radixsort->maxvalue,shift,sptr)]++;
  }

  /* compute partial sums */
  for (cptr = countptr+1; cptr <= countptr + radixsort->maxvalue; cptr++)
  {
    *cptr += *(cptr-1);
  }

  /* fill dest with the right values in the right place */
  for (sptr = source + len - 1; sptr >= source; sptr--)
  {
    idx = --countptr[GT_RADIX_KEY_PAIR(radixsort->maxvalue,shift,sptr)];
    dest[idx] = *sptr;
  }
}

static void gt_radixsort_GtUlongPair_linear(GtRadixsortinfo *radixsort,
                                            unsigned long offset,
                                            unsigned long len)
{
  unsigned int iter;
  GtUlongPair *source, *dest;

  gt_assert(radixsort != NULL &&
            radixsort->pair &&
            len <= radixsort->maxlen &&
            radixsort->arrpair != NULL &&
            radixsort->temppair != NULL);
  source = radixsort->arrpair + offset;
  dest = radixsort->temppair;
  for (iter = 0; iter <(unsigned int) (sizeof(unsigned long)/
                                       radixsort->basesize);
       iter++)
  {
    GtUlongPair *ptr;

    gt_radixsort_GtUlongPair_linear_phase (radixsort, source, dest, len,
                                           iter * CHAR_BIT *
                                           radixsort->basesize);
    ptr = source;
    source = dest;
    dest = ptr;
  }
}

void gt_radixsort_verify(GtRadixreader *rr)
{
  GtUlong current, previous = 0;

  while (true)
  {
    GT_RADIXREADER_NEXT(current,rr,break);
    gt_assert(previous <= current); /* as previous = 0 at init, this also
                                       works for the first case */
    previous = current;
  }
}

void gt_radixsort_linear(GtRadixsortinfo *radixsort,unsigned long len)
{
  gt_assert(radixsort->parts == 1U);
  if (radixsort->pair)
  {
    gt_radixsort_GtUlongPair_linear(radixsort,0,len);
  } else
  {
    gt_radixsort_GtUlong_linear(radixsort,0,len);
  }
}

GtRadixreader *gt_radixsort_linear_rr(GtRadixsortinfo *radixsort,
                                      unsigned long len)
{

  gt_assert(radixsort->parts >= 2U);
  if (radixsort->parts == 2U)
  {
    unsigned long len1 = len/2;
    GtRadixreader *rr;

    gt_assert(len >= len1);
    gt_assert(radixsort->radixreader != NULL);
    rr = radixsort->radixreader;
    if (radixsort->pair)
    {
      gt_radixsort_GtUlongPair_linear(radixsort,0,len1);
      gt_radixsort_GtUlongPair_linear(radixsort,len1,len - len1);
      rr->ptr1_pair = radixsort->arrpair;
      rr->ptr2_pair = rr->end1_pair = radixsort->arrpair + len1;
      rr->end2_pair = radixsort->arrpair + len;
      rr->ptr1 = rr->ptr2 = rr->end1 = rr->end2 = NULL;
    } else
    {
      gt_radixsort_GtUlong_linear(radixsort,0,len1);
      gt_radixsort_GtUlong_linear(radixsort,len1,len - len1);
      rr->ptr1 = radixsort->arr;
      rr->ptr2 = rr->end1 = radixsort->arr + len1;
      rr->end2 = radixsort->arr + len;
      rr->ptr1_pair = rr->ptr2_pair = rr->end1_pair = rr->end2_pair = NULL;
    }
    /*gt_radixsort_verify(radixsort->radixreader);*/
  } else
  {
    unsigned int idx;
    unsigned long sumwidth = 0, width;

    if (len % radixsort->parts == 0)
    {
      width = len/radixsort->parts;
    } else
    {
      width = len/radixsort->parts + 1;
    }
    for (idx = 0; idx < radixsort->parts; idx++)
    {
      if (idx == 0)
      {
        radixsort->ranges[idx].start = 0;
      } else
      {
        radixsort->ranges[idx].start = radixsort->ranges[idx-1].end + 1;
      }
      if (idx < radixsort->parts - 1)
      {
        radixsort->ranges[idx].end = (idx+1) * width - 1;
      } else
      {
        radixsort->ranges[idx].end = len - 1;
      }
      sumwidth += radixsort->ranges[idx].end - radixsort->ranges[idx].start + 1;
    }
    gt_assert(sumwidth == len);
    gt_assert(radixsort->radixreader != NULL);
    for (idx = 0; idx < radixsort->parts; idx++)
    {
      unsigned long currentwidth = radixsort->ranges[idx].end -
                                   radixsort->ranges[idx].start + 1;
      gt_assert (currentwidth <= radixsort->tempalloc);
      if (radixsort->pair)
      {
        gt_assert(false);
      } else
      {
        gt_radixsort_GtUlong_linear(radixsort,radixsort->ranges[idx].start,
                                    currentwidth);
        radixsort->radixreader->ptrtab[idx].currentptr
          = radixsort->arr + radixsort->ranges[idx].start;
        radixsort->radixreader->ptrtab[idx].endptr
          = radixsort->arr + radixsort->ranges[idx].end + 1;
      }
    }
  }
  return radixsort->radixreader;
}

static void gt_radix_phase_GtUlong_recursive(size_t offset,
                                             GtUlong *source,
                                             GtUlong *dest,
                                             unsigned long len)
{
  unsigned long idx, s, c, *sp, *cp;
  const size_t maxoffset = sizeof (unsigned long) - 1;
  unsigned long count[UINT8_MAX+1] = {0};
  const size_t shift = (maxoffset - offset) * CHAR_BIT;

  /* count occurences of every byte value */
  for (sp = source; sp < source+len; sp++)
  {
    count[GT_RADIX_KEY_UINT8(shift,sp)]++;
  }
  /* compute partial sums */
  for (s = 0, cp = count; cp <= count + UINT8_MAX; cp++)
  {
    c = *cp;
    *cp = s;
    s += c;
  }
  /* fill dest with the right values in the right place */
  for (sp = source; sp < source+len; sp++)
  {
    dest[count[GT_RADIX_KEY_UINT8(shift,sp)]++] = *sp;
  }
  memcpy(source,dest,(size_t) sizeof (*source) * len);
  if (offset < maxoffset)
  {
    for (idx = 0; idx <= UINT8_MAX; idx++)
    {
      unsigned long newleft = (idx == 0) ? 0 : count[idx-1];
      /* |newleft .. count[idx]-1| = count[idx]-1-newleft+1
                                   = count[idx]-newleft > 1
      => count[idx] > newleft + 1 */
      if (newleft+1 < count[idx])
      {
        gt_radix_phase_GtUlong_recursive(offset+1,
                                         source+newleft,
                                         dest+newleft,
                                         count[idx]-newleft);
      }
    }
  }
}

void gt_radixsort_GtUlong_recursive(GtUlong *source, GtUlong *dest,
                                    unsigned long len)
{
  gt_radix_phase_GtUlong_recursive(0,source,dest,len);
}

typedef struct
{
  GtUlong *left;
  unsigned long len;
  uint8_t shift;
} GtRadixsort_stackelem;

GT_STACK_DECLARESTRUCT(GtRadixsort_stackelem,512);

static void gt_radixsort_GtUlong_initstack(GtStackGtRadixsort_stackelem *stack,
                                           GtUlong *source,
                                           GtUlong *dest,
                                           unsigned long len)
{
  GtRadixsort_stackelem tmpelem;
  unsigned long idx, s, c, *sp, *cp, newleft, count[UINT16_MAX+1];
  const size_t mask = UINT16_MAX;
#ifdef _LP64
  const size_t shift = (size_t) 48;
#else
  const size_t shift = (size_t) 16;
#endif

  GT_STACK_INIT(stack,64UL);
  for (idx=0; idx<=UINT16_MAX; idx++)
  {
    count[idx] = 0;
  }
  for (sp = source; sp < source + len; sp++)
  {
    count[GT_RADIX_KEY_PTR(mask,shift,sp)]++;
  }
  for (s = 0, cp = count; cp <= count + UINT16_MAX; cp++)
  {
    c = *cp;
    *cp = s;
    s += c;
  }
  /* fill dest with the right values in the right place */
  for (sp = source; sp < source + len; sp++)
  {
    dest[count[GT_RADIX_KEY_PTR(mask,shift,sp)]++] = *sp;
  }
  memcpy(source,dest,(size_t) sizeof (*source) * len);
  for (idx = 0; idx <= UINT16_MAX; idx++)
  {
    newleft = (idx == 0) ? 0 : count[idx-1];
    /* |newleft .. count[idx]-1| = count[idx]-1-newleft+1
                                 = count[idx]-newleft > 1
        => count[idx] > newleft + 1
    */
    if (newleft+1 < count[idx])
    {
#ifdef _LP64
      tmpelem.shift = (uint8_t) 40;
#else
      tmpelem.shift = (uint8_t) 8;
#endif
      tmpelem.left = source + newleft;
      tmpelem.len = count[idx] - newleft;
      GT_STACK_PUSH(stack,tmpelem);
    }
  }
}

void gt_radixsort_GtUlong_divide(GtUlong *source, GtUlong *dest,
                                 unsigned long len)
{
  GtStackGtRadixsort_stackelem stack;
  GtRadixsort_stackelem tmpelem, current;
  unsigned long idx, s, c, *sp, *cp, newleft, count[UINT8_MAX+1] = {0};
  const bool simple = false;

  if (simple)
  {
    GT_STACK_INIT(&stack,64UL);
    tmpelem.shift = (sizeof (unsigned long) - 1) * CHAR_BIT;
    tmpelem.left = source;
    tmpelem.len = len;
    GT_STACK_PUSH(&stack,tmpelem);
  } else
  {
    gt_radixsort_GtUlong_initstack(&stack, source, dest, len);
  }
  while (!GT_STACK_ISEMPTY(&stack))
  {
    current = GT_STACK_POP(&stack);
    /* count occurences of every byte value */
    for (sp = current.left; sp < current.left+current.len; sp++)
    {
      count[GT_RADIX_KEY_UINT8(current.shift,sp)]++;
    }
    /* compute partial sums */
    for (s = 0, cp = count; cp <= count + UINT8_MAX; cp++)
    {
      c = *cp;
      *cp = s;
      s += c;
    }
    /* fill dest with the right values in the right place */
    for (sp = current.left; sp < current.left+current.len; sp++)
    {
      dest[count[GT_RADIX_KEY_UINT8(current.shift,sp)]++] = *sp;
    }
    memcpy(current.left,dest,(size_t) sizeof (*source) * current.len);
    if (current.shift > 0)
    {
      for (idx = 0; idx <= UINT8_MAX; idx++)
      {
        newleft = (idx == 0) ? 0 : count[idx-1];
        /* |newleft .. count[idx]-1| = count[idx]-1-newleft+1
                                     = count[idx]-newleft > 1
        => count[idx] > newleft + 1 */
        if (newleft+1 < count[idx])
        {
          tmpelem.shift = current.shift - CHAR_BIT;
          tmpelem.left = current.left + newleft;
          tmpelem.len = count[idx] - newleft;
          GT_STACK_PUSH(&stack,tmpelem);
        }
      }
    }
    memset(count,0,(size_t) sizeof (*count) * (UINT8_MAX+1));
  }
  GT_STACK_DELETE(&stack);
}
