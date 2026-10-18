@echo off
@REM .\scripts\sync_web.bat release-v6.0.3 D:\Jenkins\workspace\Community 

if [%1]==[] (
    set web_branch=master
)  else (
    set web_branch=%1
)
if [%2]==[] (
    set web_root=%cd%\..\C3DSlicerCustom
) else (
    set web_root=%2
)

set cp_source=%cd%
echo web_root=%web_root%
echo cp_source=%cp_source%
if not exist %web_root% (
    echo %web_root% not exist
    exit 1
)

@REM 定义源目录和目标目录
set "sync_source_dir=%web_root%\customized"
set "sync_target_dir=%cp_source%\customized"

:WEB_REPO
@REM web repo start
echo WEB_REPO start
echo curwebdir=%web_root%
cd /d %web_root%
git fetch
git checkout %web_branch%
git pull origin %web_branch%
REM 获取 git rev-list 的输出

rem copy start
:SYNC_WEB
if exist %sync_target_dir% (
    echo %sync_target_dir% is exist,delete it
    rmdir /s /q %sync_target_dir%
)
if not exist %sync_source_dir% (
    echo %sync_source_dir% not exist
    exit /b 1
)
echo copy start
xcopy "%sync_source_dir%\*" "%sync_target_dir%\" /s /e /y
echo  copy end

:END
cd  %cp_source%
echo web sync end



