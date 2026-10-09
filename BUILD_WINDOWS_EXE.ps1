$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if (-not (Get-Command py.exe -ErrorAction SilentlyContinue)) { throw 'Python 3 launcher (py.exe) not found.' }
try { py -3 -m PyInstaller --version | Out-Null } catch { py -3 -m pip install --upgrade pyinstaller }
py -3 -m PyInstaller --clean --noconfirm MD2GBA_WINDOWS.spec
Write-Host "Built: $PSScriptRoot\dist\MD2GBA.exe"
Write-Host 'P150 still expects LLVM/Clang on PATH for native host/GBA compilation.'
