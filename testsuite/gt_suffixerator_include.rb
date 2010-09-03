def outoptionsnobck
  return "-tis -suf -des -sds -ssp -lcp -bwt"
end

def outoptions
  return outoptionsnobck + " -bck"
end

def trials()
  return "-scantrials 10 -multicharcmptrials 1000"
end

def checksfx(parts,pl,withsmap,sat,cmp,doubling,filelist)
  extra=withsmap
  if cmp
    extra=extra + " -cmpcharbychar"
    if doubling
      extra=extra + " -maxdepth"
    end
  end
  filearg=""
  filelist.each do |filename|
    filearg += "#{$testdata}#{filename} "
  end
  run_test "#{$bin}gt suffixerator -v -parts #{parts} -pl #{pl} " +
           "-algbds 10 31 80 #{extra} #{outoptions} " +
           "-indexname sfx -db " + filearg
  run_test "#{$bin}gt dev sfxmap #{trials()} #{outoptions} -v sfx",
           :maxtime => 600
end

def checkdc(filelist)
  filearg=""
  filelist.each do |filename|
    filearg += "#{$testdata}#{filename} "
  end
  run_test "#{$bin}gt suffixerator -v -pl -dc 64 -suf -ssp -tis " +
           "-indexname sfx -db " + filearg
  run_test "#{$bin}gt dev sfxmap #{trials()} -suf -tis -ssp -v sfx",
           :maxtime => 600
  run_test "#{$bin}gt suffixerator -v -pl -parts 3 -dc 64 -suf -tis " +
           "-indexname sfx3 -db " + filearg
  run "diff sfx3.suf sfx.suf"
end

def flattenfilelist(filelist)
  s=""
  filelist.each do |f|
    s += "#{$testdata}#{f} "
  end
  return s
end

def checkbwt(filelist)
  filearg=""
  filelist.each do |filename|
    filearg += "#{$testdata}#{filename} "
  end
  run_test "#{$bin}gt suffixerator -pl #{outoptions} -indexname sfx -db " +
           flattenfilelist(filelist)
end

def runsfxfail(args)
  Name "gt suffixerator failure"
  Keywords "gt_suffixerator"
  Test do
    run_test "#{$bin}gt suffixerator -tis " + args,:retval => 1
  end
end

allfiles = []
all_fastafiles = ["Atinsert.fna",
                  "Duplicate.fna",
                  "Random-Small.fna",
                  "Random.fna",
                  "Random159.fna",
                  "Random160.fna",
                  "RandomN.fna",
                  "TTT-small.fna",
                  "trna_glutamine.fna",
                  "atC99826.fna"]

allfiles += all_fastafiles
allfiles += (all_genbankfiles = all_fastafiles.collect{ |f|
                                                        f.gsub(".fna",".gbk")
                                                      })
allfiles += (all_emblfiles = all_fastafiles.collect{ |f|
                                                     f.gsub(".fna",".embl")
                                                   })

allmultifiles = []
all_multifastafiles = ["Atinsert.fna",
                       "Duplicate.fna",
                       "Random159.fna",
                       "Random160.fna"]

allmultifiles += all_multifastafiles
allmultifiles += (all_multigenbankfiles = all_multifastafiles.collect{ |f|
                                                         f.gsub(".fna",".gbk")
                                                                     })
allmultifiles += (all_multiemblfiles = all_multifastafiles.collect{ |f|
                                                         f.gsub(".fna",".embl")
                                                                  })

all_fastqfiles = ["fastq_long.fastq",
                  "test10_multiline.fastq",
                  "test1.fastq",
                  "test5_tricky.fastq"]

allmultifiles += all_fastqfiles
allfiles += all_fastqfiles

alldir = ["fwd","cpl","rev","rcl"]

# put the tests with paircmp, maxpair, patternmatch, into a file gt_idxmatch

{"FASTA" => all_fastafiles,
 "EMBL" => all_emblfiles,
 "GenBank" => all_genbankfiles,
 "FastQ" => all_fastqfiles}.each do |k,filelist|
    Name "gt suffixerator (#{k})"
    Keywords "gt_suffixerator_tis"
    Test do
    run_test "#{$bin}gt suffixerator -tis -ssp -indexname sfx -db " +
             flattenfilelist(filelist)
    run_test "#{$bin}gt dev sfxmap -ssp -tis sfx"
    filelist.each do |filename|
      run_test "#{$bin}gt suffixerator -tis -ssp -indexname sfx -db " +
               "#{$testdata}#{filename}"
      run_test "#{$bin}gt dev sfxmap -ssp -tis sfx"
    end
  end
