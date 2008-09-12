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

#include <string.h>
#include "core/bioseq.h"
#include "core/ma.h"
#include "core/option.h"
#include "core/undef.h"
#include "tools/gt_bioseq.h"

typedef struct {
  bool recreate,
       showfasta,
       gc_content,
       stat,
       seqlengthdistri;
  unsigned long showseqnum,
                width;
  GtStr *reader;
} GT_BioseqArguments;

static void* gt_gt_bioseq_arguments_new(void)
{
  GT_BioseqArguments *arguments = gt_calloc(1, sizeof *arguments);
  arguments->reader = gt_str_new();
  return arguments;
}

static void gt_gt_bioseq_arguments_delete(void *tool_arguments)
{
  GT_BioseqArguments *arguments = tool_arguments;
  if (!arguments) return;
  gt_str_delete(arguments->reader);
  gt_free(arguments);
}

static OptionParser* gt_gt_bioseq_option_parser_new(void *tool_arguments)
{
  GT_BioseqArguments *arguments = tool_arguments;
  Option *option, *option_recreate, *option_showfasta, *option_showseqnum,
         *option_width, *option_stat, *option_reader;
  OptionParser *op;
  static const char *reader_types[] = { "rec", "fsm", "seqit", NULL };
  assert(arguments);

  op = option_parser_new("[option ...] sequence_file [...]",
                         "Construct the GT_Biosequence files for the given "
                         "sequence_file(s) (if necessary).");

  /* -recreate */
  option_recreate = option_new_bool("recreate", "recreate GT_Biosequence "
                                    "files, even if they exist already",
                                    &arguments->recreate, false);
  option_parser_add_option(op, option_recreate);

  /* -showfasta */
  option_showfasta = option_new_bool("showfasta", "show sequences on stdout "
                                     "(in fasta format)", &arguments->showfasta,
                                     false);
  option_parser_add_option(op, option_showfasta);

  /* -showseqnum */
  option_showseqnum = option_new_ulong_min("showseqnum", "show sequence with "
                                           "given number on stdout (in fasta "
                                           "format)", &arguments->showseqnum,
                                           UNDEF_ULONG, 1);
  option_parser_add_option(op, option_showseqnum);

  /* -gc-content */
  option = option_new_bool("gc-content", "show GC-content on stdout (for DNA "
                           "files)", &arguments->gc_content, false);
  option_parser_add_option(op, option);

  /* -stat */
  option_stat = option_new_bool("stat", "show sequence statistics",
                                &arguments->stat, false);
  option_parser_add_option(op, option_stat);

  /* -seqlengthdistri */
  option = option_new_bool("seqlengthdistri", "show sequence length "
                           "distribution", &arguments->seqlengthdistri, false);
  option_parser_add_option(op, option);

  /* -width */
  option_width = option_new_ulong("width", "set output width for showing of "
                                  "sequences (0 disables formatting)",
                                  &arguments->width, 0);
  option_parser_add_option(op, option_width);

  /* -reader */
  option_reader = option_new_choice("reader", "set fasta reader type\n"
                                    "choose rec|fsm|seqit",
                                    arguments->reader, reader_types[0],
                                    reader_types);
  option_is_development_option(option_reader);
  option_parser_add_option(op, option_reader);

  /* option implications */
  option_imply(option_reader, option_recreate);
  option_imply_either_2(option_width, option_showfasta, option_showseqnum);

  /* option exclusions */
  option_exclude(option_showfasta, option_stat);
  option_exclude(option_showfasta, option_showseqnum);
  option_exclude(option_showseqnum, option_stat);

  /* set minimal arugments */
  option_parser_set_min_args(op, 1);

  return op;
}

static int gt_gt_bioseq_arguments_check(int rest_argc, void *tool_arguments,
                                     GT_Error *err)
{
  GT_BioseqArguments *arguments = tool_arguments;
  gt_error_check(err);
  assert(arguments);
  /* option -showseqnum makes only sense if we got a single sequence file */
  if (arguments->showseqnum != UNDEF_ULONG && rest_argc > 1) {
    gt_error_set(err, "option '-showseqnum' makes only sense with a single "
                   "sequence_file");
    return -1;
  }
  return 0;
}

static int gt_gt_bioseq_runner(int argc, const char **argv, int parsed_args,
                            void *tool_arguments, GT_Error *err)
{
  GT_BioseqArguments *arguments = tool_arguments;
  GT_Bioseq *bioseq;
  GT_FastaReaderType reader_type = GT_FASTA_READER_REC;
  int arg = parsed_args, had_err = 0;
  gt_error_check(err);
  assert(tool_arguments);

  /* determine fasta reader type */
  if (!strcmp(gt_str_get(arguments->reader), "rec"))
    reader_type = GT_FASTA_READER_REC;
  else if (!strcmp(gt_str_get(arguments->reader), "fsm"))
    reader_type = GT_FASTA_READER_FSM;
  else if (!strcmp(gt_str_get(arguments->reader), "seqit"))
    reader_type = GT_FASTA_READER_SEQIT;
  else {
    assert(0); /* cannot happen */
  }

  while (!had_err && arg < argc) {
    /* bioseq construction */
    if (arguments->recreate)
      bioseq = gt_bioseq_new_with_fasta_reader(argv[arg], reader_type, err);
    else
      bioseq = gt_bioseq_new(argv[arg], err);
    if (!bioseq)
      had_err = -1;

    /* output */
    if (!had_err && arguments->showfasta)
      gt_bioseq_show_as_fasta(bioseq, arguments->width);

    if (!had_err && arguments->showseqnum != UNDEF_ULONG) {
      if (arguments->showseqnum > gt_bioseq_number_of_sequences(bioseq)) {
        gt_error_set(err, "argument '%lu' to option '-showseqnum' is too "
                     "large. The GT_Biosequence contains only '%lu' sequences.",
                  arguments->showseqnum, gt_bioseq_number_of_sequences(bioseq));
        had_err = -1;
      }
      if (!had_err) {
        gt_bioseq_show_sequence_as_fasta(bioseq, arguments->showseqnum - 1,
                                      arguments->width);
      }
    }

    if (!had_err && arguments->gc_content)
      gt_bioseq_show_gc_content(bioseq);

    if (!had_err && arguments->stat)
      gt_bioseq_show_stat(bioseq);

    if (!had_err && arguments->seqlengthdistri)
      gt_bioseq_show_seqlengthdistri(bioseq);

    /* free */
    gt_bioseq_delete(bioseq);

    arg++;
  }

  return had_err;
}

Tool* gt_bioseq(void)
{
  return tool_new(gt_gt_bioseq_arguments_new,
                  gt_gt_bioseq_arguments_delete,
                  gt_gt_bioseq_option_parser_new,
                  gt_gt_bioseq_arguments_check,
                  gt_gt_bioseq_runner);
}
