#!/bin/bash
# Installs free, open-source effect plug-ins (64-bit VST3) on the CI Windows machine, so Triad's stress test can host
# real third-party effects there too. Each one is best effort: a missing download or installer is reported and skipped.
# Runs in Git Bash (the GitHub Windows runner's bash).
set -uo pipefail
export MSYS_NO_PATHCONV=1   # pass /VERYSILENT and the like to the installers as they are
vst3="C:/Program Files/Common Files/VST3"
work="$(cygpath -m "${RUNNER_TEMP:-$TEMP}")/free-plugins"
rm -rf "$work"; mkdir -p "$work" "$vst3"
auth=()
[ -n "${GITHUB_TOKEN:-}" ] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")
py="$(command -v python3 || command -v python)"

# repository (each one's newest release with a 64-bit Windows download)
plugins=(
  "surge-synthesizer/releases-xt"
  "jatinchowdhury18/AnalogTapeModel"
  "Chowdhury-DSP/ChowMatrix"
  "michaelwillis/dragonfly-reverb"
  "baconpaul/airwin2rack"
  "Chowdhury-DSP/BYOD"
  "jatinchowdhury18/ChowPhaser"
  "jatinchowdhury18/KlonCentaur"
  "Chowdhury-DSP/ChowMultiTool"
  "tiagolr/gate12"
  "tiagolr/time12"
  "tiagolr/filtr"
  "tiagolr/reevr"
  "wolf-plugins/wolf-shaper"
)
# Free plug-ins downloaded straight from their makers' sites (not on GitHub): name | URL.
direct=(
  "Graillon 3 Free (Auburn Sounds), a pitch corrector|https://www.auburnsounds.com/downloads/Graillon-FREE-3.2.zip"
)

asset_url()   # repository
{
  local json=""
  for try in 1 2 3 4; do
    json=$(curl -fsSL "${auth[@]}" "https://api.github.com/repos/$1/releases?per_page=6" 2>/dev/null) && [ "${json:0:1}" = "[" ] && break
    json=""
    sleep $((try * 10))
  done
  printf '%s' "$json" | "$py" -c '
import json, re, sys
try:
    releases = json.load(sys.stdin)
except Exception:
    sys.exit(0)
seen = []
def rank(name):
    n = name.lower()
    # "win" as a word of its own ("-win64-", "_windows."), not inside a name such as "airwin2rack"
    if not re.search(r"(?<![a-z])win(dows)?(?![a-z])", n) or re.search(r"darwin|macos|osx|linux", n): return None
    if re.search(r"(win32|32bit|32-bit|x86[^_-]|x86$|arm64|aarch64|lv2|clap-only|vst2|source|debug|pdb|symbols|sha256|\.sig$|\.txt$)", n): return None
    if not re.search(r"\.(zip|exe|msi)$", n): return None
    score = 0
    if "pluginsonly" in n or "plugins-only" in n: score += 4
    if n.endswith(".zip"): score += 2
    if re.search(r"(64|x64|amd64)", n): score += 1
    return score
for release in releases if isinstance(releases, list) else []:
    if release.get("draft"): continue
    best = None
    for asset in release.get("assets", []):
        seen.append(asset["name"])
        r = rank(asset["name"])
        if r is not None and (best is None or r > best[0]): best = (r, asset["browser_download_url"])
    if best:
        print(best[1]); sys.exit(0)
print("  assets offered: " + (", ".join(seen[:12]) or "none"), file=sys.stderr)
'
}

is_x64_dll()   # file: true when it is a 64-bit (x86-64) Windows DLL
{
  "$py" -c '
import struct, sys
with open(sys.argv[1], "rb") as f:
    head = f.read(4096)
if head[:2] != b"MZ": sys.exit(1)
pe = struct.unpack_from("<I", head, 0x3C)[0]
f = open(sys.argv[1], "rb"); f.seek(pe); sig = f.read(6)
sys.exit(0 if sig[:4] == b"PE\0\0" and struct.unpack_from("<H", sig, 4)[0] == 0x8664 else 1)
' "$1"
}

