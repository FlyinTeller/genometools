/*
  Copyright (c) 2005-2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2005-2008 Center for Bioinformatics, University of Hamburg

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

#include "core/fptr_api.h"
#include "core/unused_api.h"
#include "core/xansi.h"
#include "exercise/sspliced_alignment.h"
#include "exercise/sspliced_alignment_parsing.h"
#include "extended/consensus_sa.h"
#include "tools/gt_consensus_sa.h"

static GT_Range get_genomic_range(const void *sa)
{
  SSplicedAlignment *alignment = *(SSplicedAlignment**) sa;
  assert(alignment);
  return sspliced_alignment_genomic_range(alignment);
}

static GtStrand get_strand(const void *sa)
{
  SSplicedAlignment *alignment = *(SSplicedAlignment**) sa;
  if (sspliced_alignment_is_forward(alignment))
    return GT_STRAND_FORWARD;
  return GT_STRAND_REVERSE;
}

static void get_exons(GtArray *exon_ranges, const void *sa)
{
  SSplicedAlignment *alignment = *(SSplicedAlignment**) sa;
  GT_Range exon;
  unsigned long i;
  assert(alignment);
  for (i = 0; i < sspliced_alignment_num_of_exons(alignment); i++) {
    exon = sspliced_alignment_get_exon(alignment, i);
    gt_array_add(exon_ranges, exon);
  }
}

static void process_splice_form(GtArray *spliced_alignments_in_form,
                                GT_UNUSED const void *set_of_sas,
                                GT_UNUSED unsigned long number_of_sas,
                                GT_UNUSED size_t size_of_sa,
                                GT_UNUSED void *userdata)
{
  unsigned long i;

  printf("contains [");
  for (i = 0; i < gt_array_size(spliced_alignments_in_form); i++) {
    if (i)
      xputchar(',');
    printf("%lu",
           *((unsigned long*) gt_array_get(spliced_alignments_in_form, i)));
  }
  printf("]\n");
}

static OptionParser* gt_consensus_sa_option_parser_new(GT_UNUSED
                                                       void *tool_arguments)
{
  OptionParser *op;
  op = option_parser_new("spliced_alignment_file", "Read file containing "
                         "spliced alingments, compute consensus spliced "
                         "alignments,\nand print them to stdout.");
  option_parser_set_min_max_args(op, 1, 1);
  return op;
}

static int gt_consensus_sa_runner(GT_UNUSED int argc, const char **argv,
                                  int parsed_args,
                                  GT_UNUSED void *tool_arguments, GT_Error *err)
{
  GtArray *spliced_alignments;
  SSplicedAlignment *sa;
  unsigned long i;
  int had_err = 0;
  gt_error_check(err);

  /* parse input file and store resuilts in the spliced alignment array */
  spliced_alignments = gt_array_new(sizeof (SSplicedAlignment*));
  had_err = sspliced_alignment_parse(spliced_alignments, argv[parsed_args],
                                     err);

  if (!had_err) {
    /* sort spliced alignments */
    qsort(gt_array_get_space(spliced_alignments),
          gt_array_size(spliced_alignments),
          gt_array_elem_size(spliced_alignments),
          (GT_Compare) sspliced_alignment_compare_ptr);

    /* compute the consensus spliced alignments */
    consensus_sa(gt_array_get_space(spliced_alignments),
                 gt_array_size(spliced_alignments),
                 gt_array_elem_size(spliced_alignments), get_genomic_range,
                 get_strand, get_exons, process_splice_form, NULL);
  }

  /* free */
  for (i = 0; i < gt_array_size(spliced_alignments); i++) {
    sa = *(SSplicedAlignment**) gt_array_get(spliced_alignments, i);
    sspliced_alignment_delete(sa);
  }
  gt_array_delete(spliced_alignments);

  return had_err;
}

Tool* gt_consensus_sa(void)
{
  return tool_new(NULL,
                  NULL,
                  gt_consensus_sa_option_parser_new,
                  NULL,
                  gt_consensus_sa_runner);
}
