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
#include "core/log.h"
#include "core/queue.h"
#include "core/undef.h"
#include "core/unused_api.h"
#include "extended/csa_variable_strands.h"
#include "extended/csa_visitor.h"
#include "extended/node_visitor_rep.h"

#define GT_CSA_SOURCE_TAG "gt csa"

struct CSAVisitor {
  const GtNodeVisitor parent_instance;
  GtQueue *gt_genome_node_buffer;
  unsigned long join_length;
  GtArray *cluster;
  GtFeatureNode *buffered_feature;
  GtRange first_range,
        second_range;
  GtStr *first_str,
      *second_str,
      *gt_csa_source_str;
};

#define csa_visitor_cast(GV)\
        gt_node_visitor_cast(csa_visitor_class(), GV)

static void csa_visitor_free(GtNodeVisitor *gv)
{
  CSAVisitor *csa_visitor = csa_visitor_cast(gv);
  gt_queue_delete(csa_visitor->gt_genome_node_buffer);
  gt_array_delete(csa_visitor->cluster);
  gt_str_delete(csa_visitor->gt_csa_source_str);
}

static int csa_visitor_genome_feature(GtNodeVisitor *gv, GtFeatureNode *gf,
                                      GT_UNUSED GtError *err)
{
  CSAVisitor *csa_visitor;
  gt_error_check(err);
  csa_visitor = csa_visitor_cast(gv);

  /* determine the first range if necessary */
  if (csa_visitor->buffered_feature) {
    csa_visitor->first_range =
      gt_genome_node_get_range((GtGenomeNode*) csa_visitor->buffered_feature);
    csa_visitor->first_str =
      gt_genome_node_get_seqid((GtGenomeNode*) csa_visitor->buffered_feature);
    assert(!gt_array_size(csa_visitor->cluster));
    gt_array_add(csa_visitor->cluster, csa_visitor->buffered_feature);
    csa_visitor->buffered_feature = NULL;
  }
  else if (!gt_array_size(csa_visitor->cluster)) {
    csa_visitor->first_range = gt_genome_node_get_range((GtGenomeNode*) gf);
    csa_visitor->first_str = gt_genome_node_get_seqid((GtGenomeNode*) gf);
    gt_array_add(csa_visitor->cluster, gf);
    return 0;
  }

  assert(!csa_visitor->buffered_feature);
  csa_visitor->second_range = gt_genome_node_get_range((GtGenomeNode*) gf);
  csa_visitor->second_str = gt_genome_node_get_seqid((GtGenomeNode*) gf);

  if ((gt_str_cmp(csa_visitor->first_str, csa_visitor->second_str) == 0) &&
      (csa_visitor->first_range.end + csa_visitor->join_length >=
       csa_visitor->second_range.start)) {
      /* we are still in the cluster */
      gt_array_add(csa_visitor->cluster, gf);
      /* update first range */
      assert(csa_visitor->second_range.start >= csa_visitor->first_range.start);
      if (csa_visitor->second_range.end > csa_visitor->first_range.end)
        csa_visitor->first_range.end = csa_visitor->second_range.end;
  }
  else {
    /* end of cluster -> process it */
    gt_log_log("process cluster");
    csa_visitor->buffered_feature = gf;
    csa_visitor_process_cluster(gv, false);
    csa_visitor->first_range = csa_visitor->second_range;
    csa_visitor->first_str = csa_visitor->second_str;
  }
  return 0;
}

static int csa_visitor_default_func(GtNodeVisitor *gv, GtGenomeNode *gn,
                                    GT_UNUSED GtError *err)
{
  CSAVisitor *csa_visitor;
  gt_error_check(err);
  csa_visitor = csa_visitor_cast(gv);
  gt_queue_add(csa_visitor->gt_genome_node_buffer, gn);
  return 0;
}

static int csa_visitor_comment(GtNodeVisitor *gv, GtCommentNode *c,
                               GtError *err)
{
  return csa_visitor_default_func(gv, (GtGenomeNode*) c, err);
}

