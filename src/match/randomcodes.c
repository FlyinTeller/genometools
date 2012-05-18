/*
  Copyright (c) 2011-2012 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2011-2012 Center for Bioinformatics, University of Hamburg

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

#include <math.h>
#include "core/arraydef.h"
#include "core/codetype.h"
#include "core/encseq.h"
#include "core/error_api.h"
#include "core/fa.h"
#include "core/logger_api.h"
#include "core/mathsupport.h"
#include "core/radix_sort.h"
#include "core/showtime.h"
#include "core/spacecalc.h"
#include "core/spacepeak.h"
#include "core/undef_api.h"
#include "core/unused_api.h"
#include "core/minmax.h"
#ifdef GT_THREADS_ENABLED
#include "core/thread.h"
#endif
#include "randomcodes-buf.h"
#include "firstcodes-spacelog.h"
#include "randomcodes-tab.h"
#include "randomcodes-accum.h"
#include "randomcodes-insert.h"
#include "randomcodes.h"
#include "marksubstring.h"
#include "seqnumrelpos.h"
#include "randomcodes-sfx-partssuf.h"
#include "sfx-shortreadsort.h"
#include "sfx-suffixer.h"
#include "spmsuftab.h"
#include "stamp.h"

typedef struct
{
  unsigned long *ptr, code, index;
} GtIndexwithcodeRC;

typedef struct
{
  GtIndexwithcodeRC *spaceGtIndexwithcodeRC;
  unsigned long width, nextfreeGtIndexwithcodeRC, allocatedGtIndexwithcodeRC;
  unsigned int depth;
} GtArrayGtIndexwithcodeRC;

typedef struct
{
  unsigned long firstcodehits,
                firstcodeposhits,
                countsequences,
                numofsequences,
                codebuffer_total,
                currentminindex,
                currentmaxindex,
                differentcodes, /* a copy of the same value as in tab */
                widthofpart;
  GtArrayGtIndexwithcodeRC binsearchcache;
  unsigned int flushcount,
               shiftright2index,
               marksuffixunits,
               markprefixunits,
               bitsforposref;
  GtRadixsortinfo *radixsort_code,
                  *radixsort_codepos;
  GtSpmsuftab *spmsuftab;
  GtSfxmappedrange *mappedleftborder,
                   *mappedallrandomcodes,
                   *mappedmarkprefix;
  unsigned long *allrandomcodes;
#define RANDOMCODES_DIFFERENCES
#ifdef RANDOMCODES_DIFFERENCES
  unsigned long *allrandomcodes_differences;
#endif
  GtFirstcodesspacelog *fcsl;
  GtCodeposbuffer buf;
  GtRandomcodestab tab;
} GtRandomcodesinfo;

static double gt_randomcodes_round(double d)
{
  return floor(d + 0.5);
}

static unsigned long gt_kmercode2prefix_index(unsigned long idx,
                                              const void *data)
{
  const GtRandomcodesinfo *fci = (const GtRandomcodesinfo *) data;

  gt_assert(fci != NULL && idx < fci->differentcodes);
  return fci->allrandomcodes[idx] >> fci->shiftright2index;
}

static void gt_minmax_index_kmercode2prefix(unsigned long *minindex,
                                            unsigned long *maxindex,
                                            const void *data)
{
  *minindex = gt_kmercode2prefix_index(*minindex,data);
  *maxindex = gt_kmercode2prefix_index(*maxindex,data);
}

static void gt_storerandomcodes(void *processinfo,
                               GT_UNUSED bool firstinrange,
                               GT_UNUSED unsigned long pos,
                               GtCodetype code)
{
  GtRandomcodesinfo *fci = (GtRandomcodesinfo *) processinfo;

  gt_assert(fci != NULL && firstinrange &&
            fci->allrandomcodes != NULL &&
            fci->countsequences < fci->numofsequences);
  fci->allrandomcodes[fci->countsequences++] = code;
}

#ifdef RANDOMCODES_DIFFERENCES
static void init_randomcodes_differences(GtRandomcodesinfo *fci)
{
  unsigned long idx, previouscode, currentcode;

  /* XXX overlay allrandomcodes_differences and allrandomcodes */
  fci->allrandomcodes_differences
    = malloc(sizeof(*fci->allrandomcodes_differences) * fci->differentcodes);
  gt_assert(fci->allrandomcodes_differences != NULL);
  /* XXX unnecessary */
  fci->allrandomcodes_differences[0] = fci->allrandomcodes[0];
  previouscode = fci->allrandomcodes[0];
  for (idx=1UL; idx < fci->differentcodes; idx++)
  {
    currentcode = fci->allrandomcodes[idx];
    gt_assert(previouscode < currentcode);
    fci->allrandomcodes_differences[idx] = currentcode - previouscode;
    previouscode = currentcode;
  }
}

static const unsigned long *gt_randomcodes_findcodelinear(
                                             const GtRandomcodesinfo *fci,
                                             const unsigned long *foundlinear,
                                             unsigned long startidx,
                                             unsigned long endidx,
                                             unsigned long code,
                                             unsigned long previouscode)
{
  unsigned long idx;

  gt_assert(fci->allrandomcodes_differences != NULL);
  for (idx = startidx; idx <= endidx; idx++)
  {
    previouscode += fci->allrandomcodes_differences[idx];
    if (fci->allrandomcodes[idx] != previouscode)
    {
      fprintf(stderr,"fci->allrandomcodes[%lu] = %lu != %lu =previouscode\n",
                     idx,fci->allrandomcodes[idx],previouscode);
      exit(EXIT_FAILURE);
    }
    if (code <= previouscode)
    {
      return fci->allrandomcodes + idx;
    }
  }
  return foundlinear;
}
#endif

static void gt_randomcodes_fillbinsearchcache(GtRandomcodesinfo *fci,
                                             unsigned int addbscache_depth)
{
  size_t allocbytes = 0;

  fci->binsearchcache.depth
    = addbscache_depth + (unsigned int) log10((double) fci->differentcodes);
  fci->binsearchcache.nextfreeGtIndexwithcodeRC = 0;
  fci->binsearchcache.allocatedGtIndexwithcodeRC
    = 1UL << (fci->binsearchcache.depth+1);
  fci->binsearchcache.width
    = fci->differentcodes/fci->binsearchcache.allocatedGtIndexwithcodeRC;
  if (fci->binsearchcache.allocatedGtIndexwithcodeRC < fci->differentcodes)
  {
    unsigned long idx, current = fci->binsearchcache.width;

    allocbytes = sizeof (*fci->binsearchcache.spaceGtIndexwithcodeRC)
                 * fci->binsearchcache.allocatedGtIndexwithcodeRC;
    fci->binsearchcache.spaceGtIndexwithcodeRC = gt_malloc(allocbytes);
    for (idx=0; idx < fci->binsearchcache.allocatedGtIndexwithcodeRC; idx++)
    {
      gt_assert(current < fci->differentcodes);
      fci->binsearchcache.
           spaceGtIndexwithcodeRC[fci->binsearchcache.nextfreeGtIndexwithcodeRC]
                               .ptr = fci->allrandomcodes + current;
      fci->binsearchcache.
           spaceGtIndexwithcodeRC[fci->binsearchcache.nextfreeGtIndexwithcodeRC]
                               .index = current;
      fci->binsearchcache.
        spaceGtIndexwithcodeRC[fci->binsearchcache.nextfreeGtIndexwithcodeRC++]
        .code = fci->allrandomcodes[current];
      current += fci->binsearchcache.width;
    }
  }
#ifdef RANDOMCODES_DIFFERENCES
  init_randomcodes_differences(fci);
#endif
  gt_log_log("binsearchcache.depth=%u => %lu bytes",
             fci->binsearchcache.depth,
             (unsigned long) allocbytes);
  GT_FCI_ADDWORKSPACE(fci->fcsl,"binsearchcache",allocbytes);
}

