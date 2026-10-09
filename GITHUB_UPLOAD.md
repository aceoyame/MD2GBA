# Publishing this sanitized source snapshot

This source archive has **not been committed or published**. Create a **private** empty repository named `MD2GBA` on your GitHub account, extract this ZIP, and execute:

```bash
cd MD2GBA
git init
git branch -M main
git add .
git status --short
git commit -m "Import sanitized MD2GBA P150 source"
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/MD2GBA.git
git push -u origin main
```

No test ROMs or compiled Windows/host binaries are included. Run the toolchain setup described in README.md when building locally. This is a development prototype, not an arbitrary-ROM conversion guarantee.
