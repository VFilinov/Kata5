#!/bin/bash -eu
set -o pipefail
{

if [[ $# -ne 1 ]]
then
    echo "Usage: $0 BASEDIR MODELKIND OUTPUTDIR"
    echo "MATCHDIR containing match games"
    exit 0
fi
MATCHDIR="$1"
shift

#------------------------------------------------------------------------------

time python ./summarize_sgfs.py "$MATCHDIR" \
      2>&1 | tee "$MATCHDIR"/../ooutmatch.txt &
wait
exit 0
}