end

Name "gt suffixerator file of reads of equal length"
Keywords "gt_suffixerator_reads"
Test do
  run_test "#{$bin}/gt suffixerator -des -tis -ssp -dna " +
           "-db #{$testdata}U89959_genomic.fas -indexname u8idx"
  run_test "#{$bin}/gt simreads -coverage 10 -len 100 -force -o u8.reads u8idx"
  run_test "#{$bin}/gt suffixerator -v -suf -lcp -des -sds -ssp -tis -dna -db u8.reads"
  run "grep -q '# init character encoding (eqlen,' #{$last_stdout}"
  run_test "#{$bin}/gt dev sfxmap -suf -lcp -des -sds -ssp u8.reads"
end

Name "gt qsortbench"
Keywords "gt_qsortbench"
Test do
  ["thomas","system","inlinedptr","inlinedarr"].each do |impl|
    ["-aqsort","-permute",""].each do |option|
      run_test "#{$bin}gt dev qsortbench -size 10000 -maxval 1000 -impl #{impl} #{option}"
    end
  end
end

all_fastafiles.each do |filename|
  Name "gt suffixerator -dc 64 -parts 1+3 #{filename}"
  Keywords "gt_suffixerator"
  Test do
    checkdc([filename])
  end
end

Name "gt suffixerator -dc 64 -parts 1+3 all-fastafiles"
Keywords "gt_suffixerator"
Test do
  checkdc(all_fastafiles)
end

Name "gt suffixerator paircmp"
Keywords "gt_suffixerator"
Test do
  run_test "#{$bin}gt dev paircmp -a ac 11" # mv to idx 
end

Name "gt suffixerator patternmatch"
Keywords "gt_suffixerator"
Test do
  run_test "#{$bin}gt suffixerator -db #{$testdata}Atinsert.fna " +
           "-indexname sfx -dna -bck -suf -tis -pl"
  run_test "#{$bin}gt dev patternmatch -samples 10000 -minpl 10 -maxpl 15 " +
           " -bck -imm -ii sfx"
  run_test "#{$bin}gt dev patternmatch -samples 10000 -ii sfx"
end

alldir.each do |dir|
  {"FASTA" => all_fastafiles,
   "EMBL" => all_emblfiles,
   "GenBank" => all_genbankfiles,
   "FastQ" => all_fastqfiles}.each do |k,filelist|
    Name "gt suffixerator #{dir} (#{k})"
    Keywords "gt_suffixerator"
    Test do
       run_test "#{$bin}gt suffixerator -dir #{dir} -tis -suf -bwt -lcp " +
                "-indexname sfx -pl -db " +
                flattenfilelist(filelist)
       run_test "#{$bin}gt suffixerator -storespecialcodes -dir #{dir} -tis " +
                "-suf -lcp -indexname sfx -pl -db " +
                flattenfilelist(filelist)
       run_test "#{$bin}gt suffixerator -tis -bwt -lcp -pl -ii sfx"
    end
  end
end

runsfxfail "-indexname sfx -db /nothing"
runsfxfail "-indexname /nothing/sfx -db #{$testdata}TTT-small.fna"
runsfxfail "-smap /nothing -db #{$testdata}TTT-small.fna"
runsfxfail "-dna -db #{$testdata}sw100K1.fsa"
runsfxfail "-protein -dir cpl -db #{$testdata}sw100K1.fsa"
runsfxfail "-dna -db #{$testdata}Random.fna RandomN.fna"
runsfxfail "-dna -suf -pl 10 -db #{$testdata}Random.fna"
runsfxfail "-dna -tis -sat plain -db #{$testdata}TTT-small.fna"

