@echo off
setlocal
cd /d "%~dp0"
where pyw.exe >nul 2>nul && (start "" pyw.exe -3 "%~dp0MD2GBA_GUI.pyw" %* & exit /b 0)
where pythonw.exe >nul 2>nul && (start "" pythonw.exe "%~dp0MD2GBA_GUI.pyw" %* & exit /b 0)
echo Python 3 was not found. If you built MD2GBA.exe, run that instead.
echo Otherwise install Python 3 with Tcl/Tk enabled.
pause
