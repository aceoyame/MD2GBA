@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo === MD2GBA Windows EXE builder ===
where py.exe >nul 2>nul || (echo Python 3 launcher not found. Install Python 3 first.& pause & exit /b 1)
py -3 -m PyInstaller --version >nul 2>nul
if errorlevel 1 (
  echo PyInstaller is not installed. Installing it for this Python...
  py -3 -m pip install --upgrade pyinstaller || (echo PyInstaller installation failed.& pause & exit /b 1)
)
py -3 -m PyInstaller --clean --noconfirm MD2GBA_WINDOWS.spec || (echo Build failed.& pause & exit /b 1)
echo.
echo Built: %CD%\dist\MD2GBA.exe
echo The EXE bundles the Python GUI/compiler code. LLVM/Clang is still required for host/GBA native compilation in P150.
pause