#define SHOWFOUND(F)\
        if ((F) == NULL)\
        {\
          fprintf(stderr,"%s = NULL\n",#F);\
        } else\
        {\
          fprintf(stderr,"%s = %lu\n",#F,\
                   (unsigned long) ((F) - fci->allrandomcodes));\
        }

const unsigned long gt_randomcodes_find_accu(bool searchlinear,
                                            const GtRandomcodesinfo *fci,
                                            unsigned long code)
{
  const unsigned long *found = NULL, *leftptr = NULL, *midptr, *rightptr = NULL;
  unsigned long previouscode = ULONG_MAX;

  if (code <= fci->allrandomcodes[0])
  {
    return 0;
  }
  if (fci->binsearchcache.spaceGtIndexwithcodeRC != NULL)
  {
    const GtIndexwithcodeRC *leftic, *midic, *rightic;
    unsigned int depth;

    leftic = fci->binsearchcache.spaceGtIndexwithcodeRC;
    rightic = fci->binsearchcache.spaceGtIndexwithcodeRC +
              fci->binsearchcache.nextfreeGtIndexwithcodeRC - 1;
    for (depth = 0; /* Nothing */; depth++)
    {
      midic = leftic + GT_DIV2((unsigned long) (rightic-leftic));
      if (code < midic->code)
      {
        found = midic->ptr;
        if (depth < fci->binsearchcache.depth)
        {
          rightic = midic - 1;
        } else
        {
          gt_assert(leftic->ptr != NULL && rightic->ptr != NULL);
          if (leftic > fci->binsearchcache.spaceGtIndexwithcodeRC)
          {
            leftptr = (leftic-1)->ptr + 1;
            previouscode = (leftic-1)->code;
          } else
          {
            gt_assert(code > fci->allrandomcodes[0]);
            leftptr = fci->allrandomcodes + 1;
            previouscode = fci->allrandomcodes[0];
          }
          rightptr = rightic->ptr - 1;
          break;
        }
      } else
      {
        if (code > midic->code)
        {
          if (depth < fci->binsearchcache.depth)
          {
            leftic = midic + 1;
          } else
          {
            gt_assert(leftic->ptr != NULL && rightic->ptr != NULL);
            leftptr = leftic->ptr + 1;
            previouscode = leftic->code;
            if (rightic < fci->binsearchcache.spaceGtIndexwithcodeRC +
                          fci->binsearchcache.nextfreeGtIndexwithcodeRC - 1)
            {
              rightptr = (rightic+1)->ptr - 1;
            } else
            {
              rightptr = fci->allrandomcodes + fci->differentcodes - 1;
            }
            break;
          }
        } else
        {
          gt_assert(midic->ptr != NULL);
          return (unsigned long) (midic->ptr - fci->allrandomcodes);
        }
      }
    }
    gt_assert(leftptr != NULL && rightptr != NULL);
  } else
  {
    leftptr = fci->allrandomcodes + 1;
    previouscode = fci->allrandomcodes[0];
    rightptr = fci->allrandomcodes + fci->differentcodes - 1;
  }
  if (searchlinear)
  {
#ifdef RANDOMCODES_DIFFERENCES
    if (leftptr <= rightptr)
    {
      gt_assert(previouscode != ULONG_MAX);
      found = gt_randomcodes_findcodelinear(fci,
          found,
          (unsigned long) (leftptr - fci->allrandomcodes),
          (unsigned long) (rightptr - fci->allrandomcodes),
          code,
          previouscode);
    }
#endif
  } else
  {
    while (leftptr <= rightptr)
    {
      midptr = leftptr + GT_DIV2((unsigned long) (rightptr-leftptr));
      if (code < *midptr)
      {
        rightptr = midptr - 1;
        found = midptr;
      } else
      {
        if (code > *midptr)
        {
          leftptr = midptr + 1;
        } else
        {
          gt_assert(midptr != NULL);
          return (unsigned long) (midptr - fci->allrandomcodes);
        }
      }
    }
  }
  return (found != NULL) ? (unsigned long) (found - fci->allrandomcodes)
                         : ULONG_MAX;
}

const unsigned long *gt_randomcodes_find_insert(const GtRandomcodesinfo *fci,
                                               unsigned long code)
{
  const unsigned long *found = NULL, *leftptr = NULL, *midptr, *rightptr = NULL;

  leftptr = fci->allrandomcodes + fci->currentminindex;
  rightptr = fci->allrandomcodes + fci->currentmaxindex;
  while (leftptr <= rightptr)
  {
    midptr = leftptr + GT_DIV2((unsigned long) (rightptr-leftptr));
    if (code < *midptr)
    {
      rightptr = midptr - 1;
      found = midptr;
    } else
    {
      if (code > *midptr)
      {
        leftptr = midptr + 1;
      } else
      {
        return midptr;
      }
    }
  }
  return found;
}

static unsigned long gt_randomcodes_accumulatecounts_merge(
                                        GtRandomcodesinfo *fci,
                                        const unsigned long *querystream_fst,
                                        const unsigned long *subjectstream_fst)
{
  unsigned long found = 0;
  const unsigned long *query = querystream_fst,
                      *subject = subjectstream_fst,
                      *querystream_lst = fci->buf.spaceGtUlong
                                         + fci->buf.nextfree - 1,
                      *subjectstream_lst = fci->allrandomcodes
                                           + fci->differentcodes - 1;

  while (query <= querystream_lst && subject <= subjectstream_lst)
  {
    if (*query <= *subject)
    {
      if (*query == *subject)
      {
        gt_randomcodes_countocc_increment(&fci->tab,(unsigned long)
                                         (subject - fci->allrandomcodes),
                                         false);
        found++;
      }
      query++;
    } else
    {
      subject++;
    }
  }
  return found;
}

static void gt_randomcodes_accumulatecounts_flush(void *data)
{
  GtRandomcodesinfo *fci = (GtRandomcodesinfo *) data;

  if (fci->buf.nextfree > 0)
  {
    unsigned long foundindex, foundindex_linear;

    gt_assert(fci->allrandomcodes != NULL);
    fci->codebuffer_total += fci->buf.nextfree;
    gt_radixsort_inplace_sort(fci->radixsort_code,fci->buf.nextfree);
    foundindex = gt_randomcodes_find_accu(false,fci,fci->buf.spaceGtUlong[0]);
    foundindex_linear = gt_randomcodes_find_accu(true,fci,
                                                fci->buf.spaceGtUlong[0]);
    gt_assert(foundindex == foundindex_linear);
    if (foundindex != ULONG_MAX)
    {
      fci->firstcodehits
        += gt_randomcodes_accumulatecounts_merge(fci,fci->buf.spaceGtUlong,
                                                fci->allrandomcodes +
                                                foundindex);
    }
    fci->flushcount++;
    fci->buf.nextfree = 0;
  }
}

static unsigned long gt_randomcodes_insertsuffixes_merge(
                                        GtRandomcodesinfo *fci,
                                        const GtUlongPair *querystream_fst,
                                        const unsigned long *subjectstream_fst)
{
  unsigned long found = 0, idx;
  const GtUlongPair *query = querystream_fst,
                    *querystream_lst = fci->buf.spaceGtUlongPair +
                                       fci->buf.nextfree - 1;
  const unsigned long *subject = subjectstream_fst,
                      *subjectstream_lst = fci->allrandomcodes +
                                           fci->currentmaxindex;

  while (query <= querystream_lst && subject <= subjectstream_lst)
  {
    if (query->a <= *subject)
    {
      if (query->a == *subject)
      {
        idx = gt_randomcodes_insertionindex(&fci->tab,
                                           (unsigned long)
                                           (subject - fci->allrandomcodes));
        gt_assert(idx < fci->firstcodehits + fci->numofsequences);
        gt_spmsuftab_set(fci->spmsuftab,idx,
                         gt_spmsuftab_usebitsforpositions(fci->spmsuftab)
                           ? gt_seqnumrelpos_decode_pos(fci->buf.snrp,query->b)
                           : query->b);
        found++;
      }
      query++;
    } else
    {
      subject++;
    }
  }
  return found;
}

static void gt_randomcodes_insertsuffixes_flush(void *data)
{
  GtRandomcodesinfo *fci = (GtRandomcodesinfo *) data;

  if (fci->buf.nextfree > 0)
  {
    const unsigned long *ptr;

    gt_assert(fci->allrandomcodes != NULL);
    fci->codebuffer_total += fci->buf.nextfree;
    gt_radixsort_inplace_sort(fci->radixsort_codepos,fci->buf.nextfree);
    ptr = gt_randomcodes_find_insert(fci,fci->buf.spaceGtUlongPair[0].a);
    if (ptr != NULL)
    {
      fci->firstcodeposhits
        += gt_randomcodes_insertsuffixes_merge(fci,fci->buf.spaceGtUlongPair,
                                              ptr);
    }
    fci->flushcount++;
    fci->buf.nextfree = 0;
  }
}

static void gt_randomcodes_checksuftab_bucket(const GtEncseq *encseq,
                                             GtReadmode readmode,
                                             GtEncseqReader *esr1,
                                             GtEncseqReader *esr2,
                                             unsigned long previoussuffix,
                                             bool previousdefined,
                                             const unsigned long
                                               *seqnum_relpos_bucket,
                                             const GtSeqnumrelpos *snrp,
                                             GT_UNUSED const uint16_t
                                               *lcptab_bucket,
                                             unsigned long numberofsuffixes)
{
  unsigned long idx, current, maxlcp,
                totallength = gt_encseq_total_length(encseq);
  const unsigned long depth = 0;
  GT_UNUSED int cmp;
  const bool specialsareequal = false, specialsareequalatdepth0 = false;

  gt_assert(!previousdefined || previoussuffix < totallength);
  for (idx = 0; idx < numberofsuffixes; idx++)
  {
    current = gt_seqnumrelpos_decode_pos(snrp,seqnum_relpos_bucket[idx]);
    if (previousdefined && idx < totallength)
    {
      gt_assert(current < totallength);
      cmp = gt_encseq_check_comparetwosuffixes(encseq,
                                               readmode,
                                               &maxlcp,
                                               specialsareequal,
                                               specialsareequalatdepth0,
                                               depth,
                                               previoussuffix,
                                               current,
                                               esr1,
                                               esr2);
      gt_assert(cmp <= 0);
      gt_assert(idx == 0 || maxlcp == (unsigned long) lcptab_bucket[idx]);
    }
    previoussuffix = current;
    previousdefined = true;
  }
}

static int gt_randomcodes_sortremaining(GtShortreadsortworkinfo *srsw,
                                       const GtEncseq *encseq,
                                       GtReadmode readmode,
                                       const GtSpmsuftab *spmsuftab,
                                       const GtSeqnumrelpos *snrp,
                                       const GtRandomcodestab *rct,
                                       unsigned long minindex,
                                       unsigned long maxindex,
                                       unsigned long sumofwidth,
                                       unsigned long spaceforbucketprocessing,
                                       unsigned long depth,
                                       GtRandomcodesintervalprocess itvprocess,
                                       GtRandomcodesintervalprocess_end
                                              itvprocess_end,
                                       void *itvprocessdata,
                                       bool withsuftabcheck,
                                       GtError *err)
{
  unsigned long current,
                next = GT_UNDEF_ULONG,
                idx,
                width,
                sumwidth = 0,
                previoussuffix = 0;
  GtShortreadsortresult srsresult;
  bool previousdefined = false, haserr = false;

  current = gt_randomcodes_get_leftborder(rct,minindex);
  for (idx = minindex; idx <= maxindex; idx++)
  {
    if (idx < maxindex)
    {
      next = gt_randomcodes_get_leftborder(rct,idx+1);
      gt_assert(current < next);
      width = next - current;
    } else
    {
      gt_assert(sumofwidth > current);
      width = sumofwidth - current;
    }
    sumwidth += width;
    gt_assert(sumwidth <= spmsuftab->numofentries);
    if (width >= 2UL)
    {
      gt_shortreadsort_firstcodes_sort(&srsresult,
                                       srsw,
                                       snrp,
                                       encseq,
                                       spmsuftab,
                                       current,
                                       width,
                                       depth);
      if (withsuftabcheck)
      {
        gt_randomcodes_checksuftab_bucket(encseq,
                                         readmode,
                                         NULL,
                                         NULL,
                                         previoussuffix,
                                         previousdefined,
                                         srsresult.suftab_bucket,
                                         snrp,
                                         srsresult.lcptab_bucket,
                                         width);
        previousdefined = true;
        previoussuffix
          = gt_seqnumrelpos_decode_pos(snrp,srsresult.suftab_bucket[width-1]);
      }
      if (itvprocess != NULL)
      {
        if (itvprocess(itvprocessdata,
                       srsresult.suftab_bucket,
                       snrp,srsresult.lcptab_bucket,width,
                       spaceforbucketprocessing,err) != 0)
        {
          haserr = true;
          break;
        }
      }
    } else
    {
      gt_assert(width == 1UL);
    }
    gt_assert(next != GT_UNDEF_ULONG);
    current = next;
  }
  if (itvprocess_end != NULL)
  {
    itvprocess_end(itvprocessdata);
  }
  return haserr ? -1 : 0;
}

#ifdef GT_THREADS_ENABLED

static unsigned long gt_randomcodes_findfirstlarger(const GtRandomcodestab *rct,
                                                   unsigned long start,
                                                   unsigned long end,
                                                   unsigned long offset)
{
  unsigned long left = start, right = end, found = end, mid, midval;

  while (left+1 < right)
  {
    mid = GT_DIV2(left+right);
    midval = gt_randomcodes_get_leftborder(rct,mid);
    if (offset == midval)
    {
      return mid;
    }
    if (offset < midval)
    {
      found = mid;
      right = mid - 1;
    } else
    {
      left = mid + 1;
    }
  }
  return found;
}

static unsigned long *gt_evenly_divide_part(const GtRandomcodestab *rct,
                                            unsigned long partminindex,
                                            unsigned long partmaxindex,
                                            unsigned long numofsuffixes,
                                            unsigned int numofparts)
{
  unsigned long *endindexes, widthofpart, offset;
  unsigned int part, remainder;

  gt_assert(partminindex < partmaxindex && numofparts >= 2U);
  widthofpart = numofsuffixes/numofparts;
  endindexes = gt_malloc(sizeof (*endindexes) * numofparts);
  offset = gt_randomcodes_get_leftborder(rct,partminindex);
  remainder = (unsigned int) (numofsuffixes % (unsigned long) numofparts);
  for (part=0; part < numofparts; part++)
  {
    if (remainder > 0)
    {
      offset += widthofpart + 1;
      remainder--;
    } else
    {
      offset += widthofpart;
    }
    if (part == numofparts - 1)
    {
      endindexes[part] = partmaxindex;
    } else
    {
      unsigned long start = (part == 0) ? partminindex : endindexes[part-1] + 1;

      endindexes[part] = gt_randomcodes_findfirstlarger(rct,start,partmaxindex,
                                                       offset);
      gt_assert(endindexes[part] <= partmaxindex);
    }
  }
  return endindexes;
}

typedef struct
{
  GtShortreadsortworkinfo *srsw;
  const GtEncseq *encseq;
  GtReadmode readmode;
  const GtSpmsuftab *spmsuftab;
  const GtSeqnumrelpos *snrp;
  const GtRandomcodestab *rct;
  unsigned long depth,
                minindex,
                maxindex,
                sumofwidth,
                spaceforbucketprocessing;
  bool withsuftabcheck;
  GtRandomcodesintervalprocess itvprocess;
  GtRandomcodesintervalprocess_end itvprocess_end;
  void *itvprocessdata;
  GtError *err;
  GtThread *thread;
} GtSortRemainingThreadinfo;

static void *gt_randomcodes_thread_caller_sortremaining(void *data)
{
  GtSortRemainingThreadinfo *threadinfo = (GtSortRemainingThreadinfo *) data;

  if (gt_randomcodes_sortremaining(threadinfo->srsw,
                                  threadinfo->encseq,
                                  threadinfo->readmode,
                                  threadinfo->spmsuftab,
                                  threadinfo->snrp,
                                  threadinfo->rct,
                                  threadinfo->minindex,
                                  threadinfo->maxindex,
                                  threadinfo->sumofwidth,
                                  threadinfo->spaceforbucketprocessing,
                                  threadinfo->depth,
                                  threadinfo->itvprocess,
                                  threadinfo->itvprocess_end,
                                  threadinfo->itvprocessdata,
                                  threadinfo->withsuftabcheck,
                                  threadinfo->err) != 0)
  {
    gt_assert(false);
  }
  return NULL;
}

static int gt_randomcodes_thread_sortremaining(
                                       GtShortreadsortworkinfo **srswtab,
                                       const GtEncseq *encseq,
                                       GtReadmode readmode,
                                       const GtSpmsuftab *spmsuftab,
                                       const GtSeqnumrelpos *snrp,
                                       const GtRandomcodestab *rct,
                                       unsigned long partminindex,
                                       unsigned long partmaxindex,
                                       unsigned long widthofpart,
                                       unsigned long sumofwidth,
                                       unsigned long spaceforbucketprocessing,
                                       unsigned long depth,
                                       GtRandomcodesintervalprocess itvprocess,
                                       GtRandomcodesintervalprocess_end
                                         itvprocess_end,
                                       void *itvprocessdatatab,
                                       bool withsuftabcheck,
                                       unsigned int threads,
                                       GtLogger *logger,
                                       GtError *err)
{
  unsigned int t;
  unsigned long sum = 0, *endindexes;
  GtSortRemainingThreadinfo *threadinfo;
  bool haserr = false;

  gt_assert(threads >= 2U);
  endindexes = gt_evenly_divide_part(rct,partminindex,partmaxindex,widthofpart,
                                     threads);
  threadinfo = gt_malloc(sizeof (*threadinfo) * threads);
  for (t=0; t<threads; t++)
  {
    unsigned long lb;

    threadinfo[t].srsw = srswtab[t];
    threadinfo[t].encseq = encseq;
    threadinfo[t].spmsuftab = spmsuftab;
    threadinfo[t].snrp = snrp;
    threadinfo[t].rct = rct;
    threadinfo[t].minindex = t == 0 ? partminindex : endindexes[t-1] + 1,
    threadinfo[t].maxindex = endindexes[t];
    threadinfo[t].readmode = readmode;
    threadinfo[t].withsuftabcheck = withsuftabcheck;
    threadinfo[t].spaceforbucketprocessing = spaceforbucketprocessing;
    threadinfo[t].depth = depth;
    threadinfo[t].itvprocess = itvprocess;
    threadinfo[t].itvprocess_end = itvprocess_end;
    if (itvprocessdatatab == NULL)
    {
      threadinfo[t].itvprocessdata = NULL;
    } else
    {
      threadinfo[t].itvprocessdata = ((void **) itvprocessdatatab)[t];
    }
    threadinfo[t].err = err;
    lb = gt_randomcodes_get_leftborder(rct,threadinfo[t].minindex);
    if (t < threads - 1)
    {
      threadinfo[t].sumofwidth
        = gt_randomcodes_get_leftborder(rct,threadinfo[t].maxindex+1);
    } else
    {
      threadinfo[t].sumofwidth = sumofwidth;
    }
    gt_assert(lb < threadinfo[t].sumofwidth);
    gt_logger_log(logger,"thread %u: process [%lu,%lu]=[%lu,%lu] "
                         "of width %lu",t,threadinfo[t].minindex,
                                          threadinfo[t].maxindex,
                                          lb,
                                          threadinfo[t].sumofwidth,
                                          threadinfo[t].sumofwidth - lb);
    sum += threadinfo[t].sumofwidth - lb;
    threadinfo[t].thread
      = gt_thread_new (gt_randomcodes_thread_caller_sortremaining,
                       threadinfo + t,err);
    if (threadinfo[t].thread == NULL)
    {
      haserr = true;
    }
  }
  gt_assert (haserr || sum == widthofpart);
  for (t=0; t<threads; t++)
  {
    if (!haserr)
    {
      gt_thread_join(threadinfo[t].thread);
    }
    gt_thread_delete(threadinfo[t].thread);
  }
  gt_free(threadinfo);
  gt_free(endindexes);
  return haserr ? -1 : 0;
}
#endif

static void gt_firstcode_delete_before_end(GtRandomcodesinfo *fci)
{
  if (fci->binsearchcache.spaceGtIndexwithcodeRC != NULL)
  {
    GT_FCI_SUBTRACTWORKSPACE(fci->fcsl,"binsearchcache");
    GT_FREEARRAY(&fci->binsearchcache,GtIndexwithcodeRC);
  }
  if (fci->radixsort_codepos != NULL)
  {
    gt_radixsort_delete(fci->radixsort_codepos);
    GT_FCI_SUBTRACTWORKSPACE(fci->fcsl,"radixsort_codepos");
    fci->radixsort_codepos = NULL;
  }
  if (fci->mappedmarkprefix != NULL)
  {
    gt_Sfxmappedrange_delete(fci->mappedmarkprefix);
    gt_marksubstring_delete(fci->buf.markprefix,true);
  } else
  {
    gt_marksubstring_delete(fci->buf.markprefix,true);
    if (fci->buf.markprefix != NULL)
    {
      GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"markprefix");
    }
  }
  fci->buf.markprefix = NULL;
  gt_marksubstring_delete(fci->buf.marksuffix,true);
  if (fci->buf.marksuffix != NULL)
  {
    GT_FCI_SUBTRACTWORKSPACE(fci->fcsl,"marksuffix");
  }
  fci->buf.marksuffix = NULL;
  if (fci->mappedallrandomcodes == NULL && fci->allrandomcodes != NULL)
  {
    gt_free(fci->allrandomcodes);
    fci->allrandomcodes = NULL;
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"allrandomcodes");
  }
}

