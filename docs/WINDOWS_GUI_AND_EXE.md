# Windows GUI and EXE packaging

## Running from source

Double-click `RUN_MD2GBA_GUI.cmd` or `MD2GBA_GUI.pyw`.  The GUI is not a wrapper around a fixed game profile: it launches analysis, all-core autonomous profiling, hardware precompile planning and target generation for the ROM selected by the user.

Python 3 with Tk support and LLVM/Clang must be installed when running from source.

## Building `MD2GBA.exe`

Run `BUILD_WINDOWS_EXE.cmd` on Windows.  It invokes PyInstaller using `MD2GBA_WINDOWS.spec` and creates:

`dist\MD2GBA.exe`

The frozen executable contains the Python GUI/backend and the MD2GBA C/Python compiler source tree needed for generated builds.  It can also re-enter itself for internal compiler helper scripts, so a separate Python interpreter is not required after freezing.

P149 intentionally does **not** bundle the Windows LLVM toolchain.  `clang.exe`, `ld.lld.exe` and `llvm-objcopy.exe` must be available on PATH.  A later installer can redistribute a known-compatible LLVM subset if licensing/package size is acceptable.

## Frozen entry points

`MD2GBA.exe` with no arguments opens the GUI.

Dragging a ROM onto the executable opens the GUI with that ROM selected.

The same executable can run CLI commands, for example:

`MD2GBA.exe doctor`

`MD2GBA.exe grind game.bin -o game.gba`

`grind` is an alias for the autonomous pipeline.  With the default `--hours 0`, profiling has no wall-clock limit and ends on saturation.