static int csa_visitor_sequence_region(GtNodeVisitor *gv, GtRegionNode *rn,
                                       GtError *err)
{
  return csa_visitor_default_func(gv, (GtGenomeNode*) rn, err);
}

static int csa_visitor_sequence_node(GtNodeVisitor *gv, GtSequenceNode *sn,
                                     GtError *err)
{
  return csa_visitor_default_func(gv, (GtGenomeNode*) sn, err);
}

const GtNodeVisitorClass* csa_visitor_class()
{
  static const GtNodeVisitorClass *gvc = NULL;
  if (!gvc) {
    gvc = gt_node_visitor_class_new(sizeof (CSAVisitor),
                                    csa_visitor_free,
                                    csa_visitor_comment,
                                    csa_visitor_genome_feature,
                                    csa_visitor_sequence_region,
                                    csa_visitor_sequence_node);
  }
  return gvc;
}

GtNodeVisitor* csa_visitor_new(unsigned long join_length)
{
  GtNodeVisitor *gv = gt_node_visitor_create(csa_visitor_class());
  CSAVisitor *csa_visitor = csa_visitor_cast(gv);
  csa_visitor->gt_genome_node_buffer = gt_queue_new();
  csa_visitor->join_length = join_length;
  csa_visitor->cluster = gt_array_new(sizeof (GtFeatureNode*));
  csa_visitor->buffered_feature = NULL;
  csa_visitor->gt_csa_source_str = gt_str_new_cstr(GT_CSA_SOURCE_TAG);
  return gv;
}

unsigned long csa_visitor_node_buffer_size(GtNodeVisitor *gv)
{
  CSAVisitor *csa_visitor = csa_visitor_cast(gv);
  return gt_queue_size(csa_visitor->gt_genome_node_buffer);
}

GtGenomeNode* csa_visitor_get_node(GtNodeVisitor *gv)
{
  CSAVisitor *csa_visitor;
  csa_visitor = csa_visitor_cast(gv);
  return gt_queue_get(csa_visitor->gt_genome_node_buffer);
}

static GtRange get_genomic_range(const void *sa)
{
  GtFeatureNode *gf = *(GtFeatureNode**) sa;
  assert(gf && gt_feature_node_has_type(gf, gft_gene));
  return gt_genome_node_get_range((GtGenomeNode*) gf);
}

static GtStrand get_strand(const void *sa)
{
  GtFeatureNode *gf = *(GtFeatureNode**) sa;
  assert(gf && gt_feature_node_has_type(gf, gft_gene));
  return gt_feature_node_get_strand(gf);
}

static int save_exon(GtGenomeNode *gn, void *data, GT_UNUSED GtError *err)
{
  GtFeatureNode *gf = (GtFeatureNode*) gn;
  GtArray *exon_ranges = (GtArray*) data;
  GtRange range;
  gt_error_check(err);
  assert(gf && exon_ranges);
  if (gt_feature_node_has_type(gf, gft_exon)) {
    range = gt_genome_node_get_range(gn);
    gt_array_add(exon_ranges, range);
  }
  return 0;
}

static void get_exons(GtArray *exon_ranges, const void *sa)
{
  GtFeatureNode *gf = *(GtFeatureNode**) sa;
  int had_err;
  assert(exon_ranges && gf && gt_feature_node_has_type(gf, gft_gene));
  had_err = gt_genome_node_traverse_children((GtGenomeNode*) gf, exon_ranges,
                                          save_exon, false, NULL);
  /* we cannot have an error here, because save_exon() doesn't produces one. */
  assert(!had_err);
  /* we got at least one exon */
  assert(gt_array_size(exon_ranges));
  assert(gt_ranges_are_sorted_and_do_not_overlap(exon_ranges));
}