static unsigned long gt_randomcodes_idx2code(const GtRandomcodesinfo *fci,
                                            unsigned long idx)
{
  gt_assert(idx <= fci->differentcodes);
  if (idx == fci->differentcodes)
  {
    return fci->allrandomcodes[idx-1];
  }
  return fci->allrandomcodes[idx];
}

void gt_rungetencseqkmers_rc(const GtEncseq *encseq,unsigned int kmersize)
{
  const GtReadmode readmode = GT_READMODE_FORWARD;

  getencseqkmers_twobitencoding(encseq,
                                readmode,
                                kmersize,
                                kmersize,
                                false,
                                NULL,
                                NULL,
                                NULL,
                                NULL);
}

static void run_allcodes_distribution(const unsigned long *allrandomcodes,
                                      unsigned long differentcodes)
{
  unsigned long idx, diff, mindiff = 0, maxdiff = 0, distbits[64+1] = {0};

  for (idx = 1UL; idx < differentcodes; idx++)
  {
    gt_assert(allrandomcodes[idx-1] < allrandomcodes[idx]);
    diff = allrandomcodes[idx] - allrandomcodes[idx-1];
    if (idx == 1UL || diff < mindiff)
    {
      mindiff = diff;
    }
    if (diff > maxdiff)
    {
      maxdiff = diff;
    }
    distbits[gt_determinebitspervalue(diff)]++;
  }
  printf("allrandomcodes: mindiff=%lu,maxdiff=%lu(%u bits)\n",
         mindiff,maxdiff,gt_determinebitspervalue(maxdiff));
  for (idx = 0; idx <= 64UL; idx++)
  {
    if (distbits[idx] > 0)
    {
      printf("%lu bits: %lu\n",idx,distbits[idx]);
    }
  }
}

