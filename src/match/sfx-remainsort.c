/*
  Copyright (c) 2009 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2009 Center for Bioinformatics, University of Hamburg

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
#include <string.h>
#include "core/chardef.h"
#include "core/queue.h"
#include "core/chardef.h"
#include "core/minmax.h"
#include "core/unused_api.h"
#include "core/ma_api.h"
#include "core/qsort_r.h"
#include "divmodmul.h"
#include "seqpos-def.h"
#include "encseq-def.h"
#include "sfx-remainsort.h"
#include "stamp.h"

typedef struct
{
  Seqpos key,
         suffixstart;
} Itventry;

typedef struct
{
  Seqpos *left,
         *right;
} Pairsuffixptr;

typedef struct
{
  Seqpos *left,
         *right,
         depth;
  unsigned long count, totalwidth;
} Firstwithnewdepth;

struct Rmnsufinfo
{
  Seqpos *inversesuftab,
         *presortedsuffixes;
  GtQueue *rangestobesorted;
  Seqpos partwidth,
         totallength,
         currentdepth;
  const Encodedsequence *encseq;
  Readmode readmode;
  unsigned long allocateditvinfo,
                currentqueuesize,
                maxqueuesize;
  Itventry *itvinfo;
  Firstwithnewdepth firstwithnewdepth;
};

Rmnsufinfo *newRmnsufinfo(Seqpos *presortedsuffixes,
                          const Encodedsequence *encseq,
                          Readmode readmode,
                          Seqpos partwidth)
{
  Rmnsufinfo *rmnsufinfo;

  rmnsufinfo = gt_malloc(sizeof(Rmnsufinfo));
  rmnsufinfo->presortedsuffixes = presortedsuffixes;
  rmnsufinfo->rangestobesorted = gt_queue_new();
  rmnsufinfo->partwidth = partwidth;
  rmnsufinfo->totallength = getencseqtotallength(encseq);
  rmnsufinfo->encseq = encseq;
  rmnsufinfo->readmode = readmode;
  rmnsufinfo->allocateditvinfo = 0;
  rmnsufinfo->itvinfo = NULL;
  rmnsufinfo->currentqueuesize = 0;
  rmnsufinfo->maxqueuesize = 0;
  rmnsufinfo->firstwithnewdepth.depth = 0;
  rmnsufinfo->firstwithnewdepth.totalwidth = 0;
  rmnsufinfo->firstwithnewdepth.count = 0;
  rmnsufinfo->firstwithnewdepth.left = NULL;
  rmnsufinfo->firstwithnewdepth.right = NULL;
  rmnsufinfo->currentdepth = 0;
  return rmnsufinfo;
}

void addunsortedrange(Rmnsufinfo *rmnsufinfo,
                      Seqpos *left,Seqpos *right,Seqpos depth)
{
  Pairsuffixptr *pairptr;
  unsigned long width;

  gt_assert(left < right && depth > 0);
  gt_assert(rmnsufinfo->firstwithnewdepth.left == NULL ||
            (rmnsufinfo->firstwithnewdepth.depth > 0 &&
             rmnsufinfo->firstwithnewdepth.depth <= depth));
  if (rmnsufinfo->firstwithnewdepth.left == NULL ||
      rmnsufinfo->firstwithnewdepth.depth < depth)
  {
    if (rmnsufinfo->firstwithnewdepth.left != NULL)
    {
      printf("intervals in previous level=%lu (total=%lu,avg=%.2f,"
             "%.2f%% of all)\n",
              rmnsufinfo->firstwithnewdepth.count,
              rmnsufinfo->firstwithnewdepth.totalwidth,
              (double) rmnsufinfo->firstwithnewdepth.totalwidth/
                       rmnsufinfo->firstwithnewdepth.count,
              100.0 * (double) rmnsufinfo->firstwithnewdepth.totalwidth/
                               rmnsufinfo->totallength);
    }
    printf("enter new level with depth=" FormatSeqpos "\n",
            PRINTSeqposcast(depth));
    rmnsufinfo->firstwithnewdepth.left = left;
    rmnsufinfo->firstwithnewdepth.right = right;
    rmnsufinfo->firstwithnewdepth.depth = depth;
    rmnsufinfo->firstwithnewdepth.count = 1UL;
    rmnsufinfo->firstwithnewdepth.totalwidth
      = (unsigned long) (right - left + 1);
  } else
  {
    rmnsufinfo->firstwithnewdepth.count++;
    rmnsufinfo->firstwithnewdepth.totalwidth
      += (unsigned long) (right - left + 1);
  }
  width = (unsigned long) (right - left + 1);
  if (rmnsufinfo->allocateditvinfo < width)
  {
    gt_assert(rmnsufinfo->itvinfo == NULL);
    rmnsufinfo->allocateditvinfo = width;
  }
  pairptr = gt_malloc(sizeof(Pairsuffixptr));
  pairptr->left = left;
  pairptr->right = right;
  gt_queue_add(rmnsufinfo->rangestobesorted,pairptr);
  rmnsufinfo->currentqueuesize++;
  if (rmnsufinfo->maxqueuesize < rmnsufinfo->currentqueuesize)
  {
    rmnsufinfo->maxqueuesize = rmnsufinfo->currentqueuesize;
  }
}

static int compareitv(const void *a,const void *b)
{
  const Itventry *itva = (const Itventry *) a,
                 *itvb = (const Itventry *) b;

  if (itva->key < itvb->key)
  {
    return -1;
  }
  if (itva->key > itvb->key)
  {
    return 1;
  }
  return 0;
}

static void inverserange(Rmnsufinfo *rmnsufinfo,Seqpos *left,Seqpos *right)
{
  Seqpos *ptr, startindex;

  startindex = (Seqpos) (left - rmnsufinfo->presortedsuffixes);
  for (ptr = left; ptr <= right; ptr++)
  {
    rmnsufinfo->inversesuftab[*ptr] = startindex;
  }
}

static void sortitv(Rmnsufinfo *rmnsufinfo,Seqpos *left,Seqpos *right)
{
  Seqpos startindex;
  unsigned long idx, rangestart;
  const unsigned long width = (unsigned long) (right - left + 1);

  if (rmnsufinfo->firstwithnewdepth.left == left &&
      rmnsufinfo->firstwithnewdepth.right == right)
  {
    rmnsufinfo->currentdepth = rmnsufinfo->firstwithnewdepth.depth;
  }
  gt_assert(rmnsufinfo->allocateditvinfo >= width);
  for (idx=0; idx<width; idx++)
  {
    rmnsufinfo->itvinfo[idx].suffixstart = left[idx];
    if (left[idx]+rmnsufinfo->currentdepth > rmnsufinfo->totallength)
    {
      fprintf(stderr,"left[%lu]+depth=%lu+%lu=%lu>%lu\n",
              idx,
              (unsigned long) left[idx],
              (unsigned long) rmnsufinfo->currentdepth,
              (unsigned long) (left[idx]+rmnsufinfo->currentdepth),
              (unsigned long) rmnsufinfo->totallength);
      exit(EXIT_FAILURE); /* programming error */
    }
    rmnsufinfo->itvinfo[idx].key
      = rmnsufinfo->inversesuftab[left[idx]+rmnsufinfo->currentdepth];
  }
  qsort(rmnsufinfo->itvinfo,(size_t) width,sizeof(Itventry),compareitv);
  for (idx=0; idx<width; idx++)
  {
    left[idx] = rmnsufinfo->itvinfo[idx].suffixstart;
  }
  rangestart = 0;
  startindex = (Seqpos) (left - rmnsufinfo->presortedsuffixes);
  for (idx=1UL; idx<width; idx++)
  {
    if (rmnsufinfo->itvinfo[idx-1].key != rmnsufinfo->itvinfo[idx].key)
    {
      if (rangestart + 1 < idx)
      {
        addunsortedrange(rmnsufinfo,
                         left + rangestart,
                         left + idx - 1,
                         MULT2(rmnsufinfo->currentdepth));
        inverserange(rmnsufinfo,
                     left + rangestart,
                     left + idx - 1);
      } else
      {
        rmnsufinfo->inversesuftab[left[rangestart]] = startindex+rangestart;
      }
      rangestart = idx;
    }
  }
  if (rangestart + 1 < width)
  {
    addunsortedrange(rmnsufinfo,
                     left + rangestart,
                     left + width - 1,
                     MULT2(rmnsufinfo->currentdepth));
    inverserange(rmnsufinfo,
                 left + rangestart,
                 left + width - 1);
  } else
  {
    rmnsufinfo->inversesuftab[left[rangestart]] = startindex+rangestart;
  }
}

