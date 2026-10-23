#!/bin/bash
# Lists the DLLs a Windows program needs and whether each one is on this machine, then starts it once with no
# arguments through PowerShell to show Windows' exit code (0xC0000135: a DLL is missing).
exe="$1"
ls -la "$(dirname "$exe")"
dumpbin="$(ls /c/Program\ Files/Microsoft\ Visual\ Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe 2>/dev/null | head -n 1)"
if [ -n "$dumpbin" ]; then
  "$dumpbin" -nologo -dependents "$exe" | tr -d '\r' | sed -n 's/^ *\([A-Za-z0-9_.-]*\.[dD][lL][lL]\)$/\1/p' | while read -r dll; do
    if [ -e "/c/Windows/System32/$dll" ] || [ -e "$(dirname "$exe")/$dll" ]; then echo "  found   $dll"
    else echo "  MISSING $dll"; fi
  done
fi
powershell -NoProfile -Command "\$p = Start-Process -FilePath '$(cygpath -w "$exe")' -NoNewWindow -Wait -PassThru; 'Started with no arguments: exit code 0x{0:X8}' -f \$p.ExitCode"
