/*
  Copyright (c) 2011 Dirk Willrodt <willrodt@zbh.uni-hamburg.de>
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

#ifndef GT_SAM_ALIGNMENT_H
#define GT_SAM_ALIGNMENT_H

#include "core/alphabet_api.h"
#include "core/str_api.h"
#include "core/types_api.h"

typedef struct GtSamAlignment GtSamAlignment;

GtSamAlignment *gt_sam_alignment_new(GtAlphabet *alphabet);

void gt_sam_alignment_delete(GtSamAlignment *gt_s_alignment);

const char *gt_sam_alignment_identifier(GtSamAlignment *gt_s_alignment);

int32_t gt_sam_alignment_ref_num(GtSamAlignment *gt_s_alignment);

unsigned long gt_sam_alignment_pos(GtSamAlignment *gt_s_alignment);

unsigned long gt_sam_alignment_read_length(GtSamAlignment *gt_s_alignment);

/*get sequence, encoded according to alphabet*/
const GtUchar *gt_sam_alignment_sequence(GtSamAlignment *gt_s_alignment);

/*
  get qualities as ASCII as in Sanger FASTQ
  length is the same as sequence, not '\0'-terminated!
*/
const GtUchar *gt_sam_alignment_qualitystring(GtSamAlignment *gt_s_alignment);

uint16_t gt_sam_alignment_cigar_length(GtSamAlignment *gt_s_alignment);

/* return length of cigar element i */
uint32_t gt_sam_alignment_cigar_i_lengt(GtSamAlignment *gt_s_alignment,
                                            uint16_t i);

/* return operation of cigar element i */
unsigned char gt_sam_alignment_cigar_i_operation(GtSamAlignment *gt_s_alignment,
                                                 uint16_t i);
/* flag questions */

uint32_t gt_sam_alignment_flag(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_paired(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_proper_paired(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_unmapped(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_mate_is_unmapped(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_reverse(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_mate_is_reverse(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_read1(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_read2(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_is_secondary(GtSamAlignment *gt_s_alignment);
uint32_t gt_sam_alignment_has_qc_failure(GtSamAlignment *gt_s_alignment);
uint32_t
gt_sam_alignment_is_optical_pcr_duplicate(GtSamAlignment *gt_s_alignment);

#endif