static int putleftbound(void **elem,void *info, GT_UNUSED GtError *err)
{
  Pairsuffixptr *pairptr = *(Pairsuffixptr**) elem;

  inverserange((Rmnsufinfo *) info,pairptr->left,pairptr->right);
  return 0;
}

static void sortremainingsuffixes(Rmnsufinfo *rmnsufinfo)
{
  Pairsuffixptr *pairptr;
  Seqpos idx;

  printf("# countovermaxdepth=%lu\n",
           gt_queue_size(rmnsufinfo->rangestobesorted));
  rmnsufinfo->inversesuftab
    = gt_malloc(sizeof(Seqpos) * (rmnsufinfo->totallength+1));
  for (idx=0; idx < rmnsufinfo->partwidth; idx++)
  {
    rmnsufinfo->inversesuftab[rmnsufinfo->presortedsuffixes[idx]] = idx;
  }
  if (hasspecialranges(rmnsufinfo->encseq))
  {
    Specialrangeiterator *sri;
    Sequencerange range;
    Seqpos specialidx;

    sri = newspecialrangeiterator(rmnsufinfo->encseq,
                                  ISDIRREVERSE(rmnsufinfo->readmode)
                                  ? false : true);
    specialidx = rmnsufinfo->partwidth;
    while (nextspecialrangeiterator(&range,sri))
    {
      for (idx = range.leftpos; idx < range.rightpos; idx++)
      {
        rmnsufinfo->inversesuftab[idx] = specialidx++;
      }
    }
    gt_assert(specialidx == rmnsufinfo->totallength);
    freespecialrangeiterator(&sri);
  }
  /* now we can unmap the encoded sequence */
  rmnsufinfo->inversesuftab[rmnsufinfo->totallength] = rmnsufinfo->totallength;
  (void) gt_queue_iterate(rmnsufinfo->rangestobesorted,
                          putleftbound,
                          rmnsufinfo,
                          NULL);
  rmnsufinfo->itvinfo
    = gt_malloc(sizeof(Itventry) * (rmnsufinfo->allocateditvinfo));
  gt_assert(rmnsufinfo->currentqueuesize  ==
            gt_queue_size(rmnsufinfo->rangestobesorted));
  while (gt_queue_size(rmnsufinfo->rangestobesorted) > 0)
  {
    pairptr = gt_queue_get(rmnsufinfo->rangestobesorted);
    gt_assert(rmnsufinfo->currentqueuesize > 0);
    rmnsufinfo->currentqueuesize--;
    sortitv(rmnsufinfo,
            pairptr->left,
            pairptr->right);
    gt_free(pairptr);
  }
  printf("maxqueuesize = %lu\n",rmnsufinfo->maxqueuesize);
}