allmultifiles.each do |filename|
  Name "gt suffixerator sfxmap-failure #{filename}"
  Keywords "gt_suffixerator"
  Test do
    run_test "#{$bin}gt suffixerator -tis -dna -indexname localidx " +
             "-db #{$testdata}#{filename}"
    run_test "#{$bin}gt suffixerator -suf -lcp -pl -dir rev -ii localidx"
    run_test "#{$bin}gt dev sfxmap -tis -des localidx",
             :retval => 1
    run_test "#{$bin}gt dev sfxmap -tis -ssp localidx",
             :retval => 1
    run_test "#{$bin}gt dev sfxmap -des localidx",
             :retval => 1
    run_test "#{$bin}gt dev sfxmap -ssp localidx",
             :retval => 1
    run_test "#{$bin}gt dev sfxmap -tis -bck localidx",
             :retval => 1
  end
end

Name "gt suffixerator bwt"
Keywords "gt_suffixerator"
Test do
  checkbwt(all_fastafiles)
end

allfiles.each do |filename|
  Name "gt suffixerator uint32 #{filename}"
  Keywords "gt_suffixerator"
  Test do
    run_test "#{$bin}gt suffixerator -tis -indexname sfx -sat uint32 " +
             "-pl -db #{$testdata}#{filename}"
  end
end

1.upto(3) do |parts|
  [0,2].each do |withsmap|
    extra=""
    if withsmap == 1
      extra="-protein"
      extraname="protein"
    elsif withsmap == 2
      extra="-smap TransProt11"
      extraname="TransProt11"
    end
    if parts == 1
     doubling=true
    else
     doubling=false
    end
    Name "gt suffixerator+sfxmap protein #{extraname} #{parts} parts"
    Keywords "gt_suffixerator"
    Test do
      checksfx(parts,2,extra,"direct",true,doubling,
               ["sw100K1.fsa","sw100K2.fsa"])
      checksfx(parts,2,extra,"bytecompress",true,doubling,
               ["sw100K1.fsa","sw100K2.fsa"])
    end
  end
end