static int gt_randomcodes_init(GtRandomcodesinfo *fci,
                              const GtEncseq *encseq,
                              unsigned int kmersize,
                              bool withsuftabcheck,
                              unsigned int minmatchlength,
                              GtError *err)
{
  unsigned long totallength, maxseqlength, maxrelpos;
  unsigned int logtotallength, bitsforrelpos, bitsforseqnum;
  bool haserr = false;

  maxseqlength = gt_encseq_max_seq_length(encseq);
  totallength = gt_encseq_total_length(encseq);
  logtotallength
    = (unsigned int) gt_randomcodes_round(log((double) totallength));
  if (logtotallength >= 8U)
  {
    fci->markprefixunits = MAX(8U,logtotallength - 8U);
  } else
  {
    fci->markprefixunits = MIN(kmersize/2U,8U);
  }
  if (fci->markprefixunits >= 2U)
  {
    fci->marksuffixunits = fci->markprefixunits - 1;
  } else
  {
    fci->marksuffixunits = fci->markprefixunits;
  }
  if (fci->marksuffixunits + fci->markprefixunits > kmersize)
  {
    if (fci->marksuffixunits % 2U == 0)
    {
      fci->marksuffixunits = fci->markprefixunits = kmersize/2U;
    } else
    {
      fci->marksuffixunits = kmersize/2;
      fci->markprefixunits = kmersize - fci->marksuffixunits;
    }
  }
  gt_log_log("markprefixunits=%u,marksuffixunits=%u",fci->markprefixunits,
                                                     fci->marksuffixunits);
  if (maxseqlength > (unsigned long) minmatchlength)
  {
    maxrelpos = maxseqlength - (unsigned long) minmatchlength;
  } else
  {
    maxrelpos = 0;
  }
  bitsforrelpos = gt_determinebitspervalue(maxrelpos);
  fci->buf.snrp = gt_seqnumrelpos_new(bitsforrelpos,encseq);
  fci->buf.markprefix = NULL;
  fci->buf.marksuffix = NULL;
  fci->numofsequences = gt_encseq_num_of_sequences(encseq);
  gt_assert(fci->numofsequences > 0);
  bitsforseqnum = gt_determinebitspervalue(fci->numofsequences - 1);
  if (bitsforseqnum + bitsforrelpos > (unsigned int) GT_INTWORDSIZE)
  {
    gt_seqnumrelpos_delete(fci->buf.snrp);
    fci->buf.snrp = NULL;
    gt_error_set(err,"cannot process encoded sequences with %lu sequences "
                     "of length up to %lu (%u+%u bits)",
                     fci->numofsequences,maxseqlength,bitsforseqnum,
                     bitsforrelpos);
    haserr = true;
  }
  fci->fcsl = gt_firstcodes_spacelog_new();
  fci->spmsuftab = NULL;
  fci->radixsort_code = NULL;
  fci->radixsort_codepos = NULL;
  fci->buf.spaceGtUlongPair = NULL;
  fci->buf.spaceGtUlong = NULL;
  fci->mappedallrandomcodes = NULL;
  fci->mappedmarkprefix = NULL;
  fci->mappedleftborder = NULL;
  GT_FCI_ADDWORKSPACE(fci->fcsl,"encseq",(size_t) gt_encseq_sizeofrep(encseq));
  if (withsuftabcheck)
  {
    gt_firstcodes_spacelog_start_diff(fci->fcsl);
  }
  if (!haserr)
  {
    size_t sizeforcodestable
      = sizeof (*fci->allrandomcodes) * fci->numofsequences;
    fci->allrandomcodes = gt_malloc(sizeforcodestable);
    GT_FCI_ADDSPLITSPACE(fci->fcsl,"allrandomcodes",sizeforcodestable);
  } else
  {
    fci->allrandomcodes = NULL;
  }
#ifdef RANDOMCODES_DIFFERENCES
  fci->allrandomcodes_differences = NULL;
#endif
  gt_randomcodes_countocc_setnull(&fci->tab);
  fci->countsequences = 0;
  fci->firstcodehits = 0;
  fci->firstcodeposhits = 0;
  fci->bitsforposref = bitsforseqnum + bitsforrelpos;
  GT_INITARRAY(&fci->binsearchcache,GtIndexwithcodeRC);
  return haserr ? -1 : 0;
}