Seqpos *lcp13_manzini(const Rmnsufinfo *rmnsufinfo)
{
  Seqpos pos, lcpvalue = 0, *lcptab;

  lcptab = gt_malloc(sizeof(Seqpos) * rmnsufinfo->partwidth);
  lcptab[0] = 0;
  for (pos=0; pos <= rmnsufinfo->totallength; pos++)
  {
    Seqpos fillpos = rmnsufinfo->inversesuftab[pos];
    if (fillpos > 0 && fillpos < rmnsufinfo->partwidth)
    {
      Seqpos previousstart = rmnsufinfo->presortedsuffixes[fillpos-1];
      while (pos+lcpvalue < rmnsufinfo->totallength &&
             previousstart+lcpvalue < rmnsufinfo->totallength)
      {
        Uchar cc1, cc2;

        cc1 = getencodedchar(rmnsufinfo->encseq,pos+lcpvalue,
                             rmnsufinfo->readmode);
        cc2 = getencodedchar(rmnsufinfo->encseq,previousstart+lcpvalue,
                             rmnsufinfo->readmode);
        if (cc1 == cc2 && ISNOTSPECIAL(cc1))
        {
          lcpvalue++;
        } else
        {
          break;
        }
      }
      lcptab[fillpos] = lcpvalue;
    }
    if (lcpvalue > 0)
    {
      lcpvalue--;
    }
  }
  return lcptab;
}

