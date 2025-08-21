@echo off
::chcp 65001

:: Parameters for the training run
:: NOTE: You may want to adjust the below numbers.
:: NOTE: You probably want to edit settings in cpp/configs/training/selfplay1.cfg
:: NOTE: You probably want to edit settings in cpp/configs/training/gatekeeper1.cfg
:: Such as what board sizes and rules, you want to learn, number of visits to use, etc.

:: Also, the parameters below are relatively small, and probably
:: good for less powerful hardware and tighter turnaround during very early training, but if
:: you have strong hardware or are later into a run you may want to reduce the overhead by scaling
:: these numbers up and doing more games and training per cycle, exporting models less frequently, etc.

set CURRENTDIR=%cd%
set CONFIGFILE=%1
if "%CONFIGFILE%" == "" (
set CONFIGFILE=%CURRENTDIR%\train.cfg
)
echo config=%CONFIGFILE%
:: default parameters
set NAMEPREFIX=b10
set TRAININGNAME=b10c256nbt
set TRAININGMOD=-fson-mish-rvgl-bnh
set MODELVER=14

set KATAGO=%CURRENTDIR%\engine\katago
set SELFPLAY_CONFIG=%CURRENTDIR%\selfplay.cfg
set GATING_CONFIG=%CURRENTDIR%\gatekeeper.cfg
set OPENINGS=%CURRENTDIR%\openings.txt

set TEE=%cd%\tee-x64

:: Every cycle, play this many games
set NUM_GAMES_PER_CYCLE=500 
set NUM_THREADS_FOR_SHUFFLING=8
:: Training will proceed in chunks of this many rows, subject to MAX_TRAIN_PER_DATA.
set NUM_TRAIN_SAMPLES_PER_EPOCH=100000
:: On average, train only this many times on each data row. Larger numbers may cause overfitting.
set MAX_TRAIN_PER_DATA=8 
:: Stochastic weight averaging frequency., default 1/2 NUM_TRAIN_SAMPLES_PER_EPOCH
set NUM_TRAIN_SAMPLES_PER_SWA=80000
:: For lower-end GPUs 64 or smaller may be needed to avoid running out of GPU memory.
set BATCHSIZE=128
:: Require this many rows at the very start before beginning training.
set SHUFFLE_MINROWS=100000
:: Each cycle will do at most this many training steps.
set MAX_TRAIN_SAMPLES_PER_CYCLE=500000
:: Needs to be larger than MAX_TRAIN_SAMPLES_PER_CYCLE, so the shuffler samples enough rows each cycle for the training to use.
set SHUFFLE_KEEPROWS=600000
set SHUFFLE_ROWS_PER_FILE=70000
:: Parameter setting the scale at which the shuffler will make the training window grow sublinearly.
set TAPER_WINDOW_SCALE=50000 

set QUIT=0
set USEGATING=0
set USESELFPLAY=1
set USESHUFFLE=1
set SKIP_VALIDATE=1
set USETRAIN=1
set USEEXPORT=1
set USEIMAGE=1
set TRAIN_REPEAT_FILES=0
set PACK_MODEL=gzip
set GATE_PROP=0.5
set DATADIR=data
set LOGCONFIG=1
:: end defaults

set /a LAST_PROCESS=0
set /a STEP=0

:cicle
:: read config
::if exist %CONFIGFILE% (
::   FOR /F "eol=# tokens=1,* delims==" %%A IN (%CONFIGFILE%) DO (
::       SET %%A=%%B
::   )
::)

if exist %CONFIGFILE% (
   FOR /F "eol=# tokens=1,* delims==" %%A IN (%CONFIGFILE%) DO (
     FOR /F "tokens=1,2 delims=#" %%1 IN ("%%B") DO (
       set %%A=%%1
     )
   )
)


if %STEP% neq 0 if %QUIT% neq 0 exit

set BASEDIR=%CURRENTDIR%\%DATADIR%
set LOGSDIR=%BASEDIR%\logs
set SCRATCHDIR=%BASEDIR%\shufflescratch
set OUTPUTDIR=%BASEDIR%

if not exist %BASEDIR% mkdir %BASEDIR%
if not exist %LOGSDIR% mkdir %LOGSDIR%
if not exist %SCRATCHDIR% mkdir %SCRATCHDIR%
if not exist %BASEDIR%\selfplay mkdir %BASEDIR%\selfplay

set MODELKIND=%TRAININGNAME%%TRAININGMOD%

set EPOCHSTR=0

if exist %BASEDIR%\lstat\epoch.txt (
  set /p EPOCHSTR=<%BASEDIR%\lstat\epoch.txt
)

set /a EPOCH=%EPOCHSTR%

set MODELSDIR=%BASEDIR%\models
set /a EPOCH=%EPOCH%+1

echo.
echo. >> %LOGSDIR%\outprocess.txt

echo Beginning epoch %EPOCH% : %date% %time%
echo Beginning epoch %EPOCH% : %date% %time% >> %LOGSDIR%\outprocess.txt