static void gt_randomcodes_collectcodes(GtRandomcodesinfo *fci,
                                       const GtEncseq *encseq,
                                       GtReadmode readmode,
                                       unsigned int kmersize,
                                       unsigned int minmatchlength,
                                       GtLogger *logger,
                                       GtTimer *timer)
{
  unsigned int numofchars;

  getencseqkmers_twobitencoding(encseq,
                                readmode,
                                kmersize,
                                minmatchlength,
                                true,
                                gt_storerandomcodes,
                                fci,
                                NULL,
                                NULL);
  fci->numofsequences = fci->countsequences;
  gt_logger_log(logger,"have stored %lu prefix codes",fci->numofsequences);
  if (timer != NULL)
  {
    gt_timer_show_progress(timer, "to sort initial prefixes",stdout);
  }
  gt_radixsort_inplace_ulong(fci->allrandomcodes,fci->numofsequences);
  numofchars = gt_encseq_alphabetnumofchars(encseq);
  gt_assert(numofchars == 4U);
  fci->buf.markprefix = gt_marksubstring_new(numofchars,kmersize,false,
                                            fci->markprefixunits);
  fci->shiftright2index = gt_marksubstring_shiftright(fci->buf.markprefix)
                          + GT_LOGWORDSIZE;
  GT_FCI_ADDSPLITSPACE(fci->fcsl,"markprefix",
                      (size_t) gt_marksubstring_size(fci->buf.markprefix));
  fci->buf.marksuffix = gt_marksubstring_new(numofchars,kmersize,true,
                                            fci->marksuffixunits);
  GT_FCI_ADDWORKSPACE(fci->fcsl,"marksuffix",
                      (size_t) gt_marksubstring_size(fci->buf.marksuffix));
  gt_assert(fci->allrandomcodes != NULL);
  fci->differentcodes = gt_randomcodes_remdups(fci->allrandomcodes,
                                             fci->fcsl,
                                             &fci->tab,
                                             fci->numofsequences,
                                             fci->buf.markprefix,
                                             fci->buf.marksuffix,
                                             logger);
  if (fci->differentcodes > 0 && fci->differentcodes < fci->numofsequences)
  {
    fci->allrandomcodes = gt_realloc(fci->allrandomcodes,
                                    sizeof (*fci->allrandomcodes) *
                                    fci->differentcodes);
    GT_FCI_SUBTRACTADDSPLITSPACE(fci->fcsl,"allrandomcodes",
                                 sizeof (*fci->allrandomcodes) *
                                 fci->differentcodes);
  }
}

static int gt_randomcodes_allocspace(GtRandomcodesinfo *fci,
                                    unsigned int numofparts,
                                    unsigned long maximumspace,
                                    unsigned long phase2extra,
                                    GtError *err)
{
  if (maximumspace > 0)
  {
    if ((unsigned long) gt_firstcodes_spacelog_total(fci->fcsl) +
                        phase2extra >= maximumspace)
    {
      gt_error_set(err,"already used %.2f MB of memory and need %.2f MB later "
                       "=> cannot compute index in at most %.2f MB",
                       GT_MEGABYTES(gt_firstcodes_spacelog_total(fci->fcsl)),
                       GT_MEGABYTES(phase2extra),
                       GT_MEGABYTES(maximumspace));
      return -1;
    } else
    {
      size_t remainspace = (size_t) maximumspace -
                           (gt_firstcodes_spacelog_total(fci->fcsl) +
                            phase2extra);

      fci->buf.allocated
        = gt_radixsort_max_num_of_entries_ulong(remainspace);
      if (fci->buf.allocated < fci->differentcodes/16UL)
      {
        fci->buf.allocated = fci->differentcodes/16UL;
      }
    }
  } else
  {
    if (numofparts == 0)
    {
      fci->buf.allocated = gt_radixsort_max_num_of_entries_ulong(
                                 gt_firstcodes_spacelog_total(fci->fcsl)/7UL);
    } else
    {
      fci->buf.allocated = fci->differentcodes/5;
    }
  }
  if (fci->buf.allocated < 16UL)
  {
    fci->buf.allocated = 16UL;
  }
  return 0;
}

