#!/bin/sh

set -e -x

if test $# -ne 4
then
  echo "Usage: $0 <numreads> <inputfile> <mincoverage> <seedlength>"
  exit 1
fi
numreads=$1
inputfile=$2
mincoverage=$3
seedlength=$4
readlength=150
maxfreq=14
minidentity=95
readset=reads-${numreads}-ill-def.fa
scripts/simulate-reads.rb -n $numreads -l ${readlength} -i ${inputfile}

env -i bin/gt encseq encode -indexname query-idx ${readset}
env -i bin/gt encseq encode -indexname reference-idx ${inputfile}
minlength=`expr ${readlength} \* ${mincoverage}`
minlength=`expr ${minlength} \/ 100`
common="-v -ii reference-idx -qii query-idx --no-reverse -outfmt fstperquery -l ${minlength} -minidentity ${minidentity} -seedlength ${seedlength}"
env -i bin/gt seed_extend ${common} > tmp.matches
scripts/collect-mappings.rb ${readset} tmp.matches
env -i bin/gt seed_extend ${common} -maxmat 2 > tmp-maxmat.matches
scripts/collect-mappings.rb ${readset} tmp-maxmat.matches
env -i bin/gt seed_extend ${common} -maxmat 2 -use-apos > tmp-maxmat-apos.matches
scripts/collect-mappings.rb ${readset} tmp-maxmat-apos.matches