if %LOGCONFIG% neq 0 (

echo MODELKIND=%MODELKIND%
echo MODELKIND=%MODELKIND% >> %LOGSDIR%\outprocess.txt

echo MODELVER=%MODELVER%
echo MODELVER=%MODELVER% >> %LOGSDIR%\outprocess.txt

echo NUM_GAMES_PER_CYCLE=%NUM_GAMES_PER_CYCLE%
echo NUM_GAMES_PER_CYCLE=%NUM_GAMES_PER_CYCLE% >> %LOGSDIR%\outprocess.txt

echo NUM_THREADS_FOR_SHUFFLING=%NUM_THREADS_FOR_SHUFFLING%
echo NUM_THREADS_FOR_SHUFFLING=%NUM_THREADS_FOR_SHUFFLING% >> %LOGSDIR%\outprocess.txt

echo NUM_TRAIN_SAMPLES_PER_EPOCH=%NUM_TRAIN_SAMPLES_PER_EPOCH%
echo NUM_TRAIN_SAMPLES_PER_EPOCH=%NUM_TRAIN_SAMPLES_PER_EPOCH% >> %LOGSDIR%\outprocess.txt

echo MAX_TRAIN_PER_DATA=%MAX_TRAIN_PER_DATA%
echo MAX_TRAIN_PER_DATA=%MAX_TRAIN_PER_DATA% >> %LOGSDIR%\outprocess.txt

echo NUM_TRAIN_SAMPLES_PER_SWA=%NUM_TRAIN_SAMPLES_PER_SWA%
echo NUM_TRAIN_SAMPLES_PER_SWA=%NUM_TRAIN_SAMPLES_PER_SWA% >> %LOGSDIR%\outprocess.txt

echo BATCHSIZE=%BATCHSIZE%
echo BATCHSIZE=%BATCHSIZE% >> %LOGSDIR%\outprocess.txt

echo SHUFFLE_MINROWS=%SHUFFLE_MINROWS%
echo SHUFFLE_MINROWS=%SHUFFLE_MINROWS% >> %LOGSDIR%\outprocess.txt

echo MAX_TRAIN_SAMPLES_PER_CYCLE=%MAX_TRAIN_SAMPLES_PER_CYCLE%
echo MAX_TRAIN_SAMPLES_PER_CYCLE=%MAX_TRAIN_SAMPLES_PER_CYCLE% >> %LOGSDIR%\outprocess.txt

echo SHUFFLE_KEEPROWS=%SHUFFLE_KEEPROWS%
echo SHUFFLE_KEEPROWS=%SHUFFLE_KEEPROWS% >> %LOGSDIR%\outprocess.txt

echo SKIP_VALIDATE=%SKIP_VALIDATE%
echo SKIP_VALIDATE=%SKIP_VALIDATE% >> %LOGSDIR%\outprocess.txt

echo SHUFFLE_ROWS_PER_FILE=%SHUFFLE_ROWS_PER_FILE%
echo SHUFFLE_ROWS_PER_FILE=%SHUFFLE_ROWS_PER_FILE% >> %LOGSDIR%\outprocess.txt

echo TAPER_WINDOW_SCALE=%TAPER_WINDOW_SCALE%
echo TAPER_WINDOW_SCALE=%TAPER_WINDOW_SCALE% >> %LOGSDIR%\outprocess.txt

echo USESELFPLAY=%USESELFPLAY%
echo USESELFPLAY=%USESELFPLAY% >> %LOGSDIR%\outprocess.txt

echo USESHUFFLE=%USESHUFFLE%
echo USESHUFFLE=%USESHUFFLE% >> %LOGSDIR%\outprocess.txt

echo USETRAIN=%USETRAIN%
echo USETRAIN=%USETRAIN% >> %LOGSDIR%\outprocess.txt

echo USEEXPORT=%USEEXPORT%
echo USEEXPORT=%USEEXPORT% >> %LOGSDIR%\outprocess.txt

echo PACK_MODEL=%PACK_MODEL%
echo PACK_MODEL=%PACK_MODEL% >> %LOGSDIR%\outprocess.txt

echo USEIMAGE=%USEIMAGE%
echo USEIMAGE=%USEIMAGE% >> %LOGSDIR%\outprocess.txt

echo USEGATING=%USEGATING%
echo USEGATING=%USEGATING% >> %LOGSDIR%\outprocess.txt

echo TRAIN_REPEAT_FILES=%TRAIN_REPEAT_FILES%
echo TRAIN_REPEAT_FILES=%TRAIN_REPEAT_FILES% >> %LOGSDIR%\outprocess.txt

echo MODELSDIR=%MODELSDIR%
echo MODELSDIR=%MODELSDIR% >> %LOGSDIR%\outprocess.txt

echo QUIT=%QUIT%
echo QUIT=%QUIT% >> %LOGSDIR%\outprocess.txt

echo GATE_PROP=%GATE_PROP%
echo GATE_PROP=%GATE_PROP% >> %LOGSDIR%\outprocess.txt

)


:gating