static void gt_randomcodes_accumulatecounts_run(GtRandomcodesinfo *fci,
                                               const GtEncseq *encseq,
                                               unsigned int kmersize,
                                               unsigned int minmatchlength,
                                               GtLogger *logger,
                                               GtTimer *timer)
{
  if (timer != NULL)
  {
    gt_timer_show_progress(timer, "to accumulate counts",stdout);
  }
  gt_assert(fci->buf.allocated > 0);
  fci->radixsort_code = gt_radixsort_new_ulong(fci->buf.allocated);
  fci->buf.spaceGtUlong = gt_radixsort_space_ulong(fci->radixsort_code);
  GT_FCI_ADDWORKSPACE(fci->fcsl,"radixsort_code",
                      gt_radixsort_size(fci->radixsort_code));
  fci->buf.fciptr = fci; /* as we need to give fci to the flush function */
  fci->buf.flush_function = gt_randomcodes_accumulatecounts_flush;
  gt_logger_log(logger,"maximum space for accumulating counts %.2f MB",
                GT_MEGABYTES(gt_firstcodes_spacelog_total(fci->fcsl)));
  gt_randomcodes_accum_runkmerscan(encseq, kmersize, minmatchlength,&fci->buf);
  gt_randomcodes_accumulatecounts_flush(fci);
  gt_logger_log(logger,"codebuffer_total=%lu (%.3f%% of all suffixes)",
                fci->codebuffer_total,
                100.0 * (double) fci->codebuffer_total/
                                 gt_encseq_total_length(encseq));
#ifdef RANDOMCODES_DIFFERENCES
  if (fci->allrandomcodes_differences != NULL)
  {
    free(fci->allrandomcodes_differences);
  }
#endif
  if (fci->firstcodehits > 0)
  {
    gt_assert(fci->flushcount > 0);
    gt_logger_log(logger,"firstcodehits=%lu (%.3f%% of all suffixes), "
                         "%u rounds (avg length %lu)",
                         fci->firstcodehits,
                         100.0 * (double) fci->firstcodehits/
                                          gt_encseq_total_length(encseq),
                         fci->flushcount,
                         fci->codebuffer_total/fci->flushcount);
  }
  gt_radixsort_delete(fci->radixsort_code);
  fci->radixsort_code = NULL;
  GT_FCI_SUBTRACTWORKSPACE(fci->fcsl,"radixsort_code");
  if (timer != NULL)
  {
    gt_timer_show_progress(timer,"to compute partial sums",stdout);
  }
}

static void gt_randomcodes_map_sections(GtRandomcodesinfo *fci,
                                       GtSfxmappedrangelist *sfxmrlist)
{
  fci->mappedmarkprefix
    = gt_Sfxmappedrange_new("markprefix",
                            gt_marksubstring_entries(fci->buf.markprefix),
                            GtSfxGtBitsequence,
                            gt_minmax_index_kmercode2prefix,
                            fci);
  gt_Sfxmappedrangelist_add(sfxmrlist,fci->mappedmarkprefix);
  if (fci->differentcodes > 0)
  {
    fci->mappedallrandomcodes = gt_Sfxmappedrange_new("allrandomcodes",
                                                    fci->differentcodes,
                                                    GtSfxunsignedlong,
                                                    NULL,
                                                    NULL);
    gt_Sfxmappedrangelist_add(sfxmrlist,fci->mappedallrandomcodes);
    fci->mappedleftborder = gt_Sfxmappedrange_new("leftborder",
                                                 fci->differentcodes+1,
                                                 GtSfxuint32_t,
                                                 NULL,
                                                 NULL);
    gt_Sfxmappedrangelist_add(sfxmrlist,fci->mappedleftborder);
  }
}

static int gt_randomcodes_auto_parts(GtRandomcodesinfo *fci,
                                    GtSfxmappedrangelist *sfxmrlist,
                                    unsigned int numofparts,
                                    unsigned long *maximumspace,
                                    unsigned long maxbucketsize,
                                    unsigned int kmersize,
                                    unsigned long totallength,
                                    unsigned long maxseqlength,
                                    unsigned long suftabentries,
                                    unsigned long phase2extra,
                                    GtError *err)
{
  int retval;
  unsigned long leftbordersize_all;

  if (numofparts == 0 && *maximumspace == 0)
  {
    *maximumspace = (unsigned long)
                    (gt_firstcodes_spacelog_peak(fci->fcsl) +
                     phase2extra +
                     gt_shortreadsort_size(true,maxbucketsize,
                                           maxseqlength - kmersize) +
                     4 * 4096);
  } else
  {
    gt_assert(*maximumspace > 0);
  }
  if (fci->mappedleftborder != NULL)
  {
    leftbordersize_all = gt_Sfxmappedrange_size_mapped(
                             fci->mappedleftborder,0,
                             gt_randomcodes_leftborder_entries(&fci->tab)-1);
  } else
  {
    leftbordersize_all = 0;
  }
  retval = gt_suftabparts_rc_fit_memlimit(
      gt_firstcodes_spacelog_total(fci->fcsl)
      /*as this is subtracted*/
      + leftbordersize_all
      + phase2extra,
      *maximumspace,
      NULL,
      &fci->tab,
      sfxmrlist,
      totallength,
      fci->bitsforposref,
      0, /* special characters not used */
      suftabentries,
      false, /* suftabuint not used */
      err);
  if (retval < 0)
  {
    return -1;
  } else
  {
    gt_assert(retval > 0);
    return retval;
  }
}

static void gt_randomcodes_handle_tmp(GtRandomcodesinfo *fci,
                                     GtSuftabparts_rc *suftabparts_rc)
{
  gt_assert(fci->mappedleftborder != NULL);
  gt_Sfxmappedrange_usetmp(fci->mappedleftborder,
                           gt_randomcodes_outfilenameleftborder(&fci->tab),
                           (void **)
                             gt_randomcodes_leftborder_address(&fci->tab),
                           gt_randomcodes_leftborder_entries(&fci->tab),
                           true);
  if (gt_suftabparts_rc_numofparts(suftabparts_rc) > 1U)
  {
    gt_assert(fci->allrandomcodes != NULL);
    gt_assert(fci->mappedallrandomcodes != NULL);
    gt_Sfxmappedrange_storetmp_ulong(fci->mappedallrandomcodes,
                                     &fci->allrandomcodes,
                                     false);
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"allrandomcodes");
    gt_assert(fci->allrandomcodes == NULL);
    gt_marksubstring_bits_null(fci->buf.markprefix,false);
    gt_assert(fci->mappedmarkprefix != NULL);
    gt_Sfxmappedrange_storetmp_bitsequence(fci->mappedmarkprefix,
                                           gt_marksubstring_bits_address(
                                                fci->buf.markprefix),
                                           false);
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"markprefix");
    gt_marksubstring_bits_null(fci->buf.markprefix,true);
  } else
  {
    gt_Sfxmappedrange_delete(fci->mappedallrandomcodes);
    fci->mappedallrandomcodes = NULL;
    gt_Sfxmappedrange_delete(fci->mappedmarkprefix);
    fci->mappedmarkprefix = NULL;
  }
}

static void gt_randomcodes_allocsize_for_insertion(GtRandomcodesinfo *fci,
                                                  unsigned long maximumspace,
                                                  const GtSuftabparts_rc
                                                    *suftabparts_rc,
                                                  unsigned long phase2extra)
{
  if (maximumspace > 0)
  {
    const unsigned long maxrounds = 400UL;
    size_t used = gt_firstcodes_spacelog_workspace(fci->fcsl) +
                  phase2extra +
                  gt_suftabparts_rc_largestsizemappedpartwise(suftabparts_rc);

    if ((unsigned long) used < maximumspace)
    {
      fci->buf.allocated = gt_radixsort_max_num_of_entries_ulongpair(
                                                (size_t) maximumspace - used);
    } else
    {
      fci->buf.allocated /= 4UL;
    }
    if ((unsigned long) (fci->codebuffer_total+fci->numofsequences)/
                        fci->buf.allocated > maxrounds)
    {
      fci->buf.allocated
        = (fci->codebuffer_total+fci->numofsequences)/maxrounds;
    }
  } else
  {
    fci->buf.allocated /= 2UL;
  }
}