run_installer()   # .exe or .msi
{
  local file="$1" log="$work/install-$(basename "$1").log"
  echo "    running $(basename "$file")"
  case "$file" in
    *.msi) timeout 600 msiexec /i "$(cygpath -w "$file")" /qn /norestart ;;
    *)
      if grep -a -q -m1 "Inno Setup" "$file"; then
        timeout 600 "$file" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCANCEL "/LOG=$(cygpath -w "$log")"
      elif grep -a -q -m1 "Nullsoft" "$file"; then
        timeout 600 "$file" /S
      else
        timeout 600 "$file" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /S /quiet
      fi ;;
  esac
  local status=$?
  [ "$status" -eq 0 ] && echo "    installed" || echo "    installer exit code $status"
}

install_from_dir()   # folder
{
  local found=0
  # VST3 bundles (folders) with a 64-bit Windows binary.
  while IFS= read -r -d '' bundle; do
    if [ ! -d "$bundle/Contents/x86_64-win" ]; then echo "    skipping $(basename "$bundle") (no 64-bit Windows binary)"; continue; fi
    echo "    copying $(basename "$bundle")"
    rm -rf "$vst3/$(basename "$bundle")"
    cp -r "$bundle" "$vst3/" && found=1
  done < <(find "$1" -maxdepth 6 -type d -name "*.vst3" -prune -print0)
  # Single-file VST3s (the older layout: the DLL itself), 64-bit only.
  while IFS= read -r -d '' dll; do
    case "$dll" in *.vst3/*) continue ;; esac
    if is_x64_dll "$dll"; then echo "    copying $(basename "$dll")"; cp "$dll" "$vst3/" && found=1
    else echo "    skipping $(basename "$dll") (not 64-bit)"; fi
  done < <(find "$1" -maxdepth 6 -type f -name "*.vst3" -print0)
  if [ "$found" = 0 ]; then
    # An installer inside the download (the Windows one).
    while IFS= read -r -d '' installer; do
      case "$(basename "$installer" | tr 'A-Z' 'a-z')" in
        *mac*|*osx*|*linux*|*32*) continue ;;
      esac
      run_installer "$installer"; found=1
    done < <(find "$1" -maxdepth 4 -type f \( -iname "*.exe" -o -iname "*.msi" \) -print0)
  fi
  [ "$found" = 0 ] && echo "    nothing to install; contents: $(cd "$1" && find . -maxdepth 4 | head -n 25 | tr '\n' ' ')"
}

unpack()   # zip folder
{
  if command -v unzip >/dev/null 2>&1; then unzip -q -o "$1" -d "$2"; else 7z x -y -o"$2" "$1" >/dev/null; fi
}

install_file()   # downloaded file
{
  local file="$1"
  echo "  $(basename "$file") ($(du -h "$file" | cut -f1))"
  case "$file" in
    *.zip)
      local folder="$work/unzip-$(basename "$file")"; mkdir -p "$folder"
      unpack "$file" "$folder" && install_from_dir "$folder" ;;
    *.exe|*.msi) run_installer "$file" ;;
  esac
}

for repo in "${plugins[@]}"; do
  echo "== $repo"
  url=$(asset_url "$repo")
  if [ -z "$url" ]; then echo "  no 64-bit Windows download found, skipped"; continue; fi
  file="$work/$(basename "$url")"
  if ! curl -fsSL -o "$file" "$url"; then echo "  download failed, skipped"; continue; fi
  install_file "$file"
done

for entry in "${direct[@]}"; do
  name="${entry%%|*}"; url="${entry#*|}"
  echo "== $name"
  file="$work/$(basename "$url")"
  if ! curl -fsSL -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64)" -o "$file" "$url"; then echo "  download failed, skipped"; continue; fi
  install_file "$file"
done

echo "== Installed VST3 ($vst3):"; ls "$vst3"
exit 0
