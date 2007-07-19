#include <math.h>

#include "fmindex.h"

#include "mapspecfm.pr"
#include "opensfxfile.pr"

static int writefmascii (const Str *indexname,
                         const Fmindex *fm,
                         bool storeindexpos,
                         Env *env)
{
  FILE *prjfp;

  if ((prjfp = opensfxfile (indexname, FMASCIIFILESUFFIX,"wb",env)) == NULL)
  {
    return -1;
  }
  fprintf (prjfp, "bwtlength=" FormatSeqpos "\n",
           PRINTSeqposcast(fm->bwtlength));
  fprintf (prjfp, "longest=" FormatSeqpos "\n",
                   PRINTSeqposcast(fm->longestsuffixpos));
  fprintf (prjfp, "storeindexpos=%d\n", storeindexpos ? 1 : 0);
  fprintf (prjfp, "log blocksize=%u\n", fm->log2bsize);
  fprintf (prjfp, "log markdistance=%u\n", fm->log2markdist);
  fprintf (prjfp, "specialcharacters=" 
                   FormatSeqpos " " 
                   FormatSeqpos " " 
                   FormatSeqpos " " 
                   FormatSeqpos "\n",
                  PRINTSeqposcast(fm->specialcharinfo.specialcharacters),
                  PRINTSeqposcast(fm->specialcharinfo.specialranges),
                  PRINTSeqposcast(fm->specialcharinfo.lengthofspecialprefix),
                  PRINTSeqposcast(fm->specialcharinfo.lengthofspecialsuffix));
  fprintf (prjfp, "suffixlength=%u\n",fm->suffixlength);
  env_fa_xfclose(prjfp, env);
  return 0;
}

static int writefmdata (const Str *indexname,
                        Fmindex *fm, 
                        bool storeindexpos,
                        Env *env)
{
  FILE *fp;

  if ((fp = opensfxfile (indexname, FMDATAFILESUFFIX,"wb",env)) == NULL)
  {
    return -1;
  }
  if(flushfmindex2file(fp,fm,storeindexpos,env) != 0)
  {
    return -2;
  }
  env_fa_xfclose(fp, env);
  return 0;
}

int saveFmindex (const Str *indexname,Fmindex *fm,
                 bool storeindexpos,Env *env)
{
  if (writefmascii (indexname, fm, storeindexpos,env) != 0)
  {
    return -1;
  }
  if (writefmdata (indexname, fm, storeindexpos,env) != 0)
  {
    return -2;
  }
  return 0;
}
