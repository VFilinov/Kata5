set MATCH_NAME=%1
if "%MATCH_NAME" == "" (
set MATCH_NAME=match
)

set ALL_MATCH_DIR=%cd%/match
if not exist %ALL_MATCH_DIR% mkdir %ALL_MATCH_DIR%

set TEE=%cd%\tee-x64

set MATCH_DIR=%ALL_MATCH_DIR%/%MATCH_NAME%
set RESULT_DIR=%ALL_MATCH_DIR%/%MATCH_NAME%result
set LOG_FILE=%ALL_MATCH_DIR%/%MATCH_NAME%.log
set RESULT_FILE=%ALL_MATCH_DIR%/%MATCH_NAME%_elo.txt
set CONFIG_FILE=%cd%/match.cfg
if exist %cd%/%MATCH_NAME%.cfg (
set CONFIG_FILE=%cd%/%MATCH_NAME%.cfg
)

engine\katago.exe match -sgf-output-dir %MATCH_DIR% -result-output-dir %RESULT_DIR% -config %CONFIG_FILE% -log-file %LOG_FILE%

cd train
python summarize_sgfs.py %MATCH_DIR% | %TEE% -a %RESULT_FILE%
cd ..