static unsigned long *computeocclesstab(const Rmnsufinfo *rmnsufinfo)
{
  unsigned long *occless, numofchars, idx;

  numofchars = (unsigned long) getencseqAlphabetnumofchars(rmnsufinfo->encseq);
  occless = gt_malloc(sizeof (unsigned long) * numofchars);
  occless[0] = 0;
  for (idx = 1UL; idx < numofchars; idx++)
  {
    occless[idx] = occless[idx-1] +
                   getencseqcharactercount(rmnsufinfo->encseq,(Uchar) (idx-1));
  }
  return occless;
}

typedef struct
{
  Seqpos lowerbound,
         upperbound,
         rank;
} Rankedbounds;

static Rankedbounds *fillrankbounds(const Rmnsufinfo *rmnsufinfo)
{
  if (hasspecialranges(rmnsufinfo->encseq))
  {
    Specialrangeiterator *sri;
    Sequencerange range;
    Seqpos currentrank = 0, realspecialranges;
    Rankedbounds *rankedbounds, *rbptr;

    realspecialranges = getencseqrealspecialranges(rmnsufinfo->encseq);
    rankedbounds = gt_malloc(sizeof(Rankedbounds) * realspecialranges);
    sri = newspecialrangeiterator(rmnsufinfo->encseq,
                                  ISDIRREVERSE(rmnsufinfo->readmode)
                                  ? false : true);
    for (rbptr = rankedbounds; nextspecialrangeiterator(&range,sri); rbptr++)
    {
      rbptr->lowerbound = range.leftpos;
      rbptr->upperbound = range.rightpos;
      rbptr->rank = currentrank;
      currentrank += rbptr->upperbound - rbptr->lowerbound;
    }
    gt_assert(rbptr == rankedbounds + realspecialranges);
    freespecialrangeiterator(&sri);
    return rankedbounds;
  }
  return NULL;
}

static Seqpos frompos2rank(const Rankedbounds *leftptr,
                           const Rankedbounds *rightptr,
                           Seqpos specialpos)
{
  const Rankedbounds *midptr;

  while (leftptr <= rightptr)
  {
    midptr = leftptr + DIV2((unsigned long) (rightptr-leftptr));
    if (specialpos < midptr->lowerbound)
    {
      rightptr = midptr-1;
    } else
    {
      if (specialpos >= midptr->upperbound)
      {
        leftptr = midptr + 1;
      } else
      {
        return midptr->rank + specialpos - midptr->lowerbound;
      }
    }
  }
  fprintf(stderr,"frompos2rank: cannot find pos " FormatSeqpos
                 " in ranges",PRINTSeqposcast(specialpos));
  exit(EXIT_FAILURE); /* programming error */
  /*@ignore@*/
  return 0;
  /*@end@*/
}

#ifdef WIHTRANK2POS
static Seqpos fromrank2pos(const Rankedbounds *leftptr,
                           const Rankedbounds *rightptr,
                           Seqpos rank)
{
  const Rankedbounds *midptr;

  while (leftptr <= rightptr)
  {
    midptr = leftptr + DIV2((unsigned long) (rightptr-leftptr));
    if (rank < midptr->rank)
    {
      rightptr = midptr-1;
    } else
    {
      if (rank >= midptr->rank + (midptr->upperbound - midptr->lowerbound))
      {
        leftptr = midptr + 1;
      } else
      {
        return midptr->lowerbound + (rank - midptr->rank);
      }
    }
  }
  fprintf(stderr,"fromrank2rank: cannot find rank " FormatSeqpos
                 " in ranges",PRINTSeqposcast(rank));
  exit(EXIT_FAILURE); /* programming error */
  /*@ignore@*/
  return 0;
  /*@end@*/
}
#endif

typedef struct
{
  Seqpos specialrank,
         key;
} Rankwithpos;

static int compareRankwithpos(const void *a,const void *b)
{
  const Rankwithpos *aptr = (const Rankwithpos *) a,
                    *bptr = (const Rankwithpos *) b;

  if (aptr->key < bptr->key)
  {
    return -1;
  }
  if (aptr->key > bptr->key)
  {
    return 1;
  }
  gt_assert(false);
  return 0;
}