static int gt_randomcodes_process_part(GtRandomcodesinfo *fci,
                                      const GtEncseq *encseq,
                                      GtReadmode readmode,
                                      unsigned int kmersize,
                                      unsigned int minmatchlength,
                                      const GtSuftabparts_rc *suftabparts_rc,
                                      unsigned int part,
                                      unsigned long maximumspace,
#ifndef GT_THREADS_ENABLED
                                      GT_UNUSED
#endif
                                      unsigned int threads,
                                      unsigned long suftabentries,
                                      bool withsuftabcheck,
                                      GtShortreadsortworkinfo **srswtab,
                                      GtRandomcodesintervalprocess itvprocess,
                                      GtRandomcodesintervalprocess_end
                                        itvprocess_end,
                                      void *itvprocessdatatab,
                                      GtLogger *logger,
                                      GtTimer *timer,
                                      GtError *err)
{
  unsigned long spaceforbucketprocessing = 0;
  void *mapptr;
  bool haserr = false;

  if (timer != NULL)
  {
    gt_timer_show_progress(timer, "to insert suffixes into buckets",stdout);
  }
  fci->widthofpart = gt_suftabparts_rc_widthofpart(part,suftabparts_rc);
  gt_logger_log(logger,"compute part %u (%.2f%% of all candidates)",part,
               (double) 100.0 * fci->widthofpart/suftabentries);
  fci->currentminindex = gt_suftabparts_rc_minindex(part,suftabparts_rc);
  fci->currentmaxindex = gt_suftabparts_rc_maxindex(part,suftabparts_rc);
  if (fci->mappedallrandomcodes != NULL)
  {
    fci->allrandomcodes
      = (unsigned long *)
        gt_Sfxmappedrange_map(fci->mappedallrandomcodes,
                              fci->currentminindex,
                              fci->currentmaxindex);
    GT_FCI_ADDSPLITSPACE(fci->fcsl,"allrandomcodes",
                         (size_t) gt_Sfxmappedrange_size_mapped(
                                        fci->mappedallrandomcodes,
                                        fci->currentminindex,
                                        fci->currentmaxindex));
  }
  gt_assert(fci->mappedleftborder != NULL);
  mapptr = gt_Sfxmappedrange_map(fci->mappedleftborder,
                                 fci->currentminindex,
                                 fci->currentmaxindex);
  gt_randomcodes_leftborder_remap(&fci->tab,(uint32_t *) mapptr);
  GT_FCI_ADDSPLITSPACE(fci->fcsl,"leftborder",
                       (size_t) gt_Sfxmappedrange_size_mapped(
                                               fci->mappedleftborder,
                                               fci->currentminindex,
                                               fci->currentmaxindex));
  if (fci->mappedmarkprefix != NULL)
  {
    mapptr = gt_Sfxmappedrange_map(fci->mappedmarkprefix,
                                   fci->currentminindex,
                                   fci->currentmaxindex);

    gt_marksubstring_bits_map(fci->buf.markprefix, (GtBitsequence *) mapptr);
    GT_FCI_ADDSPLITSPACE(fci->fcsl,"markprefix",
                         (size_t) gt_Sfxmappedrange_size_mapped(
                                               fci->mappedmarkprefix,
                                               fci->currentminindex,
                                               fci->currentmaxindex));
  }
  gt_logger_log(logger,"maximum space for part %u: %.2f MB",
                part,GT_MEGABYTES(gt_firstcodes_spacelog_total(fci->fcsl)));
  fci->buf.currentmincode = gt_randomcodes_idx2code(fci,fci->currentminindex);
  fci->buf.currentmaxcode = gt_randomcodes_idx2code(fci,fci->currentmaxindex);
  gt_spmsuftab_partoffset(fci->spmsuftab,
                          gt_suftabparts_rc_offset(part,suftabparts_rc));
  gt_randomcodes_insert_runkmerscan(encseq,
                                   kmersize,
                                   minmatchlength,
                                   &fci->buf);
  gt_randomcodes_insertsuffixes_flush(fci);
  if (fci->mappedmarkprefix != NULL)
  {
    gt_Sfxmappedrange_unmap(fci->mappedmarkprefix);
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"markprefix");
  }
  if (fci->mappedallrandomcodes != NULL)
  {
    gt_Sfxmappedrange_unmap(fci->mappedallrandomcodes);
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"allrandomcodes");
  }
  if (part == gt_suftabparts_rc_numofparts(suftabparts_rc) - 1)
  {
    gt_firstcode_delete_before_end(fci);
  }
  if (timer != NULL)
  {
    gt_timer_show_progress(timer, "to sort buckets of suffixes",stdout);
  }
  if (maximumspace > 0)
  {
    if ((unsigned long) gt_firstcodes_spacelog_total(fci->fcsl)
        < maximumspace)
    {
      spaceforbucketprocessing = maximumspace -
                                 (unsigned long)
                                 gt_firstcodes_spacelog_total(fci->fcsl);
      gt_log_log("space left for sortremaining: %.2f",
                 GT_MEGABYTES(spaceforbucketprocessing));
    } else
    {
      spaceforbucketprocessing = 0;
    }
  }
#ifdef GT_THREADS_ENABLED
  if (threads > 1U)
  {
    if (gt_randomcodes_thread_sortremaining(
          srswtab,
          encseq,
          readmode,
          fci->spmsuftab,
          fci->buf.snrp,
          &fci->tab,
          fci->currentminindex,
          fci->currentmaxindex,
          gt_suftabparts_rc_widthofpart(part,suftabparts_rc),
          gt_suftabparts_rc_sumofwidth(part,suftabparts_rc),
          spaceforbucketprocessing,
          (unsigned long) kmersize,
          itvprocess,
          itvprocess_end,
          itvprocessdatatab,
          withsuftabcheck,
          threads,
          logger,
          NULL) != 0)
    {
      haserr = true;
    }
  } else
#endif
  {
    if (gt_randomcodes_sortremaining(srswtab[0],
          encseq,
          readmode,
          fci->spmsuftab,
          fci->buf.snrp,
          &fci->tab,
          fci->currentminindex,
          fci->currentmaxindex,
          gt_suftabparts_rc_sumofwidth(part,suftabparts_rc),
          spaceforbucketprocessing,
          (unsigned long) kmersize,
          itvprocess,
          itvprocess_end,
          itvprocessdatatab == NULL
          ? NULL
          : ((void **) itvprocessdatatab)[0],
          withsuftabcheck,
          err) != 0)
    {
      haserr = true;
    }
  }
  if (fci->mappedleftborder != NULL)
  {
    gt_Sfxmappedrange_unmap(fci->mappedleftborder);
    GT_FCI_SUBTRACTSPLITSPACE(fci->fcsl,"leftborder");
  }
  return haserr ? -1 : 0;
}

