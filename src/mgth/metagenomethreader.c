/*
  Copyright (c) 2007 David Schmitz-Huebsch <dschmitz@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg

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

#include "core/fileutils.h"
#include "core/hashmap-generic.h"
#include "core/unused_api.h"
#include "match/giextract.pr"
#include "mg_xmlparser.h"
#include "metagenomethreader.h"
/* Funktion zur Ausgabe des Statistikbereichs
   Parameter: Schluessel, Value, User-Data, Env-Variable
   Returnwert: 0 */
static enum iterator_op
printout_hits(GT_UNUSED char *key,
              unsigned long *value, void *data, GT_Error * err);

static OPrval parse_options(int *parsed_args,
                            MetagenomeThreaderArguments
                            *metagenomethreader_arguments, int argc,
                            const char **argv, GT_Error * err)
{
  OptionParser *op;
  OPrval oprval;

  /* Definition der optionalen Eingabeparameter */
  Option *syn_value_option,
   *nonsyn_value_option,
   *blasthitend_value_option,
   *stopcodon_queryseq_option,
   *stopcodon_hitseq_option,
   *frameshift_span_option,
   *prediction_span_option,
   *curlfcgi_option,
   *leavegene_value_option,
   *resulttextfile_name_option,
   *giexpfile_name_option,
   *hitfile_bool_option,
   *homology_mode_option,
   *extended_mode_option,
   *testmodus_mode_option,
   *outputfile_format_option,
   *codon_mode_option,
   *min_as_option,
   *percent_option;

  /* Check Umgebungsvariablen */
  gt_error_check(err);

  /* init Option-Parser */
  op =
    option_parser_new
    ("[option ...] XML-File Query-FASTA-File Hit-FASTA-File",
     "Metagenomethreader, for predicting genes in metagenomeprojects.");

  /* Option zur Eingabe des Scores fuer synonymen Basenaustausch; default:
     1.0 */
  syn_value_option =
    option_new_double("s", "score for synonymic base exchanges",
                      &metagenomethreader_arguments->synonomic_value, 1.0);
  option_parser_add_option(op, syn_value_option);

  /* Option zur Eingabe des Scores fuer nicht-synonymen Basenaustausch;
     default: -1.0 */
  nonsyn_value_option =
    option_new_double("n", "score for non-synonymic base exchanges",
                      &metagenomethreader_arguments->nonsynonomic_value,
                      -1.0);
  option_parser_add_option(op, nonsyn_value_option);

  /* Option zur Eingabe des Scores fuer das Blast-Hit-Ende; default: -10 */
  blasthitend_value_option =
    option_new_double("b", "score for blast-hit-end within query sequence",
                      &metagenomethreader_arguments->blasthit_end_value,
                      -10.0);
  option_parser_add_option(op, blasthitend_value_option);

  /* Option zur Eingabe des Scores zur Bewertung eines Stop-Codons in der
     Query-Sequence; default: -2 */
  stopcodon_queryseq_option =
    option_new_double("q", "score for stop-codon within querysequence",
                      &metagenomethreader_arguments->stopcodon_queryseq,
                      -2.0);
  option_parser_add_option(op, stopcodon_queryseq_option);

  /* Option zur Eingabe des Scores zur Bewertung eines Stop-Codons in der
     Hit-Sequence; default: -5 */
  stopcodon_hitseq_option =
    option_new_double("h", "score for stop-codon within hitsequence",
                      &metagenomethreader_arguments->stopcodon_hitseq,
                      -5.0);
  option_parser_add_option(op, stopcodon_hitseq_option);

  /* Option zur Eingabe des Scores zur Bewertung des Verlassens oder
     Eintretens eines bzw. in ein Gen ; default: -2 */
  leavegene_value_option =
    option_new_double("l",
                      "score for leaving a gene on forward/reverse strand "
                      "or enter a gene on forward/reverse strand",
                      &metagenomethreader_arguments->leavegene_value,
                      -2.0);
  option_parser_add_option(op, leavegene_value_option);

  /* Option zur Eingabe des Wertes innerhalb welches Zwischenbereichs ein
     zusammenhaengender Genbereich vorliegt; default: 400 */
  prediction_span_option =
    option_new_double_min("p",
                          "max. span between coding-regions resume as one "
                          "prediction",
                          &metagenomethreader_arguments->prediction_span,
                          400.0, 0.0);
  option_parser_add_option(op, prediction_span_option);

  /* Option zur Eingabe des Wertes innerhalb welches Zwischenbereichs ein
     Frameshift vorliegt; default: 200 */
  frameshift_span_option =
    option_new_double_min("f",
                          "max. span between coding-regions in different "
                          "reading frames resume as coding-regions in the "
                          "optimal reading-frame",
                          &metagenomethreader_arguments->frameshift_span,
                      200.0, 0.0);
  option_parser_add_option(op, frameshift_span_option);

  /* Option zur Eingabe der DB, die bei der Verwendung des fcgi-Skriptes
     verwendet wird; default: nucleotide */
  curlfcgi_option =
    option_new_string("c", "db-name for fcgi-db",
                      metagenomethreader_arguments->curl_fcgi_db,
                      "nucleotide");
  option_parser_add_option(op, curlfcgi_option);

  /* Option zur Eingabe des Namens der Outputdatei; default: output.txt */
  resulttextfile_name_option =
    option_new_string("o", "name for resulting output-file",
                      metagenomethreader_arguments->outputtextfile_name,
                      "output");
  option_parser_add_option(op, resulttextfile_name_option);

  /* Option zur Angabe der Hit-Sequenz-DB  */
  giexpfile_name_option =
    option_new_stringarray("k", "name for the Hit-Sequence-DB",
                           metagenomethreader_arguments->giexpfile_name);
  option_parser_add_option(op, giexpfile_name_option);

  /* Option zur Angabe, ob ein Hit-FASTA-File exisitiert; default: false */
  hitfile_bool_option =
    option_new_bool("t", "true or false if a Hit-FASTA-File exist",
                    &metagenomethreader_arguments->hitfile_bool, false);
  option_parser_add_option(op, hitfile_bool_option);

  /* Option zur Angabe des Formates der Outputdatei; default: 1 -> txt */
  outputfile_format_option =
    option_new_int("r", "format of the output-file",
                   &metagenomethreader_arguments->outputfile_format, 1);
  option_parser_add_option(op, outputfile_format_option);

  /* Option zur Angabe der minimalen Laenger der AS-Sequnezen, die in der
     Ausgabedatei aufgefuehrt werden sollen; default: 15; min: 15 */
  min_as_option =
    option_new_ulong_min("a", "minimum length of the as-sequence",
                         &metagenomethreader_arguments->min_as, 15, 15);
  option_parser_add_option(op, min_as_option);

  /* Option zur Angabe der minimalen Prozentzahl, ab der Hits-Def. im
     Statistikbereich der Ausgabe angezeigt werden */
  percent_option =
    option_new_double_min_max("d",
                              "minimum percent-value for hit-statistic-output",
                              &metagenomethreader_arguments->percent_value,
                              0.0, 0.0, 1.0);
  option_parser_add_option(op, percent_option);

  /* Option zur Angabe, ob alternative Start-Codons verwendet werden
     sollen; default: 1 -> txt */
  codon_mode_option =
    option_new_int("e", "use of alternative start-codons",
                   &metagenomethreader_arguments->codon_mode, 1);
  option_parser_add_option(op, codon_mode_option);

  /* Experimenteller Status dieser Option - Funktionsfaehigkeit nicht
     abschliessend geklaert */
  /* Option zur Angabe, ob nach Homologien gesucht werden soll, statt nach
     synonymen-Basenaustauschen; default: false */
  homology_mode_option =
    option_new_bool("m", "search for homology",
                    &metagenomethreader_arguments->homology_mode, false);
  option_parser_add_option(op, homology_mode_option);

  /* Testmodus: die Programm-Parameter werden zur Vergleichbarkeit der
     Ergebnisse nicht mit ausgegeben; default: false */
  testmodus_mode_option =
    option_new_bool("g", "testmodus, output without creating date",
                    &metagenomethreader_arguments->testmodus_mode, false);
  option_parser_add_option(op, testmodus_mode_option);

  /* Option zur Angabe, ob die EGTs auf den max. ORF erweitert werden
     sollen, default: false */
  extended_mode_option =
    option_new_bool("x", "extend the EGTs to max",
                    &metagenomethreader_arguments->extended_mode, false);
  option_parser_add_option(op, extended_mode_option);

  option_parser_set_mailaddress(op, "<dschmitz@zbh.uni-hamburg.de>");
  option_parser_set_min_max_args(op, 3, 3);
  option_parser_refer_to_manual(op);

  /* es werden die Parameter XML-File, Query-Fasta-File und Hit-FASTA-File
     erwartet, Min und Max. der Anzahl an Parametern ist also 3 */
  oprval = option_parser_parse(op, parsed_args, argc, argv, versionfunc, err);

  option_parser_delete(op);
  return oprval;
}

