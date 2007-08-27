/*
  Copyright (c) 2007 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg
  See LICENSE file or http://genometools.org/license.html for license details.
*/

#include <limits.h>
#include "sarr-def.h"
#include "seqpos-def.h"
#include "symboldef.h"
#include "spacedef.h"

#define ABOVETOP  stackspace[nextfreeItvinfo]
#define TOP       stackspace[nextfreeItvinfo-1]
#define BELOWTOP  stackspace[nextfreeItvinfo-2]

#define INCSTACKSIZE  32

#define PUSHDFS(D,B,PREVIOUSPTR)\
        if (nextfreeItvinfo >= allocatedItvinfo)\
        {\
          assert(nextfreeItvinfo == allocatedItvinfo);\
          stackspace = allocItvinfo(PREVIOUSPTR,\
                                    allocatedItvinfo,\
                                    allocatedItvinfo+INCSTACKSIZE,\
                                    allocateDfsinfo,\
                                    state,\
                                    env);\
          allocatedItvinfo += INCSTACKSIZE;\
        }\
        stackspace[nextfreeItvinfo].depth = D;\
        stackspace[nextfreeItvinfo].lastisleafedge = B;\
        nextfreeItvinfo++

 DECLAREREADFUNCTION(Seqpos);

 DECLAREREADFUNCTION(Uchar);

 DECLAREREADFUNCTION(Largelcpvalue);

typedef struct Dfsinfo Dfsinfo;
typedef struct Dfsstate Dfsstate;

typedef struct
{
  bool lastisleafedge;
  Seqpos depth;
  Dfsinfo *dfsinfo;
} Itvinfo;

static Itvinfo *allocItvinfo(Itvinfo *ptr,
                             unsigned long currentallocated,
                             unsigned long allocated,
                             Dfsinfo *(*allocateDfsinfo)(Dfsstate *,Env *),
                             Dfsstate *state,
                             Env *env)
{
  unsigned long i;
  Itvinfo *itvinfo;

  ALLOCASSIGNSPACE(itvinfo,ptr,Itvinfo,allocated);
  if (allocateDfsinfo != NULL)
  {
    assert(allocated > currentallocated);
    for (i=currentallocated; i<allocated; i++)
    {
      itvinfo[i].dfsinfo = allocateDfsinfo(state,env);
    }
  }
  assert(itvinfo != NULL);
  return itvinfo;
}

static void freeItvinfo(Itvinfo *ptr,
                        unsigned long allocated,
                        void (*freeDfsinfo)(Dfsinfo *,Dfsstate *,Env *),
                        Dfsstate *state,
                        Env *env)
{
  unsigned long i;

  for (i=0; i<allocated; i++)
  {
    freeDfsinfo(ptr[i].dfsinfo,state,env);
  }
  FREESPACE(ptr);
}

