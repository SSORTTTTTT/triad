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
  "jatinchowdhury18/ChowMatrix|mac.*\.(dmg|pkg)$"
  "michaelwillis/dragonfly-reverb|(mac|osx).*\.(zip|dmg|pkg)$"
  "baconpaul/airwin2rack|(mac|darwin).*\.(dmg|pkg|zip)$"
)

asset_url()   # repository regex
{
  curl -fsSL "${auth[@]}" "https://api.github.com/repos/$1/releases?per_page=6" 2>/dev/null | python3 -c '
import json, re, sys
pattern = re.compile(sys.argv[1], re.I)
for release in json.load(sys.stdin):
    if release.get("draft"): continue
    for asset in release.get("assets", []):
        name = asset["name"]
        if pattern.search(name) and not re.search(r"(intel|x86_64|x64|arm64-only-debug|source|clap-only|lv2)", name, re.I):
            print(asset["browser_download_url"]); sys.exit(0)
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
    while IFS= read -r -d '' bundle; do
      echo "    copying $(basename "$bundle")"
      sudo cp -R "$bundle" /Library/Audio/Plug-Ins/VST3/
    done < <(find "$1" -maxdepth 5 -name "*.vst3" -prune -print0)
    while IFS= read -r -d '' bundle; do
      echo "    copying $(basename "$bundle")"
      sudo cp -R "$bundle" /Library/Audio/Plug-Ins/Components/
    done < <(find "$1" -maxdepth 5 -name "*.component" -prune -print0)
  fi
}

for entry in "${plugins[@]}"; do
  repo="${entry%%|*}"; pattern="${entry#*|}"
  echo "== $repo"
  url=$(asset_url "$repo" "$pattern")
  if [ -z "$url" ]; then echo "  no macOS download found, skipped"; continue; fi
  file="$work/$(basename "$url")"
  if ! curl -fsSL -o "$file" "$url"; then echo "  download failed, skipped"; continue; fi
  echo "  $(basename "$file") ($(du -h "$file" | cut -f1))"
  case "$file" in
    *.pkg) sudo installer -pkg "$file" -target / >/dev/null && echo "    installed" ;;
    *.dmg)
      mount="$work/mount-$(basename "$file")"; mkdir -p "$mount"
      if yes | hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$mount" "$file" >/dev/null; then
        install_from_dir "$mount"
        hdiutil detach "$mount" -quiet || hdiutil detach "$mount" -force -quiet || true
      else echo "  could not open the disk image, skipped"; fi ;;
    *.zip)
      folder="$work/unzip-$(basename "$file")"; mkdir -p "$folder"
      unzip -q "$file" -d "$folder" && install_from_dir "$folder" ;;
  esac
done

sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 3
echo "== Installed VST3:"; ls /Library/Audio/Plug-Ins/VST3
echo "== Installed AU:"; ls /Library/Audio/Plug-Ins/Components
exit 0