static Rankwithpos *fillrankwithpos(const Rmnsufinfo *rmnsufinfo)
{
  if (hasspecialranges(rmnsufinfo->encseq))
  {
    Specialrangeiterator *sri;
    Sequencerange range;
    Seqpos idx, specialcharacters;
    Rankwithpos *rankwithpos, *rbptr;

    specialcharacters = getencseqspecialcharacters(rmnsufinfo->encseq);
    rankwithpos = gt_malloc(sizeof(Rankwithpos) * specialcharacters);
    sri = newspecialrangeiterator(rmnsufinfo->encseq,
                                  ISDIRREVERSE(rmnsufinfo->readmode)
                                  ? false : true);
    rbptr = rankwithpos;
    while (nextspecialrangeiterator(&range,sri))
    {
      for (idx = range.leftpos; idx < range.rightpos; idx++)
      {
        gt_assert(idx+1<=rmnsufinfo->totallength);
        /*
        printf("fill at %lu: key = %lu\n",(unsigned long) (rbptr - rankwithpos),
                                          (unsigned long)
                                           rmnsufinfo->inversesuftab[idx+1]);
        */
        gt_assert(rbptr < rankwithpos + specialcharacters);
        rbptr->specialrank = (Seqpos) (rbptr - rankwithpos);
        rbptr->key = rmnsufinfo->inversesuftab[idx+1];
        rbptr++;
      }
    }
    gt_assert(rbptr == rankwithpos + specialcharacters);
    freespecialrangeiterator(&sri);
    qsort(rankwithpos,(size_t) specialcharacters,
          sizeof (Rankwithpos),compareRankwithpos);
    return rankwithpos;
  }
  return NULL;
}

static void updateranknext(Rmnsufinfo *rmnsufinfo,
                           Seqpos *ranknext,
                           Rankwithpos *rankwithpos,
                           unsigned long *rankwithposindex,
                           unsigned long *occless,
                           const Rankedbounds *rankedbounds,
                           Seqpos realspecialranges,
                           Seqpos pos,
                           Seqpos idx)
{
  Uchar cc = getencodedchar(rmnsufinfo->encseq,pos,rmnsufinfo->readmode);

  if (ISNOTSPECIAL(cc))
  {
    gt_assert(occless[cc] < (unsigned long) rmnsufinfo->partwidth);
    ranknext[occless[cc]++] = idx;
  } else
  {
    Seqpos rank;

    rank = rmnsufinfo->partwidth +
           frompos2rank(rankedbounds,
                        rankedbounds + realspecialranges,
                        pos);
    gt_assert(rank <= rmnsufinfo->totallength);
    ranknext[rank] = idx;
    gt_assert (rank == rmnsufinfo->partwidth +
               rankwithpos[*rankwithposindex].specialrank);
    (*rankwithposindex)++;
  }
}

#ifdef WITHcomparewithsuffixarray
static int comparewithsuffixarray(const void *a,const void *b,void *data)
{
  const Rmnsufinfo *rmnsufinfo = (const Rmnsufinfo *) data;
  Seqpos aval = *((Seqpos *) a), bval = *((Seqpos *) b);

  if (aval < rmnsufinfo->partwidth)
  {
    if (bval < rmnsufinfo->partwidth)
    {
      if (rmnsufinfo->presortedsuffixes[aval] <
          rmnsufinfo->presortedsuffixes[bval])
      {
        return -1; /* a < b */
      }
      if (rmnsufinfo->presortedsuffixes[aval] >
          rmnsufinfo->presortedsuffixes[bval])
      {
        return 1; /* a > b */
      }
      gt_assert(false);
    } else
    {
      return -1; /* a < b */
    }
  } else
  {
    if (bval < rmnsufinfo->partwidth)
    {
      return 1; /* a > b */
    }
    if (aval < bval)
    {
      return -1; /* a < b */
    }
    if (aval > bval)
    {
      return 1; /* a > b */
    }
    gt_assert(false);
  }
  gt_assert(false);
  return 0;
}
#endif

