@echo off
setlocal
set ROOT=%~dp0
where pyw >nul 2>nul
if not errorlevel 1 (
  start "MD2GBA" pyw -3 "%ROOT%tools\md2gba_gui.pyw" %*
  exit /b 0
)
where pythonw >nul 2>nul
if not errorlevel 1 (
  start "MD2GBA" pythonw "%ROOT%tools\md2gba_gui.pyw" %*
  exit /b 0
)
echo Python 3 with Tkinter is required.
pause
exit /b 1