static void add_sa_to_exon_feature_array(GtArray *exon_nodes,
                                         GtFeatureNode *sa,
                                         GtStr *seqid,
                                         GtStr *gt_csa_source_str,
                                         GtStrand gene_strand)
{
  GtArray *exons_from_sa;
  unsigned long i,
                exon_feature_index = 0,
                exons_from_sa_index = 0;
  GtFeatureNode *exon_feature, *exons_from_sa_feature;
  GtGenomeNode *new_feature;
  GtRange exon_feature_range, exons_from_sa_range;

  assert(exon_nodes && sa);
  assert(gene_strand != GT_STRAND_BOTH); /* is defined */

  exons_from_sa = gt_array_new(sizeof (GtFeatureNode*));
  gt_feature_node_get_exons(sa, exons_from_sa);
  gt_genome_nodes_sort(exons_from_sa);

  while (exon_feature_index < gt_array_size(exon_nodes) &&
         exons_from_sa_index < gt_array_size(exons_from_sa)) {
    exon_feature = *(GtFeatureNode**)
                   gt_array_get(exon_nodes, exon_feature_index);
    exons_from_sa_feature = *(GtFeatureNode**)
                            gt_array_get(exons_from_sa, exons_from_sa_index);

    exon_feature_range = gt_genome_node_get_range((GtGenomeNode*)
                                                  exon_feature);
    exons_from_sa_range =
      gt_genome_node_get_range((GtGenomeNode*) exons_from_sa_feature);

    switch (gt_range_compare(exon_feature_range, exons_from_sa_range)) {
      case -1:
        if (gt_range_overlap(exon_feature_range, exons_from_sa_range)) {
          if (!gt_range_contains(exon_feature_range, exons_from_sa_range)) {
            assert(gt_genome_node_get_start((GtGenomeNode*) exon_feature) <=
              gt_genome_node_get_start((GtGenomeNode*) exons_from_sa_feature));
            assert(gt_genome_node_get_end((GtGenomeNode*) exon_feature) <
                gt_genome_node_get_end((GtGenomeNode*) exons_from_sa_feature));
            /* update right border and score */
            gt_feature_node_set_end(exon_feature,
                                   gt_genome_node_get_end((GtGenomeNode*)
                                                       exons_from_sa_feature));
            if (gt_feature_node_score_is_defined(exons_from_sa_feature)) {
              gt_feature_node_set_score(exon_feature,
                            gt_feature_node_get_score(exons_from_sa_feature));
            }
          }
          exons_from_sa_index++;
        }
        exon_feature_index++;
        break;
      case 0:
        assert(gt_range_overlap(exon_feature_range, exons_from_sa_range));
        /* update score if necessary */
        if ((gt_feature_node_score_is_defined(exon_feature) &&
             gt_feature_node_score_is_defined(exons_from_sa_feature) &&
             gt_feature_node_get_score(exon_feature) <
             gt_feature_node_get_score(exons_from_sa_feature)) ||
            (!gt_feature_node_score_is_defined(exon_feature) &&
             gt_feature_node_score_is_defined(exons_from_sa_feature))) {
          gt_feature_node_set_score(exon_feature,
                            gt_feature_node_get_score(exons_from_sa_feature));
        }
        exon_feature_index++;
        exons_from_sa_index++;
        break;
      case 1:
        assert(gt_range_overlap(exon_feature_range, exons_from_sa_range));
        assert(gt_genome_node_get_start((GtGenomeNode*) exon_feature) <=
              gt_genome_node_get_start((GtGenomeNode*) exons_from_sa_feature));
        /* update right border and score, if necessary */
        if (gt_genome_node_get_end((GtGenomeNode*) exons_from_sa_feature) >
            gt_genome_node_get_end((GtGenomeNode*) exon_feature)) {
          gt_feature_node_set_end(exon_feature,
                                 gt_genome_node_get_end((GtGenomeNode*)
                                                     exons_from_sa_feature));
          if (gt_feature_node_score_is_defined(exons_from_sa_feature)) {
            gt_feature_node_set_score(exon_feature,
                            gt_feature_node_get_score(exons_from_sa_feature));
          }
        }
        exon_feature_index++;
        exons_from_sa_index++;
        break;
      default: assert(0);
    }
  }

  /* add remaining exons */
  for (i = exons_from_sa_index; i < gt_array_size(exons_from_sa); i++) {
    GtRange range;
    exons_from_sa_feature = *(GtFeatureNode**)
                            gt_array_get(exons_from_sa, i);
    range = gt_genome_node_get_range((GtGenomeNode*) exons_from_sa_feature),
    new_feature = gt_feature_node_new(seqid, gft_exon, range.start, range.end,
                                        gene_strand);
    if (gt_feature_node_score_is_defined(exons_from_sa_feature)) {
      gt_feature_node_set_score((GtFeatureNode*) new_feature,
                            gt_feature_node_get_score(exons_from_sa_feature));
    }
    gt_feature_node_set_source((GtFeatureNode*) new_feature, gt_csa_source_str);
    gt_array_add(exon_nodes, new_feature);
  }

  gt_array_delete(exons_from_sa);
}

