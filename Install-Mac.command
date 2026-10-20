#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
finish() { code=$?; if [ "$code" -ne 0 ]; then echo "Installation stopped. Copy the error above into our chat."; fi; read -r -p "Press Return to close..." _; }
trap finish EXIT
stamp=$(date +%Y%m%d-%H%M%S)
product="Triad v9"
for format in VST3 AU; do
    if [ "$format" = VST3 ]; then extension=vst3; folder=VST3; else extension=component; folder=Components; fi
    source_path="build-mac/Triad_artefacts/Release/$format/$product.$extension"
    destination="$HOME/Library/Audio/Plug-Ins/$folder"
    if [ ! -d "$source_path" ]; then echo "Build-Mac.command must finish successfully first."; exit 1; fi
    mkdir -p "$destination"
    # Back up this version and the earlier "Triad" / "Triad v2" ... "Triad v8" (same plug-in ID) so only one copy is installed.
    for existing in "$destination/$product.$extension" "$destination/Triad v8.$extension" "$destination/Triad v7.$extension" "$destination/Triad v6.$extension" "$destination/Triad v5.$extension" "$destination/Triad v4.$extension" "$destination/Triad v3.$extension" "$destination/Triad v2.$extension" "$destination/Triad.$extension"; do
        if [ -e "$existing" ]; then
            backup="$HOME/Library/Application Support/Triad/Backups/$stamp"
            mkdir -p "$backup"
            mv "$existing" "$backup/"
        fi
    done
    ditto "$source_path" "$destination/$product.$extension"
    codesign --force --deep --sign - "$destination/$product.$extension"
    echo "Installed $destination/$product.$extension"
done
echo "Quit and reopen Ableton or FL Studio, then rescan plugins. Add Triad v9 to a stereo audio track."