int storerandomcodes_getencseqkmers_twobitencoding(const GtEncseq *encseq,
    unsigned int kmersize,
    unsigned int numofparts,
    unsigned long maximumspace,
    unsigned int minmatchlength,
    bool withsuftabcheck,
    bool onlyaccumulation,
    bool onlyallrandomcodes,
    GT_UNUSED
    unsigned int addbscache_depth,
    unsigned long phase2extra,
    GT_UNUSED bool radixsmall,      /* set to true */
    GT_UNUSED unsigned int radixparts, /* set to 2U */
    GtRandomcodesintervalprocess
    itvprocess,
    GtRandomcodesintervalprocess_end
    itvprocess_end,
    void *itvprocessdatatab,
    GtLogger *logger,
    GtError *err)
{
  GtTimer *timer = NULL;
  GtRandomcodesinfo fci;
  size_t suftab_size = 0;
  unsigned int part, threadcount;
  unsigned long maxbucketsize, suftabentries = 0, largest_width,
                totallength = gt_encseq_total_length(encseq),
                maxseqlength = gt_encseq_max_seq_length(encseq);
  GtSfxmappedrangelist *sfxmrlist = NULL;
  GtSuftabparts_rc *suftabparts_rc = NULL;
  GtShortreadsortworkinfo **srswtab = NULL;
  const GtReadmode readmode = GT_READMODE_FORWARD;
  bool haserr = false;
#ifdef GT_THREADS_ENABLED
  const unsigned int threads = gt_jobs;
#else
  const unsigned int threads = 1U;
#endif

  if (maxseqlength < (unsigned long) minmatchlength)
  {
    return 0;
  }
  if (gt_randomcodes_init(&fci,
                         encseq,
                         kmersize,
                         withsuftabcheck,
                         minmatchlength,
                         err) != 0)
  {
    haserr = true;
  }
  if (!haserr)
  {
    sfxmrlist = gt_Sfxmappedrangelist_new();
    if (gt_showtime_enabled())
    {
      timer = gt_timer_new_with_progress_description("to collect initial "
                                                     "prefixes");
      gt_timer_start(timer);
    }
    gt_randomcodes_collectcodes(&fci,
                               encseq,
                               readmode,
                               kmersize,
                               minmatchlength,
                               logger,
                               timer);
    if (fci.differentcodes > 0 && onlyallrandomcodes)
    {
      run_allcodes_distribution(fci.allrandomcodes,fci.differentcodes);
      gt_free(fci.allrandomcodes);
      fci.allrandomcodes = NULL;
      gt_marksubstring_delete(fci.buf.markprefix,true);
      gt_marksubstring_delete(fci.buf.marksuffix,true);
      gt_randomcodes_countocc_delete(fci.fcsl,&fci.tab);
      gt_firstcodes_spacelog_delete(fci.fcsl);
      gt_seqnumrelpos_delete(fci.buf.snrp);
      gt_Sfxmappedrangelist_delete(sfxmrlist);
      if (timer != NULL)
      {
        gt_timer_delete(timer);
      }
      return 0;
    }
    fci.flushcount = 0;
    fci.codebuffer_total = 0;
    gt_randomcodes_fillbinsearchcache(&fci,addbscache_depth);
    if (gt_randomcodes_allocspace(&fci,
                                 numofparts,
                                 maximumspace,
                                 phase2extra,
                                 err) != 0)
    {
      haserr = true;
    }
  }
  fci.buf.nextfree = 0;
  if (!haserr)
  {
    gt_randomcodes_accumulatecounts_run(&fci,
                                       encseq,
                                       kmersize,
                                       minmatchlength,
                                       logger,
                                       timer);
    suftabentries = fci.firstcodehits + fci.numofsequences;
    maxbucketsize = gt_randomcodes_partialsums(fci.fcsl,&fci.tab,suftabentries);
    gt_logger_log(logger,"maximum space after computing partial sums: %.2f MB",
                  GT_MEGABYTES(gt_firstcodes_spacelog_total(fci.fcsl)));
    gt_logger_log(logger,"maxbucketsize=%lu",maxbucketsize);
    gt_randomcodes_map_sections(&fci,sfxmrlist);
    if (numofparts == 0 || maximumspace > 0)
    {
      int retval = gt_randomcodes_auto_parts(&fci,
                                            sfxmrlist,
                                            numofparts,
                                            &maximumspace,
                                            maxbucketsize,
                                            kmersize,
                                            totallength,
                                            maxseqlength,
                                            suftabentries,
                                            phase2extra,
                                            err);
      if (retval < 0)
      {
        haserr = true;
      } else
      {
        numofparts = (unsigned int) retval;
      }
    }
  }
  if (!haserr)
  {
    gt_assert(numofparts > 0);
    suftabparts_rc = gt_suftabparts_rc_new(numofparts,
                                     NULL,
                                     &fci.tab,
                                     sfxmrlist,
                                     suftabentries,
                                     0,
                                     logger);
    gt_assert(suftabparts_rc != NULL);
    gt_suftabparts_rc_showallrecords(suftabparts_rc,true);
    gt_assert(fci.allrandomcodes[fci.differentcodes - 1] ==
              gt_randomcodes_idx2code(&fci,
                gt_suftabparts_rc_maxindex_last(suftabparts_rc))
             );
    gt_Sfxmappedrangelist_delete(sfxmrlist);
    sfxmrlist = NULL;
    gt_randomcodes_samples_delete(fci.fcsl,&fci.tab);
    gt_assert(fci.buf.nextfree == 0);
    gt_randomcodes_handle_tmp(&fci,suftabparts_rc);
    largest_width = gt_suftabparts_rc_largest_width(suftabparts_rc);
    fci.spmsuftab = gt_spmsuftab_new(largest_width,
                                     totallength,
                                     fci.bitsforposref,
                                     logger);
    suftab_size = gt_spmsuftab_requiredspace(largest_width,totallength,
                                             fci.bitsforposref);
    GT_FCI_ADDWORKSPACE(fci.fcsl,"suftab",suftab_size);
    fci.buf.flush_function = gt_randomcodes_insertsuffixes_flush;
    srswtab = gt_malloc(sizeof (*srswtab) * threads);
    for (threadcount = 0; threadcount < threads; threadcount++)
    {
      srswtab[threadcount]
        = gt_shortreadsort_new(maxbucketsize,maxseqlength - kmersize,
                               readmode,true,false);
    }
    GT_FCI_ADDWORKSPACE(fci.fcsl,"shortreadsort",
                        threads * gt_shortreadsort_size(true,maxbucketsize,
                                                        maxseqlength-kmersize));
    gt_randomcodes_allocsize_for_insertion(&fci,
                                          maximumspace,
                                          suftabparts_rc,
                                          phase2extra);
    if (!onlyaccumulation)
    {
      fci.radixsort_codepos = gt_radixsort_new_ulongpair(fci.buf.allocated);
      GT_FCI_ADDWORKSPACE(fci.fcsl,"radixsort_codepos",
                          gt_radixsort_size(fci.radixsort_codepos));
      fci.buf.spaceGtUlongPair
        = gt_radixsort_space_ulongpair(fci.radixsort_codepos);
    }
    fci.codebuffer_total = 0;
    fci.flushcount = 0;
    for (part = 0; !haserr && !onlyaccumulation &&
                   part < gt_suftabparts_rc_numofparts(suftabparts_rc); part++)
    {
      if (gt_randomcodes_process_part(&fci,
                                     encseq,
                                     readmode,
                                     kmersize,
                                     minmatchlength,
                                     suftabparts_rc,
                                     part,
                                     maximumspace,
                                     threads,
                                     suftabentries,
                                     withsuftabcheck,
                                     srswtab,
                                     itvprocess,
                                     itvprocess_end,
                                     itvprocessdatatab,
                                     logger,
                                     timer,
                                     err) != 0)
      {
        haserr = true;
      }
    }
  }
  if (timer != NULL)
  {
    gt_timer_show_progress(timer, "cleaning up",stdout);
  }
  if (!haserr)
  {
    GT_FCI_SUBTRACTWORKSPACE(fci.fcsl,"shortreadsort");
  }
  if (!haserr && !onlyaccumulation && srswtab != NULL)
  {
    unsigned long sumofstoredvalues = 0;

    for (threadcount=0; threadcount<threads; threadcount++)
    {
      sumofstoredvalues +=
        gt_shortreadsort_sumofstoredvalues(srswtab[threadcount]);
    }
    gt_logger_log(logger,"average short read depth is %.2f",
                      (double) sumofstoredvalues/fci.firstcodeposhits);
  }
  if (!haserr)
  {
    if (!onlyaccumulation)
    {
      gt_logger_log(logger,"firstcodeposhits=%lu (%.3f%% of all suffixes), "
                           "%u rounds (avg length %lu)",
                           fci.firstcodeposhits,
                           100.0 * (double) fci.firstcodeposhits/totallength,
                           fci.flushcount,
                           fci.codebuffer_total/fci.flushcount);
      gt_assert(fci.firstcodeposhits == suftabentries);
    } else
    {
      gt_firstcode_delete_before_end(&fci);
    }
  }
  if (srswtab != NULL)
  {
    for (threadcount=0; threadcount<threads; threadcount++)
    {
      gt_shortreadsort_delete(srswtab[threadcount]);
    }
    gt_free(srswtab);
  }
  if (haserr)
  {
    gt_firstcode_delete_before_end(&fci);
    gt_free(fci.allrandomcodes);
    fci.allrandomcodes = NULL;
    gt_Sfxmappedrangelist_delete(sfxmrlist);
    fci.buf.spaceGtUlong = NULL;
    gt_radixsort_delete(fci.radixsort_code);
  }
  gt_suftabparts_rc_delete(suftabparts_rc);
  gt_randomcodes_countocc_delete(fci.fcsl,&fci.tab);
  gt_randomcodes_tab_delete(fci.fcsl,&fci.tab);
  if (fci.spmsuftab != NULL)
  {
    gt_spmsuftab_delete(fci.spmsuftab);
    GT_FCI_SUBTRACTWORKSPACE(fci.fcsl,"suftab");
    fci.spmsuftab = NULL;
  }
  gt_Sfxmappedrange_delete(fci.mappedleftborder);
  if (fci.mappedallrandomcodes == NULL && fci.allrandomcodes != NULL)
  {
    GT_FCI_SUBTRACTSPLITSPACE(fci.fcsl,"allrandomcodes");
  }
  gt_Sfxmappedrange_delete(fci.mappedallrandomcodes);
  gt_seqnumrelpos_delete(fci.buf.snrp);
  gt_firstcodes_spacelog_stop_diff(fci.fcsl);
  GT_FCI_SUBTRACTWORKSPACE(fci.fcsl,"encseq");
  gt_firstcodes_spacelog_delete(fci.fcsl);
  if (timer != NULL)
  {
    gt_timer_show_progress_final(timer, stdout);
    gt_timer_delete(timer);
  }
  return haserr ? -1 : 0;
}