#ifndef NDEBUG
static bool genome_nodes_are_sorted_and_do_not_overlap(GtArray *exon_nodes)
{
  GtArray *ranges = gt_array_new(sizeof (GtRange));
  unsigned long i;
  GtRange range;
  bool rval;
  assert(exon_nodes);
  for (i = 0; i < gt_array_size(exon_nodes); i++) {
    range = gt_genome_node_get_range(*(GtGenomeNode**)
                                     gt_array_get(exon_nodes, i));
    gt_array_add(ranges, range);
  }
  rval = gt_ranges_are_sorted_and_do_not_overlap(ranges);
  gt_array_delete(ranges);
  return rval;
}
#endif

static void mRNA_set_target_attribute(GtFeatureNode *mRNA_feature,
                                      const CSASpliceForm *csa_splice_form)
{
  unsigned long i;
  GtStr *targets;
  assert(mRNA_feature && csa_splice_form);
  targets = gt_str_new();
  for (i = 0; i < csa_splice_form_num_of_sas(csa_splice_form); i++) {
    GtFeatureNode *sa = *(GtFeatureNode**)
                        csa_splice_form_get_sa(csa_splice_form, i);
    if (gt_feature_node_get_attribute(sa, "Target")) {
      if (gt_str_length(targets))
        gt_str_append_char(targets, ',');
      gt_str_append_cstr(targets,
                         gt_feature_node_get_attribute(sa, "Target"));
    }
  }
  if (gt_str_length(targets)) {
    gt_feature_node_add_attribute(mRNA_feature, "Target",
                                    gt_str_get(targets));
  }
  gt_str_delete(targets);
}

static GtFeatureNode* create_mRNA_feature(CSASpliceForm *csa_splice_form,
                                          GtStr *gt_csa_source_str)
{
  GtFeatureNode *mRNA_feature;
  GtArray *exon_nodes;
  unsigned long i;
  GtRange range;
  GtStrand strand;
  GtStr *seqid;
  assert(csa_splice_form && gt_csa_source_str);

  /* create the mRNA feature itself */
  seqid = gt_genome_node_get_seqid(*(GtGenomeNode**)
                               csa_splice_form_get_representative(
                                                              csa_splice_form));
  range = csa_splice_form_genomic_range(csa_splice_form),
  strand = csa_splice_form_strand(csa_splice_form),
  mRNA_feature = (GtFeatureNode*)
                 gt_feature_node_new(seqid, gft_mRNA, range.start, range.end,
                                     strand);
  gt_feature_node_set_source(mRNA_feature, gt_csa_source_str);
  mRNA_set_target_attribute(mRNA_feature, csa_splice_form);

  /* create exon features */
  exon_nodes = gt_array_new(sizeof (GtGenomeNode*));
  for (i = 0; i < csa_splice_form_num_of_sas(csa_splice_form); i++) {
    add_sa_to_exon_feature_array(exon_nodes,
                                 *(GtFeatureNode**)
                                 csa_splice_form_get_sa(csa_splice_form, i),
                                 seqid, gt_csa_source_str, strand);
  }
  assert(genome_nodes_are_sorted_and_do_not_overlap(exon_nodes));

  /* add exon features to mRNA feature */
  for (i = 0; i < gt_array_size(exon_nodes); i++) {
    gt_feature_node_add_child(mRNA_feature,
                             *(GtFeatureNode**) gt_array_get(exon_nodes, i));
  }

  gt_array_delete(exon_nodes);

  return mRNA_feature;
}

