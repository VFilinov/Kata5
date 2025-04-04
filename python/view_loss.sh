#!/bin/bash -eu
set -o pipefail
{

if [[ $# -ne 3 ]]
then
    echo "Usage: $0 BASEDIR MODELKIND OUTPUTDIR"
    echo "BASEDIR containing selfplay data and models and related directories"
    echo "TRAININGNAME what size model to train, like b10c128, see ../modelconfigs.py"
    echo "OUTPUTDIR - path for save image loss.png"
    exit 0
fi
BASEDIR="$1"
shift
MODELKIND="$1"
shift
OUTPUTDIR="$1"
shift

#------------------------------------------------------------------------------

time python ./view_loss.py \
     -traindir "$BASEDIR"/train/ \
     -model "$TRAININGNAME" \
     -outputdir "$OUTPUTDIR" 
exit 0
}
