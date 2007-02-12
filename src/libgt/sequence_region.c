/*
  Copyright (c) 2006-2007 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2006-2007 Center for Bioinformatics, University of Hamburg
  See LICENSE file or http://genometools.org/license.html for license details.
*/

#include <assert.h>
#include <stdlib.h>
#include "sequence_region.h"
#include "genome_node_rep.h"

struct SequenceRegion
{
  const GenomeNode parent_instance;
  Str *seqid;
  Range range;
};

#define sequence_region_cast(GN)\
        genome_node_cast(sequence_region_class(), GN)

static void sequence_region_free(GenomeNode *gn)
{
  SequenceRegion *sr = sequence_region_cast(gn);
  assert(sr && sr->seqid);
  str_free(sr->seqid);
}

static Str* sequence_region_get_seqid(GenomeNode *gn)
{
  SequenceRegion *sr = sequence_region_cast(gn);
  return sr->seqid;
}

static Range sequence_region_get_range(GenomeNode *gn)
{
  SequenceRegion *sr = sequence_region_cast(gn);
  return sr->range;
}

static void sequence_region_set_range(GenomeNode *gn, Range range)
{
  SequenceRegion *sr = sequence_region_cast(gn);
  sr->range = range;
}

static void sequence_region_accept(GenomeNode *gn, GenomeVisitor *gv, Log *l)
{
  SequenceRegion *sr = sequence_region_cast(gn);
  genome_visitor_visit_sequence_region(gv, sr, l);
}

const GenomeNodeClass* sequence_region_class()
{
  static const GenomeNodeClass gnc = { sizeof(SequenceRegion),
                                         sequence_region_free,
                                         sequence_region_get_seqid,
                                         sequence_region_get_seqid,
                                         sequence_region_get_range,
                                         sequence_region_set_range,
                                         NULL,
                                         NULL,
                                         NULL,
                                         sequence_region_accept };
  return &gnc;
}

GenomeNode* sequence_region_new(Str *seqid,
                                 Range range,
                                 const char *filename,
                                 unsigned long line_number)
{
  GenomeNode *gn = genome_node_create(sequence_region_class(), filename,
                                       line_number);
  SequenceRegion *sr = sequence_region_cast(gn);
  assert(seqid);
  sr->seqid = str_ref(seqid);
  sr->range = range;
  return gn;
}