set COPY_MODEL=%BASEDIR%\%NAMEPREFIX%.gz
if "%PACK_MODEL%" == "none" (
set COPY_MODEL=%BASEDIR%\%NAMEPREFIX%.bin
)

set COMMAND=%KATAGO% gatekeeper -rejected-models-dir %BASEDIR%\rejectedmodels -accepted-models-dir %BASEDIR%\models\ -sgf-output-dir %BASEDIR%\gatekeeper\ -test-models-dir %BASEDIR%\modelstobetested\ -config %GATING_CONFIG% -required-candidate-win-prop %GATE_PROP% -copy-accepted-model %COPY_MODEL% -quit-if-no-nets-to-test -openings %OPENINGS%
if %USEGATING% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig gating: %date% %time% >> %LOGSDIR%\outprocess.txt
if not exist %BASEDIR%\gatekeeper mkdir %BASEDIR%\gatekeeper
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title=%COMMAND% 
%COMMAND% | %TEE% -a %LOGSDIR%\outgatekeeper.txt
echo Finished gating: %date% %time% >> %LOGSDIR%\outprocess.txt
)

:selfplay
SET COMMAND=%KATAGO% selfplay -max-games-total %NUM_GAMES_PER_CYCLE% -output-dir %BASEDIR%\selfplay -models-dir %MODELSDIR% -config %SELFPLAY_CONFIG% -openings %OPENINGS%
if %USESELFPLAY% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig selfplay: %date% %time% >> %LOGSDIR%\outprocess.txt
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title = %COMMAND% 
%COMMAND% | %TEE% -a %LOGSDIR%\outselfplay.txt
)

cd train

:shuffle
SET COMMAND_PARAMS=-approx-rows-per-out-file %SHUFFLE_ROWS_PER_FILE%
if %SHUFFLE_MINROWS% neq 0 (
set COMMAND_PARAMS=%COMMAND_PARAMS% -min-rows %SHUFFLE_MINROWS%
)
if %TAPER_WINDOW_SCALE% neq 0 (
set COMMAND_PARAMS=%COMMAND_PARAMS% -taper-window-scale %TAPER_WINDOW_SCALE%
)
if %SHUFFLE_KEEPROWS% neq 0 (
set COMMAND_PARAMS=%COMMAND_PARAMS% -keep-target-rows %SHUFFLE_KEEPROWS%
)

SET COMMAND=shuffle.sh %BASEDIR% %SCRATCHDIR% %NUM_THREADS_FOR_SHUFFLING% %BATCHSIZE% %SKIP_VALIDATE% %COMMAND_PARAMS%
if %USESHUFFLE% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig shuffle: %date% %time% >> %LOGSDIR%\outprocess.txt
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title=%COMMAND% 
sh %COMMAND% | %TEE% -a %LOGSDIR%\outshuffle.txt
)

:train
set REPEATKEY=-no-repeat-files
if %TRAIN_REPEAT_FILES% neq 0 (
set REPEATKEY=
)

SET COMMAND=train.sh %BASEDIR% %TRAININGNAME% %MODELKIND% %BATCHSIZE% main -samples-per-epoch %NUM_TRAIN_SAMPLES_PER_EPOCH% -swa-period-samples %NUM_TRAIN_SAMPLES_PER_SWA% -quit-if-no-data -stop-when-train-bucket-limited %REPEATKEY% -max-train-bucket-per-new-data %MAX_TRAIN_PER_DATA% -max-train-bucket-size %MAX_TRAIN_SAMPLES_PER_CYCLE% -model-version %MODELVER%
if %USETRAIN% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig train: %date% %time% >> %LOGSDIR%\outprocess.txt
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title=%COMMAND% 
sh %COMMAND% | %TEE% -a %LOGSDIR%\outtrain.txt
)

:export
SET COMMAND=export_model_for_selfplay.sh %NAMEPREFIX% %BASEDIR% %USEGATING% %PACK_MODEL%
if %USEEXPORT% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig export: %date% %time% >> %LOGSDIR%\outprocess.txt
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title=%COMMAND% 
sh %COMMAND% | %TEE% -a %LOGSDIR%\outexport.txt
)

:saveimmage
SET COMMAND=view_loss.sh %BASEDIR% %TRAININGNAME% %OUTPUTDIR%
if %USEIMAGE% neq 0 (
echo.
echo. >> %LOGSDIR%\outprocess.txt
echo Beginnig save image: %date% %time% >> %LOGSDIR%\outprocess.txt
echo %COMMAND%
echo %COMMAND% >> %LOGSDIR%\outprocess.txt
::title=%COMMAND% 
sh %COMMAND% | %TEE% -a %LOGSDIR%\outsaveimage.txt
)

cd ..
set /a STEP=%STEP%+1

echo Finished epoch %EPOCH% : %date% %time%
echo Finished epoch %EPOCH% : %date% %time% >> %LOGSDIR%\outprocess.txt

echo.
echo. >> %LOGSDIR%\outprocess.txt

if %QUIT% neq 0 exit

goto cicle
