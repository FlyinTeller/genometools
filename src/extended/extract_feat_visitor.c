/*
  Copyright (c) 2006-2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2006-2008 Center for Bioinformatics, University of Hamburg

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

#include <assert.h>
#include "core/fasta.h"
#include "core/symbol.h"
#include "core/translate.h"
#include "extended/extract_feat_sequence.h"
#include "extended/extract_feat_visitor.h"
#include "extended/genome_node_iterator.h"
#include "extended/node_visitor_rep.h"

struct GtExtractFeatVisitor {
  const GtNodeVisitor parent_instance;
  const char *type;
  bool join,
       translate;
  unsigned long fastaseq_counter;
  GtRegionMapping *region_mapping;
};

#define gt_extract_feat_visitor_cast(GV)\
        gt_node_visitor_cast(gt_extract_feat_visitor_class(), GV)

static void extract_feat_visitor_free(GtNodeVisitor *gv)
{
  GtExtractFeatVisitor *extract_feat_visitor = gt_extract_feat_visitor_cast(gv);
  assert(extract_feat_visitor);
  gt_region_mapping_delete(extract_feat_visitor->region_mapping);
}

static void construct_description(GtStr *description, const char *type,
                                  unsigned long counter, bool join,
                                  bool translate)
{
  assert(!gt_str_length(description));
  gt_str_append_cstr(description, type);
  gt_str_append_char(description, '_');
  gt_str_append_ulong(description, counter);
  if (join)
    gt_str_append_cstr(description, " (joined)");
  if (translate)
    gt_str_append_cstr(description, " (translated)");
}

static void show_entry(GtStr *description, GtStr *sequence, bool translate)
{
  if (translate) {
    GtStr *protein = gt_str_new();
    gt_translate_dna(protein, gt_str_get(sequence), gt_str_length(sequence), 0);
    gt_fasta_show_entry(gt_str_get(description), gt_str_get(protein),
                        gt_str_length(protein), 0);
    gt_str_delete(protein);
  }
  else {
    gt_fasta_show_entry(gt_str_get(description), gt_str_get(sequence),
                        gt_str_length(sequence), 0);
  }
}

static int extract_feat_visitor_genome_feature(GtNodeVisitor *gv,
                                               GtFeatureNode *gf,
                                               GtError *err)
{
  GtExtractFeatVisitor *efv;
  GtGenomeNodeIterator *gni;
  GtGenomeNode *gn;
  GtStr *description,
      *sequence;
  int had_err = 0;
  gt_error_check(err);
  efv = gt_extract_feat_visitor_cast(gv);
  assert(efv->region_mapping);
  gni = gt_genome_node_iterator_new((GtGenomeNode*) gf);
  description = gt_str_new();
  sequence = gt_str_new();
  while (!had_err && (gn = gt_genome_node_iterator_next(gni))) {
    if (gt_extract_feat_sequence(sequence, gn, efv->type, efv->join,
                              efv->region_mapping, err)) {
      had_err = -1;
    }

    if (!had_err && gt_str_length(sequence)) {
      efv->fastaseq_counter++;
      construct_description(description, efv->type, efv->fastaseq_counter,
                            efv->join, efv->translate);
      show_entry(description, sequence, efv->translate);
      gt_str_reset(description);
      gt_str_reset(sequence);
    }
  }
  gt_str_delete(sequence);
  gt_str_delete(description);
  gt_genome_node_iterator_delete(gni);
  return had_err;
}

const GtNodeVisitorClass* gt_extract_feat_visitor_class()
{
  static const GtNodeVisitorClass *gvc = NULL;
  if (!gvc) {
    gvc = gt_node_visitor_class_new(sizeof (GtExtractFeatVisitor),
                                    extract_feat_visitor_free,
                                    NULL,
                                    extract_feat_visitor_genome_feature,
                                    NULL,
                                    NULL);
  }
  return gvc;
}

GtNodeVisitor* gt_extract_feat_visitor_new(GtRegionMapping *rm,
                                           const char *type,
                                           bool join, bool translate)
{
  GtNodeVisitor *gv;
  GtExtractFeatVisitor *efv;
  assert(rm);
  gv = gt_node_visitor_create(gt_extract_feat_visitor_class());
  efv= gt_extract_feat_visitor_cast(gv);
  efv->type = gt_symbol(type);
  efv->join = join;
  efv->translate = translate;
  efv->fastaseq_counter = 0;
  efv->region_mapping = rm;
  return gv;
}