int depthfirstesa(Suffixarray *suffixarray,
                  Uchar initialchar,
                  Dfsinfo *(*allocateDfsinfo)(Dfsstate *,Env *),
                  void(*freeDfsinfo)(Dfsinfo *,Dfsstate *,Env *),
                  int(*processleafedge)(bool,Seqpos,Dfsinfo *,
                                        Uchar,Seqpos,Dfsstate *,
                                        Env *),
                  int(*processbranchedge)(bool,
                                          Seqpos,
                                          Dfsinfo *,
                                          Dfsinfo *,
                                          Dfsstate *,
                                          Env *),
                  int(*processcompletenode)(Dfsinfo *,Dfsstate *,Env *),
                  int(*assignleftmostleaf)(Dfsinfo *,Seqpos,Dfsstate *,Env *),
                  int(*assignrightmostleaf)(Dfsinfo *,Seqpos,Seqpos,
                                            Seqpos,Dfsstate *,Env *),
                  Dfsstate *state,
                  Env *env)
{
  int retval;
  Uchar tmpsmalllcpvalue;
  bool firstedge,
       firstrootedge;
  Seqpos previoussuffix,
         previouslcp,
         currentindex,
         currentlcp = 0; /* May be necessary if the lcpvalue is used after the
                            outer while loop */
  unsigned long allocatedItvinfo = 0,
                nextfreeItvinfo = 0;
  Largelcpvalue tmpexception;
  Itvinfo *stackspace;
  Uchar leftchar;
  bool haserr = false;

  firstrootedge = true;
  PUSHDFS(0,true,NULL);
  if (assignleftmostleaf != NULL &&
      assignleftmostleaf(TOP.dfsinfo,0,state,env) != 0)
  {
    haserr = true;
  }
  for (currentindex = 0; !haserr; currentindex++)
  {
    retval = readnextUcharfromstream(&tmpsmalllcpvalue,
                                     &suffixarray->lcptabstream,
                                     env);
    if (retval < 0)
    {
      haserr = true;
      break;
    }
    if (retval == 0)
    {
      break;
    }
    if (tmpsmalllcpvalue == (Uchar) UCHAR_MAX)
    {
      retval = readnextLargelcpvaluefromstream(&tmpexception,
                                               &suffixarray->llvtabstream,
                                               env);
      if (retval < 0)
      {
        haserr = true;
        break;
      }
      if (retval == 0)
      {
        env_error_set(env,"file %s: line %d: unexpected end of file when "
                      "reading llvtab",__FILE__,__LINE__);
        haserr = true;
        break;
      }
      currentlcp = tmpexception.value;
    } else
    {
      currentlcp = (Seqpos) tmpsmalllcpvalue;
    }
    retval = readnextSeqposfromstream(&previoussuffix,
                                      &suffixarray->suftabstream,
                                      env);
    if (retval < 0)
    {
      haserr = true;
      break;
    }
    if (retval == 0)
    {
      env_error_set(env,"file %s: line %d: unexpected end of file when "
                    "reading suftab",__FILE__,__LINE__);
      haserr = true;
      break;
    }
    if (previoussuffix == 0)
    {
      leftchar = initialchar;
    } else
    {
      leftchar = getencodedchar(suffixarray->encseq,
                                previoussuffix-1,
                                suffixarray->readmode);
    }
    while (currentlcp < TOP.depth)
    {
      if (TOP.lastisleafedge)
      {
        if (processleafedge != NULL &&
            processleafedge(false,TOP.depth,TOP.dfsinfo,leftchar,
                            previoussuffix,state,env) != 0)
        {
          haserr = true;
          break;
        }
      } else
      {
        assert(nextfreeItvinfo < allocatedItvinfo);
        if (processbranchedge != NULL &&
            processbranchedge(false,
                              TOP.depth,
                              TOP.dfsinfo,
                              ABOVETOP.dfsinfo,
                              state,
                              env) != 0)
        {
          haserr = true;
          break;
        }
      }
      if (assignrightmostleaf != NULL &&
          assignrightmostleaf(TOP.dfsinfo,
                              currentindex,
                              previoussuffix,
                              currentlcp,
                              state,
                              env) != 0)
      {
        haserr = true;
        break;
      }
      if (processcompletenode != NULL &&
          processcompletenode(TOP.dfsinfo,state,env) != 0)
      {
        haserr = true;
        break;
      }
      assert(nextfreeItvinfo > 0);
      nextfreeItvinfo--;
    }
    if (haserr)
    {
      break;
    }
    if (currentlcp == TOP.depth)
    {
      if (firstrootedge && TOP.depth == 0)
      {
        firstedge = true;
        firstrootedge = false;
      } else
      {
        firstedge = false;
      }
      if (TOP.lastisleafedge)
      {
        if (processleafedge != NULL &&
            processleafedge(firstedge,TOP.depth,TOP.dfsinfo,
                            leftchar,previoussuffix,state,
                            env) != 0)
        {
          haserr = true;
          break;
        }
      } else
      {
        if (!firstedge)
        {
          assert(nextfreeItvinfo < allocatedItvinfo);
        }
        if (processbranchedge != NULL &&
            processbranchedge(firstedge,
                              TOP.depth,
                              TOP.dfsinfo,
                              firstedge ? NULL : ABOVETOP.dfsinfo,
                              state,
                              env) != 0)
        {
          haserr = true;
          break;
        }
        TOP.lastisleafedge = true;
      }
    } else
    {
      PUSHDFS(currentlcp,true,stackspace);
      if (BELOWTOP.lastisleafedge)
      {
       if (assignleftmostleaf != NULL &&
           assignleftmostleaf(TOP.dfsinfo,currentindex,state,env) != 0)
        {
          haserr = true;
          break;
        }
        if (processleafedge != NULL &&
            processleafedge(true,
                            TOP.depth,
                            TOP.dfsinfo,
                            leftchar,
                            previoussuffix,
                            state,
                            env) != 0)
        {
          haserr = true;
          break;
        }
        BELOWTOP.lastisleafedge = false;
      } else
      {
        previouslcp = TOP.depth;
        if (processbranchedge != NULL &&
            processbranchedge(true,
                              previouslcp,
                              TOP.dfsinfo,
                              NULL, /* not used since firstsucc = true */
                              state,
                              env) != 0)
        {
          haserr = true;
          break;
        }
      }
    }
  }
  if (TOP.lastisleafedge)
  {
    if (previoussuffix == 0)
    {
      leftchar = initialchar;
    } else
    {
      leftchar = getencodedchar(suffixarray->encseq,
                                previoussuffix-1,
                                suffixarray->readmode);
    }
    retval = readnextSeqposfromstream(&previoussuffix,
                                      &suffixarray->suftabstream,
                                      env);
    if (retval < 0)
    {
      haserr = true;
    } else
    {
      if (retval == 0)
      {
        env_error_set(env,"file %s: line %d: unexpected end of file when "
                      "reading suftab",__FILE__,__LINE__);
        haserr = true;
      }
    }
    if (!haserr)
    {
      if (processleafedge != NULL &&
          processleafedge(false,
                          TOP.depth,
                          TOP.dfsinfo,
                          leftchar,
                          previoussuffix,
                          state,
                          env) != 0)
      {
        haserr = true;
      }
    }
    if (!haserr)
    {
      if (assignrightmostleaf != NULL &&
          assignrightmostleaf(TOP.dfsinfo,
                              currentindex,
                              previoussuffix,
                              currentlcp,
                              state,
                              env) != 0)
      {
        haserr = true;
      }
    }
    if (!haserr)
    {
      if (processcompletenode != NULL &&
          processcompletenode(TOP.dfsinfo,state,env) != 0)
      {
        haserr = true;
      }
    }
  }
  freeItvinfo(stackspace,
              allocatedItvinfo,
              freeDfsinfo,
              state,
              env);
  return haserr ? -1 : 0;
}
