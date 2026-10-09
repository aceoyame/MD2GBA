//go:build windows
package main

import (
    "fmt"
    "os"
    "os/exec"
    "path/filepath"
    "syscall"
)

const createNoWindow = 0x08000000

func launch(exe string, args []string, dir string) error {
    p, err := exec.LookPath(exe)
    if err != nil { return err }
    cmd := exec.Command(p, args...)
    cmd.Dir = dir
    cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: createNoWindow}
    return cmd.Start()
}

func main() {
    self, err := os.Executable()
    if err != nil { return }
    dir := filepath.Dir(self)
    gui := filepath.Join(dir, "MD2GBA_GUI.pyw")
    if _, err := os.Stat(gui); err != nil {
        _ = os.WriteFile(filepath.Join(dir,"MD2GBA_LAUNCH_ERROR.txt"), []byte("MD2GBA_GUI.pyw was not found next to MD2GBA.exe\r\n"), 0644)
        return
    }
    extra := os.Args[1:]
    args := append([]string{"-3", gui}, extra...)
    if launch("pyw.exe", args, dir) == nil { return }
    args = append([]string{gui}, extra...)
    if launch("pythonw.exe", args, dir) == nil { return }
    // Last-resort visible Python so an installation error is not completely silent.
    if launch("python.exe", args, dir) == nil { return }
    msg := fmt.Sprintf("Python 3 was not found. Install Python 3, or build the self-contained PyInstaller executable with BUILD_WINDOWS_EXE.cmd.\r\nGUI: %s\r\n", gui)
    _ = os.WriteFile(filepath.Join(dir,"MD2GBA_LAUNCH_ERROR.txt"), []byte(msg), 0644)
}
