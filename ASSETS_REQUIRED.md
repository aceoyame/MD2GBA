# Externally supplied inputs

- Supply your own legally obtained Genesis/Mega Drive test ROMs; none are committed.
- Regenerate `p119_nem_commands_clone.csv` and any game-specific capture datasets from your own test ROMs as needed.
- Rebuild the Windows launcher with Python/Go tooling; `MD2GBA.exe` is deliberately absent from the repository.
- Windows LLVM/Clang and the GBA target toolchain remain external dependencies.
- P150's complete original checkpoint also contained a Sonic 1 validation ROM, cache archive and translation plan captures. They are not required to preserve the source snapshot, and they were intentionally excluded.
