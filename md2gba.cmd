@echo off
setlocal
set ROOT=%~dp0
if "%~1"=="" goto gui
if exist "%~1" goto gui_with_rom
where py >nul 2>nul
if not errorlevel 1 (
  py -3 "%ROOT%tools\md2gba_utility.py" %*
  exit /b %errorlevel%
)
python "%ROOT%tools\md2gba_utility.py" %*
exit /b %errorlevel%
:gui
call "%ROOT%md2gba_gui.cmd"
exit /b %errorlevel%
:gui_with_rom
call "%ROOT%md2gba_gui.cmd" "%~1"
exit /b %errorlevel%
