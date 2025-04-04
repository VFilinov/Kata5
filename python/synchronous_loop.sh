#!/bin/bash -eu
set -o pipefail
{

# Runs the entire self-play process synchronously in a loop, training a single size of neural net appropriately.
# Assumes you have the cpp directory compiled and the katago executable is there.

# If using multiple machines, or even possibly many GPUs on one machine in some cases, then this is NOT the
# recommended method, instead it is better to run all steps simultaneously and asynchronously. See SelfplayTraining.md in
# the root of the KataGo repo for more details.

if [[ $# -lt 5 ]]
then
    echo "Usage: $0 NAMEPREFIX BASEDIR TRAININGNAME MODELKIND USEGATING"
    echo "Assumes katago is already built in the 'cpp' directory of the KataGo repo and the executable is present at cpp/katago."
    echo "NAMEPREFIX string prefix for this training run, try to pick something globally unique. Will be displayed to users when KataGo loads the model."
    echo "BASEDIR containing selfplay data and models and related directories"
    echo "TRANINGNAME name to prefix models with, specific to this training daemon"
    echo "MODELKIND what size model to train, like 'b10c128', see ../modelconfigs.py"
    echo "USEGATING = 1 to use gatekeeper, 0 to not use gatekeeper"
    exit 0
fi
NAMEPREFIX="$1"
shift
BASEDIRRAW="$1"
shift
TRAININGNAME="$1"
shift
MODELKIND="$1"
shift
USEGATING="$1"
shift

# Run selfplay
USESELFPLAY=1

#Run shuffle
USESHUFFLE=1
# Parameter for shuffle
NOFASTVALIDATE=0

#Run train
USETRAIN=1

# Parameter training for use files 
REPEATFILES=0

#Run export
USEEXPORT=1

#Run save image of training
USEIMAGE=1

#Break loop
USELOOPBREAK=0


if [ "$USEGATING" -gt 1 ]
then
    USESELFPLAY=$(("$USEGATING" & 2))
    USESHUFFLE=$(("$USEGATING" & 4))
    USETRAIN=$(("$USEGATING" & 8))
    USEEXPORT=$(("$USEGATING" & 16))
    USEIMAGE=$(("$USEGATING" & 32))
    REPEATFILES=$(("$USEGATING" & 64))
    NOFASTVALIDATE=$(("$USEGATING" & 128))
    USELOOPBREAK=$(("$USEGATING" & 256))
    USEGATING=$(("$USEGATING" & 1))
fi

if [ "$NOFASTVALIDATE" -ne 0 ]
then
  USESHUFFLE=1
fi
if [ "$REPEATFILES" -ne 0 ]
then
  USETRAIN=1
fi

#NOFASTVALIDATE=1

BASEDIR="$(realpath "$BASEDIRRAW")"
#GITROOTDIR="$(git rev-parse --show-toplevel)"
LOGSDIR="$BASEDIR"/logs
SCRATCHDIR="$BASEDIR"/shufflescratch

# Create all the directories we need
mkdir -p "$BASEDIR"
mkdir -p "$LOGSDIR"
mkdir -p "$SCRATCHDIR"
mkdir -p "$BASEDIR"/selfplay
mkdir -p "$BASEDIR"/gatekeepersgf

# Parameters for the training run
# NOTE: You may want to adjust the below numbers.
# NOTE: You probably want to edit settings in cpp/configs/training/selfplay1.cfg
# NOTE: You probably want to edit settings in cpp/configs/training/gatekeeper1.cfg
# Such as what board sizes and rules, you want to learn, number of visits to use, etc.

# Also, the parameters below are relatively small, and probably
# good for less powerful hardware and tighter turnaround during very early training, but if
# you have strong hardware or are later into a run you may want to reduce the overhead by scaling
# these numbers up and doing more games and training per cycle, exporting models less frequently, etc.

NUM_GAMES_PER_CYCLE=500 # Every cycle, play this many games
NUM_THREADS_FOR_SHUFFLING=16
NUM_TRAIN_SAMPLES_PER_EPOCH=200000  # Training will proceed in chunks of this many rows, subject to MAX_TRAIN_PER_DATA.
MAX_TRAIN_PER_DATA=8 # On average, train only this many times on each data row. Larger numbers may cause overfitting.
NUM_TRAIN_SAMPLES_PER_SWA=80000  # Stochastic weight averaging frequency.
BATCHSIZE=256 # For lower-end GPUs 64 or smaller may be needed to avoid running out of GPU memory.
SHUFFLE_MINROWS=200000 # Require this many rows at the very start before beginning training.
MAX_TRAIN_SAMPLES_PER_CYCLE=500000  # Each cycle will do at most this many training steps.
SHUFFLE_KEEPROWS=600000 # Needs to be larger than MAX_TRAIN_SAMPLES_PER_CYCLE, so the shuffler samples enough rows each cycle for the training to use.
SHUFFLE_ROWS_PER_FILE=70000

# Paths to the selfplay and gatekeeper configs that contain board sizes, rules, search parameters, etc.
SELFPLAY_CONFIG=../selfplay.cfg
GATING_CONFIG=../gatekeeper.cfg
#KATAGO=C:/Users/VFilinov/source/repos/KataGo-2023-13/bin/Debug/katago
KATAGO=../engine/katago
OUTPUTDIR="$BASEDIR"

# Begin cycling forever, running each step in order.
set -x
while true
do

    if [ "$USESELFPLAY" -ne 0 ]
    then
        echo "Selfplay"
        time "$KATAGO" selfplay -max-games-total "$NUM_GAMES_PER_CYCLE" -output-dir "$BASEDIR"/selfplay -models-dir "$BASEDIR"/models -config "$SELFPLAY_CONFIG" | tee -a "$BASEDIR"/selfplay/stdout.txt
    fi

    if [ "$USESHUFFLE" -ne 0 ]
    then
        echo "Shuffle"
        # Skip validate since peeling off 5% of data is actually a bit too chunky and discrete when running at a small scale, and validation data
        # doesn't actually add much to debugging a fast-changing RL training.
        (
        if [ "$NOFASTVALIDATE" -ne 0 ]
        then
            time SKIP_VALIDATE="" ./shuffle.sh "$BASEDIR" "$SCRATCHDIR" "$NUM_THREADS_FOR_SHUFFLING" "$BATCHSIZE" -min-rows "$SHUFFLE_MINROWS" -keep-target-rows "$SHUFFLE_KEEPROWS" -approx-rows-per-out-file "$SHUFFLE_ROWS_PER_FILE" | tee -a "$BASEDIR"/logs/outshuffle.txt
        else
            time SKIP_VALIDATE=1 ./shuffle.sh "$BASEDIR" "$SCRATCHDIR" "$NUM_THREADS_FOR_SHUFFLING" "$BATCHSIZE" -min-rows "$SHUFFLE_MINROWS" -keep-target-rows "$SHUFFLE_KEEPROWS" -approx-rows-per-out-file "$SHUFFLE_ROWS_PER_FILE" | tee -a "$BASEDIR"/logs/outshuffle.txt
        fi
        )
    fi

    if [ "$USETRAIN" -ne 0 ]
    then
        echo "Train"
        (
            if [ "$REPEATFILES" -ne 0 ]
            then
               time ./train.sh "$BASEDIR" "$TRAININGNAME" "$MODELKIND" "$BATCHSIZE" main -samples-per-epoch "$NUM_TRAIN_SAMPLES_PER_EPOCH" -swa-period-samples "$NUM_TRAIN_SAMPLES_PER_SWA" -quit-if-no-data -stop-when-train-bucket-limited -max-train-bucket-per-new-data "$MAX_TRAIN_PER_DATA" -max-train-bucket-size "$MAX_TRAIN_SAMPLES_PER_CYCLE" 
            else
               time ./train.sh "$BASEDIR" "$TRAININGNAME" "$MODELKIND" "$BATCHSIZE" main -samples-per-epoch "$NUM_TRAIN_SAMPLES_PER_EPOCH" -swa-period-samples "$NUM_TRAIN_SAMPLES_PER_SWA" -quit-if-no-data -stop-when-train-bucket-limited -no-repeat-files -max-train-bucket-per-new-data "$MAX_TRAIN_PER_DATA" -max-train-bucket-size "$MAX_TRAIN_SAMPLES_PER_CYCLE"
            fi
        )
    fi

    if [ "$USEEXPORT" -ne 0 ]
    then
        echo "Export"
        time ./export_model_for_selfplay.sh "$NAMEPREFIX" "$BASEDIR" "$USEGATING" | tee -a "$BASEDIR"/logs/outexport.txt
    fi

    if [ "$USEIMAGE" -ne 0 ]
    then
        echo "Save train image"
        time ./view_loss.sh "$BASEDIR" "$TRAININGNAME" "$OUTPUTDIR" | tee -a "$BASEDIR"/logs/outsaveimage.txt
    fi

    if [ "$USEGATING" -ne 0 ]
    then
        echo "Gatekeeper"
        time "$KATAGO" gatekeeper -rejected-models-dir "$BASEDIR"/rejectedmodels -accepted-models-dir "$BASEDIR"/models/ -sgf-output-dir "$BASEDIR"/gatekeepersgf/ -test-models-dir "$BASEDIR"/modelstobetested/ -config "$GATING_CONFIG" -quit-if-no-nets-to-test | tee -a "$BASEDIR"/gatekeepersgf/stdout.txt
    fi

    if [ "$USELOOPBREAK" -ne 0 ]
    then
        break
    fi
done

exit 0
}
