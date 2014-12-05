require "fileutils"
Name "gt seqfilter -minlength"
Keywords "gt_seqfilter"
Test do
  FileUtils.copy("#{$testdata}nGASP/protein_100.fas", ".")
  run_test "#{$bin}gt seqfilter -minlength 1000 " \
    "./protein_100.fas"
  run "diff #{last_stdout} #{$testdata}nGASP/protein_long.fas"
end

Name "gt seqfilter -maxlength"
Keywords "gt_seqfilter"
Test do
  FileUtils.copy("#{$testdata}nGASP/protein_100.fas", ".")
  run_test "#{$bin}gt seqfilter -maxlength 499 " \
    "./protein_100.fas"
  run "diff #{last_stdout} #{$testdata}nGASP/protein_short.fas"
end

Name "gt seqfilter -maxseqnum"
Keywords "gt_seqfilter"
Test do
  FileUtils.copy("#{$testdata}nGASP/protein_100.fas", ".")
  run_test "#{$bin}gt seqfilter -maxseqnum 10 " \
    "./protein_100.fas"
  run "diff #{last_stdout} #{$testdata}nGASP/protein_10.fas"
end

Name "gt seqfilter -step"
Keywords "gt_seqfilter"
Test do
  FileUtils.copy("#{$testdata}nGASP/protein_100.fas", ".")
  run_test "#{$bin}gt seqfilter -step 10 " \
    "./protein_100.fas"
  run "diff #{last_stdout} #{$testdata}nGASP/protein_10th.fas"
end

Name "gt seqfilter -sample"
Keywords "gt_seqfilter"
Test do
  FileUtils.copy("#{$testdata}nGASP/protein_100.fas", ".")
  run_test "#{$bin}gt seqfilter -sample 0.5 " \
    "./protein_100.fas"
end