static GtFeatureNode* create_gene_feature(CSAGene *csa_gene,
                                          GtStr *gt_csa_source_str)
{
  GtFeatureNode *gene_feature, *mRNA_feature;
  GtRange range;
  unsigned long i;
  assert(csa_gene && gt_csa_source_str);

  /* create top-level gene feature */
  range = csa_gene_genomic_range(csa_gene),
  gene_feature = (GtFeatureNode*)
    gt_feature_node_new(gt_genome_node_get_seqid(*(GtGenomeNode**)
                                         csa_gene_get_representative(csa_gene)),
                          gft_gene, range.start, range.end,
                          csa_gene_strand(csa_gene));
  gt_feature_node_set_source(gene_feature, gt_csa_source_str);

  /* create mRNA features representing the splice forms */
  for (i = 0; i < csa_gene_num_of_splice_forms(csa_gene); i++) {
    CSASpliceForm *csa_splice_form = csa_gene_get_splice_form(csa_gene, i);
    mRNA_feature = create_mRNA_feature(csa_splice_form, gt_csa_source_str);
    gt_feature_node_add_child(gene_feature, mRNA_feature);
  }

  return gene_feature;
}

static void process_csa_genes(GtQueue *gt_genome_node_buffer,
                              GtArray *csa_genes,
                              GtStr *gt_csa_source_str)
{
  unsigned long i;
  assert(csa_genes);
  for (i = 0; i < gt_array_size(csa_genes); i++) {
    GtFeatureNode *gene_feature = create_gene_feature(*(CSAGene**)
                                                     gt_array_get(csa_genes, i),
                                                      gt_csa_source_str);
    gt_queue_add(gt_genome_node_buffer, gene_feature);
  }
}

void csa_visitor_process_cluster(GtNodeVisitor *gv, bool final_cluster)
{
  CSAVisitor *csa_visitor = csa_visitor_cast(gv);
  GtFeatureNode *first_feature;
  GtArray *csa_genes;
  unsigned long i;

  if (final_cluster) {
    assert(!gt_array_size(csa_visitor->cluster) ||
           !csa_visitor->buffered_feature);
    if (csa_visitor->buffered_feature) {
      gt_array_add(csa_visitor->cluster, csa_visitor->buffered_feature);
      csa_visitor->buffered_feature = NULL;
    }
  }

  if (!gt_array_size(csa_visitor->cluster)) {
    assert(final_cluster);
    return;
  }

  /* compute the consensus spliced alignments */
  first_feature = *(GtFeatureNode**)
                  gt_array_get_first(csa_visitor->cluster);
  csa_genes = csa_variable_strands(gt_array_get_space(csa_visitor->cluster),
                                   gt_array_size(csa_visitor->cluster),
                                   sizeof (GtFeatureNode*),
                                   get_genomic_range,
                                   get_strand, get_exons);

  process_csa_genes(csa_visitor->gt_genome_node_buffer, csa_genes,
                    csa_visitor->gt_csa_source_str);

  for (i = 0; i < gt_array_size(csa_genes); i++)
    csa_gene_delete(*(CSAGene**) gt_array_get(csa_genes, i));
  gt_array_delete(csa_genes);

  /* remove the cluster genome nodes */
  for (i = 0; i < gt_array_size(csa_visitor->cluster); i++) {
    gt_genome_node_rec_delete(*(GtGenomeNode**)
                              gt_array_get(csa_visitor->cluster, i));
  }
  gt_array_reset(csa_visitor->cluster);
}
