--[[
  Copyright (c) 2007 Gordon Gremme <gremme@zbh.uni-hamburg.de>
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
]]

require 'gt'
require 'lfs'

function usage()
  io.stderr:write(string.format("Usage: %s PNG_dir reality_file " ..
                                "prediction_file\n", arg[0]))
  io.stderr:write("Evaluate prediction_file against reality_file and write " ..
                  "out PNGs to PNG_dir.\n")
  os.exit(1)
end

if #arg == 3 then
  png_dir  = arg[1]
  reality_file = arg[2]
  prediction_file = arg[3]
  -- make sure png_dir is a directory or create it
  rval, err = lfs.attributes(png_dir, "mode")
  if rval then
    if rval ~= "directory" then
      io.stderr:write(string.format("PNG_dir '%s' is not a directory\n",
                                    png_dir))
      os.exit(1)
    end
  else
    -- not successfull, try to create directory
    rval, err = lfs.mkdir(png_dir)
    if not rval then
      io.stderr:write(string.format("could not create directory '%s': %s",
                                    png_dir, err))
      os.exit(1)
    end
  end
else
  usage()
end

function render_to_png(png_file, seqid)
  assert(seqid)
  local diagram = gt.diagram_new(feature_index, range, seqid)
  local render =  gt.render_new()
  render:to_png(diagram, png_file, width)
end

function get_coverage(seqid, maxdist)
  assert(seqid)
  local maxdist = maxdist or 0
  local features = feature_index:get_features_for_seqid(seqid)
  local starpos, endpos
  local minstartpos = nil
  local maxendpos = nil
  local ranges = {}
  local coverage = {}

  -- collect all feature ranges
  for i, feature in ipairs(features) do
    table.insert(ranges, feature:get_range())
  end
  -- sort feature ranges
  ranges = gt.ranges_sort(ranges)

  -- compute and store coverage
  for i, range in ipairs(ranges) do
    startpos, endpos = range:get_start(), range:get_end()
    if i == 1 then
      minstartpos = startpos
      maxendpos   = endpos
    else
      -- assert(startpos >= minstartpos)
      if (startpos > maxendpos + maxdist) then
        -- new region started
        table.insert(coverage, gt.range_new(minstartpos, maxendpos))
        minstartpos = startpos
        maxendpos   = endpos
      else
        -- continue old region
        maxendpos = (endpos > maxendpos) and endpos or maxendpos
      end
    end
  end
  -- add last region
  table.insert(coverage, gt.range_new(minstartpos, maxendpos))
  return coverage
end

function contains_marked_feature(features)
  for i, feature in ipairs(features) do
    if feature:contains_marked() then
      return true
    end
  end
  return false
end

function write_marked_regions(seqid, filenumber, maxdist)
  assert(seqid)
  local coverage = get_coverage(seqid, maxdist)
  for i, r in ipairs(coverage) do
    local features = feature_index:get_features_for_range(seqid, r)
    if contains_marked_feature(features) then
      range = r
      local filename = png_dir .. "/" .. filenumber .. ".png"
      io.write(string.format("writing file '%s'\n", filename))
      render_to_png(filename, seqid)
      filenumber = filenumber + 1
    end
  end
  return filenumber
end

-- process input files
reality_stream = gt.gff3_in_stream_new_sorted(reality_file)
prediction_stream = gt.gff3_in_stream_new_sorted(prediction_file)
stream_evaluator = gt.stream_evaluator_new(reality_stream, prediction_stream)

feature_index = gt.feature_index_new()
feature_visitor = gt.feature_visitor_new(feature_index)

stream_evaluator:evaluate(feature_visitor)
stream_evaluator:show()

-- write results
filenumber = 1
width = 1600
for _, seqid in ipairs(feature_index:get_seqids()) do
  print(string.format("seqid '%s'", seqid))
  range = feature_index:get_range_for_seqid(seqid)
  filenumber = write_marked_regions(seqid, filenumber)
end
