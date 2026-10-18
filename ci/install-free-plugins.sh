#!/bin/bash
# Installs free, open-source effect plug-ins (VST3 + AU, Apple Silicon) on the CI Mac, so Triad's
# stress test can host real third-party effects. Each one is best effort: a missing download or
# installer is reported and skipped.
set -uo pipefail
work="${RUNNER_TEMP:-/tmp}/free-plugins"
rm -rf "$work"; mkdir -p "$work"
auth=()
[ -n "${GITHUB_TOKEN:-}" ] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")

# repository | regular expression for the macOS download (case-insensitive)
plugins=(
  "surge-synthesizer/releases-xt|mac.*\.dmg$"
  "jatinchowdhury18/AnalogTapeModel|mac.*\.(dmg|pkg)$"
  "Chowdhury-DSP/ChowMatrix|mac.*\.(dmg|pkg)$"
  "michaelwillis/dragonfly-reverb|(mac|osx).*\.(zip|dmg|pkg)$"
  "baconpaul/airwin2rack|(mac|darwin).*\.(dmg|pkg|zip)$"
  "Chowdhury-DSP/BYOD|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "jatinchowdhury18/ChowPhaser|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "jatinchowdhury18/KlonCentaur|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "Chowdhury-DSP/ChowMultiTool|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "tiagolr/gate12|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "tiagolr/time12|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "tiagolr/filtr|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "tiagolr/reevr|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
  "wolf-plugins/wolf-shaper|(mac|osx|darwin|apple).*\.(dmg|pkg|zip)$"
)
# Free plug-ins downloaded straight from their makers' sites (not on GitHub): name | URL.
direct=(
  "Graillon 3 Free (Auburn Sounds), a pitch corrector|https://www.auburnsounds.com/downloads/Graillon-FREE-3.2.zip"
)

asset_url()   # repository regex
{
  curl -fsSL "${auth[@]}" "https://api.github.com/repos/$1/releases?per_page=6" 2>/dev/null | python3 -c '
import json, re, sys
pattern = re.compile(sys.argv[1], re.I)
try:
    releases = json.load(sys.stdin)
except Exception:
    sys.exit(0)
seen = []
for release in releases if isinstance(releases, list) else []:
    if release.get("draft"): continue
    for asset in release.get("assets", []):
        name = asset["name"]
        seen.append(name)
        if pattern.search(name) and not re.search(r"(intel|x86_64|x64|arm64-only-debug|source|clap-only|lv2)", name, re.I):
            print(asset["browser_download_url"]); sys.exit(0)
print("  assets offered: " + (", ".join(seen[:12]) or "none"), file=sys.stderr)
' "$2"
}

install_from_dir()   # folder
{
  local found=0
  while IFS= read -r -d '' pkg; do
    echo "    installing $(basename "$pkg")"
    sudo installer -pkg "$pkg" -target / >/dev/null && found=1
  done < <(find "$1" -maxdepth 4 \( -name "*.pkg" -o -name "*.mpkg" \) -print0)
  if [ "$found" = 0 ]; then
    # A disk image inside the download: open it and install from it.
    while IFS= read -r -d '' image; do
      local inner="$work/mount-inner-$(basename "$image")"; mkdir -p "$inner"
      if (set +o pipefail; yes | hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$inner" "$image" >/dev/null 2>&1); then
        install_from_dir "$inner"; found=1
        hdiutil detach "$inner" -quiet || hdiutil detach "$inner" -force -quiet || true
      fi
    done < <(find "$1" -maxdepth 4 -name "*.dmg" -print0)
  fi
  if [ "$found" = 0 ]; then
    echo "    contents: $(cd "$1" && find . -maxdepth 4 | head -n 25 | tr '\n' ' ')"
    while IFS= read -r -d '' bundle; do
      # Only macOS bundles (a download for several systems also holds Windows and Linux ones).
      if [ ! -d "$bundle/Contents/MacOS" ]; then echo "    skipping $(basename "$bundle") (not a macOS bundle)"; continue; fi
      echo "    copying $(basename "$bundle")"
      sudo cp -R "$bundle" /Library/Audio/Plug-Ins/VST3/
    done < <(find "$1" -maxdepth 6 -name "*.vst3" -prune -print0)
    while IFS= read -r -d '' bundle; do
      echo "    copying $(basename "$bundle")"
      sudo cp -R "$bundle" /Library/Audio/Plug-Ins/Components/
    done < <(find "$1" -maxdepth 6 -name "*.component" -prune -print0)
  fi
}

install_file()   # downloaded file
{
  local file="$1"
  echo "  $(basename "$file") ($(du -h "$file" | cut -f1))"
  case "$file" in
    *.pkg) sudo installer -pkg "$file" -target / >/dev/null && echo "    installed" ;;
    *.dmg)
      mount="$work/mount-$(basename "$file")"; mkdir -p "$mount"
      # "yes" accepts a licence prompt if the image has one (its broken pipe is not an error).
      if (set +o pipefail; yes | hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$mount" "$file" >/dev/null 2>&1); then
        install_from_dir "$mount"
        hdiutil detach "$mount" -quiet || hdiutil detach "$mount" -force -quiet || true
      else echo "  could not open the disk image, skipped"; fi ;;
    *.zip)
      folder="$work/unzip-$(basename "$file")"; mkdir -p "$folder"
      unzip -q "$file" -d "$folder" && install_from_dir "$folder" ;;
  esac
}

for entry in "${plugins[@]}"; do
  repo="${entry%%|*}"; pattern="${entry#*|}"
  echo "== $repo"
  url=$(asset_url "$repo" "$pattern")
  if [ -z "$url" ]; then echo "  no macOS download found, skipped"; continue; fi
  file="$work/$(basename "$url")"
  if ! curl -fsSL -o "$file" "$url"; then echo "  download failed, skipped"; continue; fi
  install_file "$file"
done

for entry in "${direct[@]}"; do
  name="${entry%%|*}"; url="${entry#*|}"
  echo "== $name"
  file="$work/$(basename "$url")"
  if ! curl -fsSL -A "Mozilla/5.0 (Macintosh)" -o "$file" "$url"; then echo "  download failed, skipped"; continue; fi
  install_file "$file"
done

sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 3
echo "== Installed VST3:"; ls /Library/Audio/Plug-Ins/VST3
echo "== Installed AU:"; ls /Library/Audio/Plug-Ins/Components
exit 0
