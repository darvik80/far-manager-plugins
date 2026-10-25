$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class MiniDump2 {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint Access, bool Inherit, int Pid);
    [DllImport("dbghelp.dll", SetLastError=true)]
    public static extern bool MiniDumpWriteDump(IntPtr Process, int Pid, IntPtr File, uint Type, IntPtr Exc, IntPtr User, IntPtr Callback);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr H);
}
"@

$fars = Get-Process Far -ErrorAction SilentlyContinue
if (-not $fars) { Set-Content 'D:\Projects\cpp\far-manager\dump_log.txt' 'NoFarProcess'; exit 1 }

$log = @()
foreach ($p in $fars) {
    $out = "D:\Projects\cpp\far-manager\far_hang_$($p.Id).dmp"
    $proc = [MiniDump2]::OpenProcess(0x1F0FFF, $false, $p.Id)
    $log += "PID $($p.Id) OpenProcess: $proc"
    if ($proc -ne [IntPtr]::Zero) {
        $fs = [System.IO.File]::Create($out)
        try {
            $ok = [MiniDump2]::MiniDumpWriteDump($proc, $p.Id, $fs.SafeFileHandle.DangerousGetHandle(), 0x40000002, [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero)
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            $log += "MiniDumpWriteDump: $ok err=$err"
        } finally { $fs.Close() }
        [MiniDump2]::CloseHandle($proc) | Out-Null
    }
}
Set-Content 'D:\Projects\cpp\far-manager\dump_log.txt' ($log -join "`r`n")
