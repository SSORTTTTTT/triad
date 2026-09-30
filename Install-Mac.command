#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
finish() { code=$?; if [ "$code" -ne 0 ]; then echo "Installation stopped. Copy the error above into our chat."; fi; read -r -p "Press Return to close..." _; }
trap finish EXIT
stamp=$(date +%Y%m%d-%H%M%S)
for format in VST3 AU; do
    if [ "$format" = VST3 ]; then extension=vst3; folder=VST3; else extension=component; folder=Components; fi
    source_path="build-mac/Triad_artefacts/Release/$format/Triad.$extension"
    destination="$HOME/Library/Audio/Plug-Ins/$folder"
    if [ ! -d "$source_path" ]; then echo "Build-Mac.command must finish successfully first."; exit 1; fi
    mkdir -p "$destination"
    if [ -e "$destination/Triad.$extension" ]; then
        backup="$HOME/Library/Application Support/Triad/Backups/$stamp"
        mkdir -p "$backup"
        mv "$destination/Triad.$extension" "$backup/"
    fi
    ditto "$source_path" "$destination/Triad.$extension"
    codesign --force --deep --sign - "$destination/Triad.$extension"
    echo "Installed $destination/Triad.$extension"
done
echo "Restart Ableton or FL Studio and rescan plugins. Add Triad to a stereo audio track."
