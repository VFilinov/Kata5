#!/bin/bash -eu
set -o pipefail
{
#Takes any models in torchmodels_toexport/ and outputs a cuda-runnable model file to modelstobetested/
#Takes any models in torchmodels_toexport_extra/ and outputs a cuda-runnable model file to models_extra/
#Should be run periodically.

if [[ $# -ne 3 ]]
then
    echo "Usage: $0 BASEDIR MODELDIR FILE_NPZ"
    echo "Currently expects to be run from within the 'python' directory of the KataGo repo, or otherwise in the same dir as export_model.py."
    echo "BASEDIR containing selfplay data and models and related directories"
    echo "MODELDIR containing model"
    echo "FILE_NPZ file npz for test"
    exit 0
fi
BASEDIR="$1"
shift
MODELDIR="$1"
shift
FILE_NPZ="$1"
shift

CHKPNT="$BASEDIR"/models/"$MODELDIR"/model.ckpt

set -x
python ./forward_model.py \
        -npz "$FILE_NPZ" \
        -checkpoint "$CHKPNT" \
        -pos-len 19 \
        -use-swa
exit 0
}
