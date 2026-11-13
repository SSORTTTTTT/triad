#!/bin/bash
# Lists the DLLs each program needs and says which ones Windows can't find (a missing one makes it exit with 127).
dumpbin="$("/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -latest -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/dumpbin.exe' | head -1 | tr -d '\r')"
for exe in "$@"; do
  dir=$(dirname "$exe")
  for dll in $("$dumpbin" -nologo -dependents "$(cygpath -w "$exe")" | tr -d '\r' | grep -i '\.dll' | sed 's/^ *//'); do
    found=""
    for d in "$dir" /c/Windows/System32 /c/Windows/SysWOW64; do [ -e "$d/$dll" ] && found=1 && break; done
    [ -n "$found" ] || echo "MISSING DLL for $(basename "$exe"): $dll"
  done
  echo "$(basename "$exe") needs: $("$dumpbin" -nologo -dependents "$(cygpath -w "$exe")" | tr -d '\r' | grep -i '\.dll' | sed 's/^ *//' | tr '\n' ' ')"
done