0.upto(2) do |cmpval|
  1.upto(2) do |parts|
    ["direct", "bit", "uchar", "ushort", "uint"].each do |sat|
      [0,2].each do |withsmap|
        extra=""
        if withsmap == 1
          extra="-dna"
          extraname="dna"
        elsif withsmap == 2
          extra="-smap TransDNA"
          extraname="TransDNA"
        end
        doublingname=""
        if cmpval == 0
          cmp=false
          doubling=false
        elsif cmpval == 1
          cmp=true
          doubling=false
        else
          cmp=true
          doubling=true
          doublingname=" doubling "
        end
        Name "gt suffixerator+sfxmap dna #{extraname} #{sat} " +
             "#{parts} parts #{doubling}"
        Keywords "gt_suffixerator"
        Test do
          checksfx(parts,1,extra,sat,cmp,doubling,["Random-Small.fna"])
          checksfx(parts,1,extra,sat,cmp,doubling,["Random-Small.gbk"])
          checksfx(parts,1,extra,sat,cmp,doubling,["Random-Small.embl"])
          checksfx(parts,3,extra,sat,cmp,doubling,["Random.fna"])
          checksfx(parts,3,extra,sat,cmp,doubling,["Random.gbk"])
          checksfx(parts,3,extra,sat,cmp,doubling,["Random.embl"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.fna"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.gbk"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.embl"])
          checksfx(parts,2,extra,sat,cmp,doubling,["trna_glutamine.fna"])
          checksfx(parts,2,extra,sat,cmp,doubling,["trna_glutamine.gbk"])
          checksfx(parts,2,extra,sat,cmp,doubling,["trna_glutamine.embl"])
          checksfx(parts,1,extra,sat,cmp,doubling,["TTT-small.fna"])
          checksfx(parts,1,extra,sat,cmp,doubling,["TTT-small.gbk"])
          checksfx(parts,1,extra,sat,cmp,doubling,["TTT-small.embl"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.fna","Random.fna",
                                          "Atinsert.fna"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.gbk","Random.gbk",
                                          "Atinsert.gbk"])
          checksfx(parts,3,extra,sat,cmp,doubling,["RandomN.embl","Random.embl",
                                          "Atinsert.embl"])
        end
      end
    end
  end
end

def checkmapped(args)
  Name "gt suffixerator checkmapped"
  Keywords "gt_suffixerator gttestdata"
  Test do
    run_test "#{$bin}gt suffixerator #{outoptions} -algbds 3 34 90 " +
             "-indexname sfxidx #{args}",
             :maxtime => 1200
    run_test "#{$bin}gt dev sfxmap #{outoptions} #{trials()} -v sfxidx",
             :maxtime => 2400
    run_test "#{$bin}gt dev sfxmap #{outoptionsnobck} -stream -v sfxidx",
             :maxtime => 2400
  end
end

def grumbach()
  return "#{$gttestdata}DNA-mix/Grumbach.fna/"
end


if $gttestdata then
  checkmapped("-db " +
              "#{$gttestdata}Iowa/at100K1 " +
              "#{grumbach()}Wildcards.fna " +
              "#{grumbach()}chntxx.fna " +
              "#{grumbach()}hs5hcmvcg.fna " +
              "#{grumbach()}humdystrop.fna " +
              "#{grumbach()}humghcsa.fna " +
              "#{grumbach()}humhdabcd.fna " +
              "#{grumbach()}humhprtb.fna " +
              "#{grumbach()}mipacga.fna " +
              "#{grumbach()}mpocpcg.fna " +
              "#{grumbach()}ychrIII.fna " +
              "-parts 3 -pl")

  checkmapped("-parts 1 -pl -db #{$gttestdata}swissprot/swiss10K " +
              "#{$gttestdata}swissprot/swiss1MB")

  checkmapped("-db #{$gttestdata}swissprot/swiss10K " +
              "#{$gttestdata}swissprot/swiss1MB -parts 3 -pl")

  checkmapped("-parts 2 -pl -smap TransDNA -db  #{$gttestdata}Iowa/at100K1")

  checkmapped("-db #{$gttestdata}swissprot/swiss10K -parts 1 -pl -smap " +
              "TransProt11")
end

SATS = ["direct", "bytecompress", "eqlen", "bit", "uchar", "ushort", "uint32"]

EQLENDNAFILE = {:filename => "#{$testdata}/test1.fasta",
                :desc => "equal length DNA",
                :msgs => {
                  "bytecompress" => "cannot use bytecompress on DNA sequences"}}
DNAFILE   = {:filename => "#{$testdata}/at1MB",
             :desc => "non-equal length DNA",
             :msgs => {
                "bytecompress" => "cannot use bytecompress on DNA sequences",
                "eqlen" => "all sequences are of equal length and no " + \
                "sequence contains"}}
EQLENAAFILE = {:filename => "#{$testdata}/trembl-eqlen.faa",
                :desc => "equal length AA",
                :msgs => {
                  "eqlen" => "as the sequence is not DNA",
                  "bit" => "as the sequence is not DNA",
                  "uchar" => "as the sequence is not DNA",
                  "ushort" => "as the sequence is not DNA",
                  "uint32" => "as the sequence is not DNA"}}
AAFILE    = {:filename => "#{$testdata}/trembl.faa",
                :desc => "non-equal length AA",
                :msgs => {
                  "eqlen" => "as the sequence is not DNA",
                  "bit" => "as the sequence is not DNA",
                  "uchar" => "as the sequence is not DNA",
                  "ushort" => "as the sequence is not DNA",
                  "uint32" => "as the sequence is not DNA"}}

SATTESTFILES = [EQLENDNAFILE, DNAFILE, EQLENAAFILE, AAFILE]

SATTESTFILES.each do |file|
  SATS.each do |sat|
    Name "gt suffixerator sat #{sat} -> #{file[:desc]}"
    Keywords "gt_suffixerator sats"
    Test do
      if !file[:msgs][sat].nil? then
        retval = 1
      else
        retval = 0
      end
      run_test "#{$bin}/gt suffixerator -sat #{sat} -v -suf -lcp -des -sds " + \
               "-ssp -tis -db #{file[:filename]} -indexname myidx", \
               :retval => retval
      if !file[:msgs][sat].nil? then
        grep($last_stderr, /#{file[:msgs][sat]}/)
      end
      run_test "#{$bin}/gt dev sfxmap -suf -lcp -des -sds -ssp myidx", \
               :retval => retval
    end
  end
end  

Name "gt uniquesub"
Keywords "gt_uniquesub"
Test do
  run "#{$scriptsdir}runmkfm.sh #{$bin}gt 1 . Combined.fna #{$testdata}at1MB"
  run_test "#{$bin}gt uniquesub -output sequence querypos -min 10 " +
           "-max 20 -fmi Combined.fna -query #{$testdata}U89959_genomic.fas", \
           :maxtime => 600
end