static Seqpos sa2ranknext(Seqpos *ranknext,Rmnsufinfo *rmnsufinfo)
{
  Seqpos idx, longest = 0, realspecialranges;
  unsigned long *occless,
                rankwithposindex;
  Rankwithpos *rankwithpos;
  Rankedbounds *rankedbounds;

  gt_assert(rmnsufinfo->partwidth > 0);
  occless = computeocclesstab(rmnsufinfo);
  realspecialranges = getencseqrealspecialranges(rmnsufinfo->encseq);
  rankwithpos = fillrankwithpos(rmnsufinfo);
  /* now inveresuftab is not used any more, and thus the
     ranknext array (which points to ranknext can savely be stored */
  rankedbounds = fillrankbounds(rmnsufinfo);
  rankwithposindex = 0;
  for (idx=0; idx < rmnsufinfo->partwidth; idx++)
  {
    if (rmnsufinfo->presortedsuffixes[idx] > 0)
    {
      updateranknext(rmnsufinfo,ranknext,rankwithpos,&rankwithposindex,
                     occless,rankedbounds,realspecialranges,
                     rmnsufinfo->presortedsuffixes[idx]-1,idx);
    } else
    {
      longest = idx;
    }
  }
  if (hasspecialranges(rmnsufinfo->encseq))
  {
    Specialrangeiterator *sri;
    Sequencerange range;
    Seqpos specialidx, specialpos;

    sri = newspecialrangeiterator(rmnsufinfo->encseq,
                                  ISDIRREVERSE(rmnsufinfo->readmode)
                                  ? false : true);
    specialidx = rmnsufinfo->partwidth;
    while (nextspecialrangeiterator(&range,sri))
    {
      for (specialpos = range.leftpos; specialpos < range.rightpos;
           specialpos++)
      {
        if (specialpos > 0)
        {
          updateranknext(rmnsufinfo,ranknext,rankwithpos,&rankwithposindex,
                         occless,rankedbounds,
                         realspecialranges,specialpos-1,specialidx);
        } else
        {
          longest = idx;
        }
        specialidx++;
      }
    }
    freespecialrangeiterator(&sri);
  }
  gt_free(rankedbounds);
  gt_free(rankwithpos);
  gt_free(occless);
  return longest;
}

static Seqpos *lcp9_manzini(Rmnsufinfo *rmnsufinfo)
{
  Seqpos pos, previousstart, nextfillpos, fillpos, lcpvalue = 0, *lcptab,
         *ranknext;

  ranknext = rmnsufinfo->inversesuftab;
  fillpos = sa2ranknext(ranknext,rmnsufinfo);
  printf("longest=%lu\n",(unsigned long) fillpos);
  lcptab = rmnsufinfo->inversesuftab;
  /* now ranknext and lcptab point to the same memory area. After reading
     ranknext at position fillpos, the same cell is used for storing
     the determined lcp-value */
  for (pos = 0; pos < rmnsufinfo->totallength; pos++)
  {
    nextfillpos = ranknext[fillpos];
    if (fillpos > 0 && fillpos - 1 < rmnsufinfo->partwidth)
    {
      previousstart = rmnsufinfo->presortedsuffixes[fillpos-1];
      while (pos+lcpvalue < rmnsufinfo->totallength &&
             previousstart+lcpvalue < rmnsufinfo->totallength)
      {
        Uchar cc1, cc2;

        cc1 = getencodedchar(rmnsufinfo->encseq,pos+lcpvalue,
                             rmnsufinfo->readmode);
        cc2 = getencodedchar(rmnsufinfo->encseq,previousstart+lcpvalue,
                             rmnsufinfo->readmode);
        if (cc1 == cc2 && ISNOTSPECIAL(cc1))
        {
          lcpvalue++;
        } else
        {
          break;
        }
      }
      lcptab[fillpos] = lcpvalue;
      if (lcpvalue > 0)
      {
        lcpvalue--;
      }
    }
    fillpos = nextfillpos;
  }
  rmnsufinfo->inversesuftab = NULL;
  return lcptab;
}

Seqpos *wrapRmnsufinfo(Rmnsufinfo **rmnsufinfoptr,bool withlcptab)
{
  Rmnsufinfo *rmnsufinfo = *rmnsufinfoptr;
  Seqpos *lcptab;

  sortremainingsuffixes(rmnsufinfo);
  if (withlcptab)
  {
    lcptab = lcp9_manzini(rmnsufinfo);
  } else
  {
    lcptab = NULL;
  }
  gt_free(rmnsufinfo->itvinfo);
  rmnsufinfo->itvinfo = NULL;
  gt_queue_delete(rmnsufinfo->rangestobesorted);
  rmnsufinfo->rangestobesorted = NULL;
  gt_free(rmnsufinfo->inversesuftab);
  rmnsufinfo->inversesuftab = NULL;
  gt_free(rmnsufinfo);
  rmnsufinfoptr = NULL;
  return lcptab;
}
