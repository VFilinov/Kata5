#!/bin/bash -eu
{
set -o pipefail

#set +u
#source ~/.bashrc
#conda activate learn
#set -u

SELFPLAY_CONFIG="$(realpath selfplay.cfg)"
GATING_CONFIG="$(realpath gatekeeper.cfg)"

# Begin cycling forever, running each step in order.
set -x
while true
do
    source train.cfg

    BASEDIR="$(realpath "$DATADIR")"
    LOGSDIR="$BASEDIR"/logs
    SCRATCHDIR="$BASEDIR"/shufflescratch
    MODELKIND="$TRAININGNAME""$TRAININGMOD"
    GATEDIR="$BASEDIR"/gatekeeper
    COPY_MODEL="$BASEDIR"/"$NAMEPREFIX".gz
    if [[ "$PACK_MODEL" = "none" ]]
    then
    (
      COPY_MODEL="$BASEDIR"/"$NAMEPREFIX".bin
    )
    fi
    
# Create all the directories we need
    mkdir -p "$BASEDIR"
    mkdir -p "$LOGSDIR"
    mkdir -p "$SCRATCHDIR"
    mkdir -p "$BASEDIR"/selfplay
    mkdir -p "$GATEDIR"

    if [[ $USEGATING -ne 0 ]]
    then
    (
       echo "Gatekeeper"
       time ./engine/katago gatekeeper -rejected-models-dir "$BASEDIR"/rejectedmodels -accepted-models-dir "$BASEDIR"/models/ -sgf-output-dir "$GATEDIR" -test-models-dir "$BASEDIR"/modelstobetested/ -config "$GATING_CONFIG" -required-candidate-win-prop "$GATE_PROP" -copy-accepted-model "$COPY_MODEL" -quit-if-no-nets-to-test | tee -a "$LOGSDIR"/outgatekeeper.txt
    )
    fi

    if [[ $USESELFPLAY -ne 0 ]]
    then
    (
      echo "Selfplay"
      time ./engine/katago selfplay -max-games-total "$NUM_GAMES_PER_CYCLE" -output-dir "$BASEDIR"/selfplay -models-dir "$BASEDIR"/models -config "$SELFPLAY_CONFIG" -openings "$OPENINGS" | tee -a "$LOGSDIR"/outselfplay.txt
    )
    fi

    cd train

    if [[ $USESHUFFLE -ne 0 ]]
    then
    (
        echo "Shuffle"
        (
            # Skip validate since peeling off 5% of data is actually a bit too chunky and discrete when running at a small scale, and validation data
            # doesn't actually add much to debugging a fast-changing RL training.
            time ./shuffle.sh "$BASEDIR" "$SCRATCHDIR" "$NUM_THREADS_FOR_SHUFFLING" "$BATCHSIZE" "$SKIP_VALIDATE" -approx-rows-per-out-file "$SHUFFLE_ROWS_PER_FILE" -min-rows "$SHUFFLE_MINROWS" -keep-target-rows "$SHUFFLE_KEEPROWS" -taper-window-scale "$TAPER_WINDOW_SCALE" | tee -a "$LOGSDIR"/outshuffle.txt
        )
    )
    fi

    if [[ $USETRAIN -ne 0 ]]
    then
    (
        echo "Train"
        if [[ $TRAIN_REPEAT_FILES -ne 0 ]]
        then
          time ./train.sh "$BASEDIR" "$TRAININGNAME" "$MODELKIND" "$BATCHSIZE" main -samples-per-epoch "$NUM_TRAIN_SAMPLES_PER_EPOCH" -swa-period-samples "$NUM_TRAIN_SAMPLES_PER_SWA" -quit-if-no-data -stop-when-train-bucket-limited -max-train-bucket-per-new-data "$MAX_TRAIN_PER_DATA" -max-train-bucket-size "$MAX_TRAIN_SAMPLES_PER_CYCLE" | tee -a "$LOGSDIR"/outtrain.txt
        else
          time ./train.sh "$BASEDIR" "$TRAININGNAME" "$MODELKIND" "$BATCHSIZE" main -samples-per-epoch "$NUM_TRAIN_SAMPLES_PER_EPOCH" -swa-period-samples "$NUM_TRAIN_SAMPLES_PER_SWA" -quit-if-no-data -stop-when-train-bucket-limited -no-repeat-files -max-train-bucket-per-new-data "$MAX_TRAIN_PER_DATA" -max-train-bucket-size "$MAX_TRAIN_SAMPLES_PER_CYCLE" | tee -a "$LOGSDIR"/outtrain.txt
        fi
    )
    fi

    if [[ $USEEXPORT -ne 0 ]]
    then
    (
        echo "Export"
        time ./export_model_for_selfplay.sh "$NAMEPREFIX" "$BASEDIR" "$USEGATING" "$PACK_MODEL" | tee -a "$LOGSDIR"/outexport.txt
    )
    fi

    if [[ $USEIMAGE -ne 0 ]]
    then
    (
        echo "Save image"
	    time ./view_loss.sh "$BASEDIR" "$TRAININGNAME" "$BASEDIR" | tee -a "$LOGSDIR"/outsave.txt
    )
    fi
    cd ..


    if [[ $QUIT -ne 0 ]]
    then break
    fi
done

#conda deactivate
exit 0
}