DEFINE_HASHMAP(char *, cstr_nofree, unsigned long *, ulp, ht_cstr_elem_hash,
               ht_cstr_elem_cmp, NULL_DESTRUCTOR, NULL_DESTRUCTOR,,)

int metagenomethreader(int argc, const char **argv, GT_Error * err)
{
  int had_err = 0,
      parsed_args = 0;

  /* Variablen/Zeiger zur Erstellung des Hashes fuer die GT_Bioseq-Strukturen
   */
  unsigned long *querynum,
   *hitnum = NULL,
    nrofseq,
    loop_index;
  char *descr_ptr_hit,
   *descr_ptr_query;

  GtStr *outputfilename;

  /* GT_GenFile Zeiger auf die XML-Datei mit den Blast-Hits */
  GT_GenFile *fp_xmlfile;

  /* Anlegen der Parser-Array-Struktur, ueber den der Austausch von
     Informationen Parsestruct parsestruct; zwischen den
     XML-Parser-Handlern erfolgt Zeiger auf die parser_array-Struktur;
     notwendig, da bei expat nur die Uebergabe eines Zeigers auf die
     UserDaten erfolgen kann und nicht z.B. ein Zeiger auf Zeiger */
  ParseStruct parsestruct;
  ParseStruct *parsestruct_ptr;

  parsestruct_ptr = &parsestruct;

  ARGUMENTS(curl_fcgi_db) = gt_str_new();
  ARGUMENTS(outputtextfile_name) = gt_str_new();
  ARGUMENTS(giexpfile_name) = gt_strarray_new();

  /* Check der Umgebungsvariablen */
  gt_error_check(err);

  /* option parsing */
  switch (parse_options(&parsed_args, &parsestruct.metagenomethreader_arguments,
                        argc, argv, err)) {
    case OPTIONPARSER_OK:
      break;
    case OPTIONPARSER_ERROR:
      gt_str_delete(ARGUMENTS(curl_fcgi_db));
      gt_str_delete(ARGUMENTS(outputtextfile_name));
      return -1;
    case OPTIONPARSER_REQUESTS_EXIT:
      gt_str_delete(ARGUMENTS(curl_fcgi_db));
      gt_str_delete(ARGUMENTS(outputtextfile_name));
      return 0;
  }

  /* GT_Bioseqstruktur des Query-DNA-FASTA-File wird erzeugt */
  parsestruct.queryseq = gt_bioseq_new(argv[parsed_args + 1], err);
  /* Erstellung der GT_Bioseq-Struktur fehlerhaft -> Fehlercode setzen */
  if (!parsestruct.queryseq)
  {
    had_err = -1;
  }

  /* nur wenn die Hitfile-Bool-Option auf true gesetzt ist (File
     vorhanden) und der Fehlercode nicht gesetzt ist, muss die
     GT_Bioseq-Struktur auch fuer die Hits erstellt werden */
  if (ARGUMENTS(hitfile_bool) && !had_err)
  {
    /* GT_Bioseqstruktur des Hit-DNA-FASTA-File wird erzeugt */
    parsestruct.hitseq = gt_bioseq_new(argv[parsed_args + 2], err);
    /* Erstellung der GT_Bioseq-Struktur fehlerhaft -> Fehlercode setzen */
    if (!parsestruct.hitseq)
    {
      gt_bioseq_delete(parsestruct.queryseq);
      had_err = -1;
    }
  }

  if (!had_err)
  {
    outputfilename = gt_str_new();

    /* StringArrays zur Aufnahme der einzulesenden XML-Tag Bezeichnungen */
    parsestruct.query_array = gt_strarray_new();
    parsestruct.hit_array = gt_strarray_new();
    parsestruct.hit_hsp_array = gt_strarray_new();
    parsestruct.hits_statistics.hits_statistic = gt_strarray_new();
    parsestruct.key_tmp = gt_strarray_new();

    /* String fuer die Bezeichnung des letzten XML-Tags eines
       Bearbeitunsschrittes */
    parsestruct.xml_tag = gt_str_new();

    /* Zeiger auf den Speicherbereich, wo der aktuelle Text abgespeichert
       wird */
    parsestruct.buf_ptr = gt_str_new();

    /* temp-Variable zum Zwischenspeichern der Hit-GI-Definition */
    parsestruct.gi_def_tmp = gt_str_new();
    parsestruct.gi_acc_tmp = gt_str_new();
    parsestruct.hit_gi_nr_tmp = gt_str_new();
    parsestruct.fasta_row = gt_str_new();

    /* Zeiger auf Query-DNA bzw. Query-Def */
    parsestruct.matrix_info.query_dna = gt_str_new();
    parsestruct.matrix_info.query_def = gt_str_new();

    /* Strings fuer Blast-XML-File und Hit-FASTA-File */
    parsestruct.hit_fastafile = gt_str_new();
    parsestruct.xmlfile = gt_str_new();
    /* String fuer die aktuelle Hit-Def fuer die Statistik */
    parsestruct.result_hits = gt_str_new();

    /* diverse Zeiger auf Informationen aus dem XML-File */
    parsestruct.matrix_info.query_from = gt_array_new(sizeof (unsigned long));
    parsestruct.matrix_info.query_to = gt_array_new(sizeof (unsigned long));
    parsestruct.matrix_info.query_frame = gt_array_new(sizeof (unsigned long));
    parsestruct.matrix_info.hit_frame = gt_array_new(sizeof (unsigned long));
    parsestruct.value_tmp = gt_array_new(sizeof (unsigned long));

    /* mit dem schliessenden Iteration_hits XML-Tag wird im Programmablauf
       der Abschluss eines Query-Eintrages erkannt */
    gt_str_set(parsestruct.xml_tag, "Iteration_stat");

    /* XML-Tags im Query-Def-Bereich */
    gt_strarray_add_cstr(parsestruct.query_array, "Iteration_query-def");

    /* XML-Tags im Hit-Def-Bereich */
    gt_strarray_add_cstr(parsestruct.hit_array, "Hit_id");
    gt_strarray_add_cstr(parsestruct.hit_array, "Hit_def");
    gt_strarray_add_cstr(parsestruct.hit_array, "Hit_accession");

    /* XML-Tags im Hits-Bereich */
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_num");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_query-from");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_query-to");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_hit-from");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_hit-to");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_query-frame");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_hit-frame");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_qseq");
    gt_strarray_add_cstr(parsestruct.hit_hsp_array, "Hsp_hseq");

    /* 3 Flags, die bei oeffnenden XML-Tags gesetzt und bei schliessenden
       geloescht werden */
    parsestruct.def_flag = UNSET;
    parsestruct.hit_flag = UNSET;
    parsestruct.hit_hsp_flag = UNSET;
    parsestruct.xml_tag_flag = UNSET;
    parsestruct.giexp_flag = UNSET;
    parsestruct.gi_flag = UNSET;

    /* Variablen (Zeilennummer, Fehlercode) zur Fehlerbehandlung waehrend
       des Parsvorganges des XML-Files */
    parsestruct.xml_linenumber = 0;
    parsestruct.had_err = 0;

    /* Zaehlvariablen fuer die Anzahl der Syn- und Nichtsynonymen
       DNA-Austausche */
    parsestruct.syn = 0.0;
    parsestruct.non_syn = 0.0;

    /* hit_counter zum Zaehlen der Hits pro Hit-GI */
    parsestruct.xmlparser_static.hit_counter = 0;

    /* Zaehlvariablen, die Position in den jeweiligen XML-Tag-Bloecken
       angeben */
    parsestruct.xmlparser_static.query_array_index_start = 0;
    parsestruct.xmlparser_static.hit_array_index_start = 0;
    parsestruct.xmlparser_static.hit_hsp_array_index_start = 0;
    parsestruct.xmlparser_static.query_array_index_end = 0;
    parsestruct.xmlparser_static.hit_array_index_end = 0;
    parsestruct.xmlparser_static.hit_hsp_array_index_end = 0;

    /* als "Static" verwendete Variablen in der Struktur */
    parsestruct.geneprediction_static.matrixscore = 0;
    parsestruct.geneprediction_static.matrixscore_before = 0;
    parsestruct.geneprediction_static.current_frame = 0;
    parsestruct.geneprediction_static.frame_before = 0;
    parsestruct.geneprediction_static.function_stop = 0;
    parsestruct.geneprediction_static.noncodingcounter = 0;
    parsestruct.geneprediction_static.codingcounter = 0;
    parsestruct.hits_statistics.hitsnumber = 0;

    /* Speichergroesse fuer die Speicherbereiche der Statistik setzen */
    parsestruct.hits_memory = MEMORY_SIZE;

    /* Speicher fuer die Statistik reservieren und zuweisen */
    parsestruct.hits_statistics.hitsnum =
      gt_calloc(MEMORY_SIZE, sizeof (unsigned long));
    parsestruct.hits_statistics.memory =
      gt_calloc(MEMORY_SIZE, sizeof (unsigned long));

    /* Variable der Umgebungsvariablen - genometools.org */
    parsestruct.err = err;

    /* Abspeichern des XML- und des Hit-Dateinamens in der
       ParseStruct-Struktur */
    gt_str_set(parsestruct.hit_fastafile, argv[parsed_args + 2]);
    gt_str_set(parsestruct.xmlfile, argv[parsed_args]);

    /* Anzahl der Query-DNA-Eintraege */
    nrofseq = gt_bioseq_number_of_sequences(parsestruct.queryseq);
    /* Speicherbereich reservieren fuer Zeiger auf die Indices der
       Query-DNA-Eintraege in der GT_Bioseq-Struktur */
    querynum = gt_calloc(nrofseq, sizeof (unsigned long));
    /* Hash erzeugen - Eintraege: Key - Query-FASTA-Def; Value - Zeiger
       auf deren Indices in der GT_Bioseq - Struktur */
    parsestruct.queryhash = cstr_nofree_ulp_hashmap_new();

    for (loop_index = 0; loop_index < nrofseq; loop_index++)
    {
      /* Descriptions der GT_Bioseq-Struktur werden nacheinander
         abgearbeitet */
      descr_ptr_query =
        (char *) gt_bioseq_get_description(parsestruct.queryseq, loop_index);
      /* Position in der GT_Bioseq in den Speicher querynum schreiben */
      querynum[loop_index] = loop_index;

      /* Dem aktuellen Schluessel Zeige auf die Position in der GT_Bioseq
         zuordnen */
      if (!cstr_nofree_ulp_hashmap_get(parsestruct.queryhash, descr_ptr_query))
        cstr_nofree_ulp_hashmap_add(parsestruct.queryhash, descr_ptr_query,
                                   querynum + loop_index);
    }

    /* nur wenn die Option hitfile_bool auf TRUE gesetzt ist, muss die
       GT_Bioseq-Struktur bzw. die Hash-Tabelle auch fuer die Hits erzeugt
       werden; Erzeugung inkl. Hash wie beim Query-DNA-FASTA-File */
    if (ARGUMENTS(hitfile_bool))
    {
      /* Anzahl der Hit-DNA-Eintraege */
      nrofseq = gt_bioseq_number_of_sequences(parsestruct.hitseq);
      /* Speicherbereich reservieren fuer Zeiger auf die Indices der
         Hit-DNA-Eintraege in der GT_Bioseq-Struktur */
      hitnum = gt_calloc(nrofseq, sizeof (unsigned long));
      /* Hash erzeugen - Eintraege: Key - Hit-FASTA-Zeile; Value - Zeiger
         auf deren Indices in der GT_Bioseq - Struktur */
      parsestruct.hithash = cstr_nofree_ulp_hashmap_new();

      /* Hit-Fasta-File zeilenweise abarbeiten */
      for (loop_index = 0; loop_index < nrofseq; loop_index++)
      {
        /* Der Schluessel ist die Hit-FASTA-Zeile */
        descr_ptr_hit =
          (char *) gt_bioseq_get_description(parsestruct.hitseq, loop_index);
        /* Position in der GT_Bioseq in den Speicher hitnum schreiben */
        hitnum[loop_index] = loop_index;

        /* Dem aktuellen Schluessel Zeige auf die Position in der GT_Bioseq
           zuordnen */
        if (!cstr_nofree_ulp_hashmap_get(parsestruct.hithash, descr_ptr_hit))
          cstr_nofree_ulp_hashmap_add(parsestruct.hithash, descr_ptr_hit,
                                     hitnum + loop_index);
      }
    }

    /* Hashtabelle fuer die Statistik anlegen */
    parsestruct.resulthits = cstr_nofree_ulp_hashmap_new();

    /* konstruieren des Output-Filenames aus angegebenen Filename und dem
       angegebenen Format */
    switch (ARGUMENTS(outputfile_format))
    {
      case 2:
        gt_str_append_str(outputfilename, ARGUMENTS(outputtextfile_name));
        gt_str_append_cstr(outputfilename, ".");
        gt_str_append_cstr(outputfilename, "html");
        break;
      case 3:
        gt_str_append_str(outputfilename, ARGUMENTS(outputtextfile_name));
        gt_str_append_cstr(outputfilename, ".");
        gt_str_append_cstr(outputfilename, "xml");
        break;
      default:
        gt_str_append_str(outputfilename, ARGUMENTS(outputtextfile_name));
        gt_str_append_cstr(outputfilename, ".");
        gt_str_append_cstr(outputfilename, "txt");
    }

    /* Der Name des XML-Files mit den Blast-Hits ist das erste Argument
       nach dem Programmnamen */
    fp_xmlfile = gt_genfile_xopen(argv[parsed_args], "r");

    if (file_exists(gt_str_get(outputfilename)))
    {
      parsestruct.fp_outputfile = gt_genfile_xopen(gt_str_get(outputfilename),
                                                   "w+");
      gt_genfile_close(parsestruct.fp_outputfile);
    }

    /* Der Name des Outputfiles wird den eingegebenen Optionen entnommen
       oder der default-Wert output.txt verwendet */
    parsestruct.fp_outputfile = gt_genfile_xopen(gt_str_get(outputfilename),
                                                 "a+");

    if (!ARGUMENTS(hitfile_bool))
    {
      GtStr *gi_numbers_txt;
      gi_numbers_txt = gt_str_new();

      gt_str_set(gi_numbers_txt, "gi_numbers.txt");
      unsigned long row_width = 150;

      if (!gt_strarray_size(ARGUMENTS(giexpfile_name)))
      {
        gt_strarray_add_cstr(ARGUMENTS(giexpfile_name), "nt.gz");
      }

      /* Datei fuer die GI-Nr. des XML-Files  */
      parsestruct.fp_giexp_file = gt_genfile_xopen(gt_str_get(gi_numbers_txt),
                                                   "w+");

      had_err = mg_xmlparser(parsestruct_ptr, fp_xmlfile, err);

      gt_genfile_close(parsestruct.fp_giexp_file);
      gt_genfile_close(fp_xmlfile);

      if (!had_err)
      {
        parsestruct.giexp_flag = 1;

        /* Die Hit-Datei wird mit dem Modus w+ geoeffnet */
        parsestruct.fp_blasthit_file =
          gt_genfile_xopen(gt_str_get(parsestruct.hit_fastafile), "w+");

        had_err = extractginumbers(true,
                                   parsestruct.fp_blasthit_file,
                                   row_width,
                                   gi_numbers_txt,
                                   ARGUMENTS(giexpfile_name),
                                   err);

        gt_genfile_close(parsestruct.fp_blasthit_file);

        if (had_err)
        {
          had_err = 0;
        }
        /* XXX else XXX */

        if (!had_err)
        {
          parsestruct.hitseq = gt_bioseq_new(argv[parsed_args + 2], err);

          if (!parsestruct.hitseq)
          {
            had_err = -1;
          }
          if (!had_err)
          {
            /* Anzahl der Hit-DNA-Eintraege */
            nrofseq = gt_bioseq_number_of_sequences(parsestruct.hitseq);
            /* Speicherbereich reservieren fuer Zeiger auf die Indices der
               Hit-DNA-Eintraege in der GT_Bioseq-Struktur */
            hitnum = gt_calloc(nrofseq, sizeof (unsigned long));
            /* Hash erzeugen - Eintraege: Key - Hit-FASTA-Zeile;
               Value - Zeiger
               auf deren Indices in der GT_Bioseq - Struktur */
            parsestruct.hithash = cstr_nofree_ulp_hashmap_new();

            /* Hit-Fasta-File zeilenweise abarbeiten */
            for (loop_index = 0; loop_index < nrofseq; loop_index++)
            {
              /* Der Schluessel ist die Hit-FASTA-Zeile */
              descr_ptr_hit =
              (char *) gt_bioseq_get_description(parsestruct.hitseq,
                                                 loop_index);
              /* Position in der GT_Bioseq in den Speicher hitnum schreiben */
              hitnum[loop_index] = loop_index;

             /* Dem aktuellen Schluessel Zeige auf die Position in der GT_Bioseq
                zuordnen */
              if (!cstr_nofree_ulp_hashmap_get(parsestruct.hithash,
                                              descr_ptr_hit))
                cstr_nofree_ulp_hashmap_add(parsestruct.hithash, descr_ptr_hit,
                                           hitnum + loop_index);
            }
          }
        }
      }
    }

    if (!had_err)
    {
      /* Schreiben des Ausgabe-Headers */
      mg_outputwriter(parsestruct_ptr, NULL, NULL, NULL, 't', err);

      if (!ARGUMENTS(hitfile_bool))
      {
        /* Der Name des XML-Files mit den Blast-Hits ist das erste Argument
           nach dem Programmnamen */
        fp_xmlfile = gt_genfile_xopen(argv[parsed_args], "r");
      }
      parsestruct.giexp_flag = 1;

      /* Aufruf des xmlparsers; uebergeben werden: Zeiger auf die
         parser_array-Struktur der Filepointer auf die XML-Datei Zeiger auf
         die "Programmierumgebung" */
      had_err = mg_xmlparser(parsestruct_ptr, fp_xmlfile, err);
    }

    if (!had_err)
    {
      /* Schreiben des Ausgabe-Footers */
      mg_outputwriter(parsestruct_ptr, NULL, NULL, NULL, 'v', err);

      /* Ausgeben der verwendeten Hit-Def, in dem die Hashtabelle einmal
         vollstaendig durchlaufen wird */
      (void) cstr_nofree_ulp_hashmap_foreach(parsestruct.resulthits,
                                            printout_hits, &parsestruct, err);

      /* Schreiben des Ausgabe-Footers */
      mg_outputwriter(parsestruct_ptr, NULL, NULL, NULL, 'f', err);
    }

    /* Freigabe der verschiedenen reservierten Speicherbereiche */
    gt_strarray_delete(parsestruct.query_array);
    gt_strarray_delete(parsestruct.hit_array);
    gt_strarray_delete(parsestruct.hit_hsp_array);
    gt_strarray_delete(parsestruct.hits_statistics.hits_statistic);
    gt_strarray_delete(parsestruct.key_tmp);

    gt_str_delete(outputfilename);
    gt_str_delete(parsestruct.xml_tag);
    gt_str_delete(parsestruct.buf_ptr);
    gt_str_delete(parsestruct.gi_def_tmp);
    gt_str_delete(parsestruct.gi_acc_tmp);
    gt_str_delete(parsestruct.hit_gi_nr_tmp);
    gt_str_delete(parsestruct.fasta_row);
    gt_str_delete(parsestruct.matrix_info.query_dna);
    gt_str_delete(parsestruct.matrix_info.query_def);
    gt_str_delete(parsestruct.hit_fastafile);
    gt_str_delete(parsestruct.xmlfile);
    gt_str_delete(parsestruct.result_hits);

    gt_array_delete(parsestruct.matrix_info.query_from);
    gt_array_delete(parsestruct.matrix_info.query_to);
    gt_array_delete(parsestruct.matrix_info.query_frame);
    gt_array_delete(parsestruct.matrix_info.hit_frame);
    gt_array_delete(parsestruct.value_tmp);

    gt_free(querynum);
    gt_free(parsestruct.hits_statistics.hitsnum);
    gt_free(parsestruct.hits_statistics.memory);

    /* Schliessen der XML-, Output-Datei und des Hit-Sequenz-Files */
    gt_genfile_close(fp_xmlfile);
    gt_genfile_close(parsestruct.fp_outputfile);

    /* Hashtable loeschen */
    hashtable_delete(parsestruct.queryhash);
    hashtable_delete(parsestruct.resulthits);

    hashtable_delete(parsestruct.hithash);
    gt_bioseq_delete(parsestruct.hitseq);
    gt_free(hitnum);

    gt_bioseq_delete(parsestruct.queryseq);
  }

  gt_str_delete(ARGUMENTS(curl_fcgi_db));
  gt_str_delete(ARGUMENTS(outputtextfile_name));
  gt_strarray_delete(ARGUMENTS(giexpfile_name));

  /* Rueckgabe des Fehlercode */
  return had_err;
}

static enum iterator_op
printout_hits(GT_UNUSED char *key,
              unsigned long *value, void *data, GT_Error * err)
{
  /* Parsestruct-Struktur */
  ParseStruct *parsestruct_ptr = (ParseStruct *) data;

  gt_error_check(err);

  /* Position des aktuell betrachteten Schluessels */
  HITSTRUCT(stat_pos) = *value;

  /* Ausgabe erfolgt nur, wenn der Anteil des Hits ueber dem Wert des
     Metagenomethreader-Argumentes liegt */
  if ((double)
      (HITSTRUCT(hitsnum)[HITSTRUCT(stat_pos)] /
       (double) HITSTRUCT(hitsnumber)) >= ARGUMENTSSTRUCT(percent_value))
  {
    mg_outputwriter(parsestruct_ptr, NULL, NULL, NULL, 's', err);
  }
  return 0;
}
