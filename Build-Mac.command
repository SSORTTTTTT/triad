#!/bin/bash
# Triad — build the AU and VST3 for Apple Silicon and wrap them in an installer package.
# Double-click in Finder, or run:  bash Build-Mac.command
#
# Output: Installer/Triad-v20-<version>-macOS-AppleSilicon.pkg, Installer/Triad-Time-<version>-macOS-AppleSilicon.pkg
# and Installer/Triad-Split-<version>-macOS-AppleSilicon.pkg
# A full log is written to build-mac.log next to this script.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

VERSION="0.21.0"
PRODUCT="Triad v20"                 # bundle and display name (plug-in IDs unchanged from Triad 0.1)
JUCE_COMMIT="29396c22c93392d6738e021b83196283d6e4d850"   # JUCE 8.0.12
CMAKE_FALLBACK_VERSION="3.31.9"
CMAKE_FALLBACK_SHA256="eb9d0b2b6a5b900e2f5811326a86ea393a146e4cb2384d15512c226e9c98f8bf"
BUILD_DIR="$ROOT/build-mac"
DEPS_DIR="$ROOT/deps"
OUT_DIR="$ROOT/Installer"
PKG_NAME="Triad-v20-$VERSION-macOS-AppleSilicon.pkg"
TIME_PRODUCT="Triad Time"            # Triad's time / volume gate as a plug-in of its own (its own installer)
TIME_PKG_PATH="$OUT_DIR/Triad-Time-$VERSION-macOS-AppleSilicon.pkg"
SPLIT_PRODUCT="Triad Split"          # split mode: one band per track or mixer insert (its own installer)
SPLIT_PKG_PATH="$OUT_DIR/Triad-Split-$VERSION-macOS-AppleSilicon.pkg"
PKG_PATH="$OUT_DIR/$PKG_NAME"
LOG="$ROOT/build-mac.log"
STATUS="$ROOT/build-mac.status"

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/CMake.app/Contents/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

echo "RUNNING $(date '+%Y-%m-%d %H:%M:%S')" > "$STATUS"
exec > >(tee "$LOG") 2>&1

finish()
{
    local code=$?
    if [ "$code" -eq 0 ]; then
        echo "SUCCESS $PKG_PATH" > "$STATUS"
    else
        echo "FAILED ($code)" > "$STATUS"
        echo
        echo "Build stopped. The full log is in build-mac.log. Copy the error above into our chat."
    fi
    if [ -t 0 ]; then read -r -p "Press Return to close..." _ || true; fi
}
trap finish EXIT

step() { echo; echo "==> $*"; }
fail() { echo "ERROR: $*"; exit 1; }

version_at_least_3_22()
{
    local major minor
    IFS=. read -r major minor _ <<< "$1"
    [[ "$major" =~ ^[0-9]+$ && "$minor" =~ ^[0-9]+$ ]] || return 1
    [ "$major" -gt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -ge 22 ]; }
}

# ---------------------------------------------------------------------------
step "Checking dependencies"
echo "macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" != 1 ]; then
    echo "Note: this Mac is not Apple Silicon. The plugins are still built for Apple Silicon (arm64) only."
fi

if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang >/dev/null 2>&1; then
    echo "Apple's Command Line Tools are required. A macOS window will offer to install them:"
    echo "click Install, wait for it to finish, then open Build-Mac.command again."
    xcode-select --install >/dev/null 2>&1 || true
    exit 1
fi
echo "Developer tools: $(xcode-select -p)"
echo "Compiler:        $(xcrun clang --version | head -n 1)"
echo "macOS SDK:       $(xcrun --show-sdk-version)"

for tool in git pkgbuild productbuild pkgutil codesign lipo ditto plutil shasum curl; do
    command -v "$tool" >/dev/null 2>&1 || fail "Required tool '$tool' was not found."
done
echo "Git:             $(git --version)"

CMAKE=""
if command -v cmake >/dev/null 2>&1; then
    found_version=$(cmake --version | head -n 1 | awk '{print $3}')
    if version_at_least_3_22 "$found_version"; then
        CMAKE="$(command -v cmake)"
    else
        echo "Found CMake $found_version, but 3.22 or newer is needed."
    fi
fi
if [ -z "$CMAKE" ]; then
    local_cmake="$DEPS_DIR/cmake-$CMAKE_FALLBACK_VERSION-macos-universal/CMake.app/Contents/bin/cmake"
    if [ ! -x "$local_cmake" ]; then
        echo "Downloading CMake $CMAKE_FALLBACK_VERSION from github.com/Kitware/CMake (used only for this build)..."
        mkdir -p "$DEPS_DIR"
        tarball="$DEPS_DIR/cmake-$CMAKE_FALLBACK_VERSION-macos-universal.tar.gz"
        curl -fL --retry 3 -o "$tarball.part" \
            "https://github.com/Kitware/CMake/releases/download/v$CMAKE_FALLBACK_VERSION/cmake-$CMAKE_FALLBACK_VERSION-macos-universal.tar.gz"
        echo "$CMAKE_FALLBACK_SHA256  $tarball.part" | shasum -a 256 -c - || fail "CMake download checksum mismatch."
        mv "$tarball.part" "$tarball"
        tar -xzf "$tarball" -C "$DEPS_DIR"
        rm -f "$tarball"
    fi
    CMAKE="$local_cmake"
fi
echo "CMake:           $("$CMAKE" --version | head -n 1) ($CMAKE)"

# JUCE: a shallow checkout of the pinned commit (the commit hash verifies the content).
JUCE_DIR="$DEPS_DIR/JUCE"
JUCE_ARGS=()
if [ "$(git -C "$JUCE_DIR" rev-parse HEAD 2>/dev/null || true)" = "$JUCE_COMMIT" ]; then
    JUCE_ARGS=(-DJUCE_SOURCE_DIR="$JUCE_DIR")
else
    echo "Downloading JUCE 8.0.12 (commit ${JUCE_COMMIT:0:12})..."
    rm -rf "$JUCE_DIR"
    mkdir -p "$JUCE_DIR"
    if git -C "$JUCE_DIR" init -q \
        && git -C "$JUCE_DIR" remote add origin https://github.com/juce-framework/JUCE.git \
        && git -C "$JUCE_DIR" fetch -q --depth 1 origin "$JUCE_COMMIT" \
        && git -C "$JUCE_DIR" -c advice.detachedHead=false checkout -q FETCH_HEAD; then
        JUCE_ARGS=(-DJUCE_SOURCE_DIR="$JUCE_DIR")
    else
        echo "Shallow JUCE download failed; letting CMake fetch it instead."
        rm -rf "$JUCE_DIR"
    fi
fi
[ ${#JUCE_ARGS[@]} -gt 0 ] && echo "JUCE:            $JUCE_DIR"

# ---------------------------------------------------------------------------
step "Configuring (Release, arm64, macOS 12.0+)"
"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DTRIAD_BUILD_TESTS=OFF \
    "${JUCE_ARGS[@]+"${JUCE_ARGS[@]}"}"

step "Building Triad, Triad Time and Triad Split (AU and VST3)"
"$CMAKE" --build "$BUILD_DIR" --config Release --target Triad_VST3 Triad_AU TriadTime_VST3 TriadTime_AU TriadSplit_VST3 TriadSplit_AU \
    --parallel "$(sysctl -n hw.ncpu)"

# ---------------------------------------------------------------------------
step "Preparing the plugin bundles"
VST3_BUNDLE="$BUILD_DIR/Triad_artefacts/Release/VST3/$PRODUCT.vst3"
AU_BUNDLE="$BUILD_DIR/Triad_artefacts/Release/AU/$PRODUCT.component"
PKG_WORK="$BUILD_DIR/pkg"
rm -rf "$PKG_WORK"
VST3_ROOT="$PKG_WORK/vst3-root"
AU_ROOT="$PKG_WORK/au-root"
mkdir -p "$VST3_ROOT/Library/Audio/Plug-Ins/VST3" "$AU_ROOT/Library/Audio/Plug-Ins/Components" \
         "$PKG_WORK/components" "$PKG_WORK/scripts-au" "$PKG_WORK/resources" "$OUT_DIR"

stage_bundle()   # source destination
{
    [ -d "$1" ] || fail "Expected build output is missing: $1"
    ditto --norsrc --noextattr --noqtn --noacl "$1" "$2"
    xattr -cr "$2" 2>/dev/null || true
    local binary="$2/Contents/MacOS/$PRODUCT"
    [ -f "$binary" ] || fail "Plugin binary missing: $binary"
    local archs; archs=$(lipo -archs "$binary")
    [[ " $archs " == *" arm64 "* ]] || fail "$binary is not an Apple Silicon binary (found: $archs)"
    # Remove local debug symbols from the shipped copy only (the build folder keeps the original).
    local before; before=$(stat -f %z "$binary")
    xcrun strip -x "$binary"
    echo "  $(basename "$2") binary: $((before / 1024)) KB -> $(( $(stat -f %z "$binary") / 1024 )) KB after strip"
    # The plug-in scanner helper (checks plug-ins in its own process, so a bad plug-in cannot crash the DAW).
    local helper="$2/Contents/Helpers/Triad Scanner"
    [ -x "$helper" ] || fail "Plug-in scanner helper missing: $helper"
    [[ " $(lipo -archs "$helper") " == *" arm64 "* ]] || fail "$helper is not an Apple Silicon binary"
    xcrun strip -x "$helper"
    echo "  Triad Scanner helper: $(( $(stat -f %z "$helper") / 1024 )) KB"
    # Local ad-hoc signature (no Developer ID). Apple Silicon requires every binary to be signed.
    codesign --force --deep --sign - "$2"
    codesign --verify --deep --strict "$2"
    echo "  $(basename "$2"): $archs, $(du -sh "$2" | awk '{print $1}'), ad-hoc signed, id $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$2/Contents/Info.plist")"
}
stage_bundle "$VST3_BUNDLE" "$VST3_ROOT/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
stage_bundle "$AU_BUNDLE"   "$AU_ROOT/Library/Audio/Plug-Ins/Components/$PRODUCT.component"

# ---------------------------------------------------------------------------
step "Building the installer package"

# Never let Installer "relocate" the plugins to a copy it finds elsewhere (e.g. this build folder).
component_plist()   # root plist
{
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$2" >/dev/null 2>&1; do
        # Newer pkgbuild versions omit the key entirely, so (re)add it rather than edit it.
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$2" >/dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$2"
        i=$((i + 1))
    done
    [ "$i" -gt 0 ] || fail "pkgbuild found no bundle to describe in $1"
}
component_plist "$VST3_ROOT" "$PKG_WORK/vst3-components.plist"
component_plist "$AU_ROOT"   "$PKG_WORK/au-components.plist"

# Triad v20 replaces the earlier "Triad", "Triad v2" ... "Triad v19" bundles (same plug-in IDs): remove them first
# so a host never finds two copies of the same plug-in. Only bundles with Triad's identifier are touched.
mkdir -p "$PKG_WORK/scripts-vst3"
write_preinstall()   # script-dir old-bundle-paths-relative-to-volume...
{
    local dir="$1"; shift
    {
        echo '#!/bin/sh'
        for old in "$@"; do
            cat <<EOF
old="\${3%/}/$old"
id=\$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "\$old/Contents/Info.plist" 2>/dev/null)
if [ -d "\$old" ] && [ "\$id" = "audio.thomasross.triad" ]; then
    /bin/rm -rf "\$old"
fi
EOF
        done
        echo 'exit 0'
    } > "$dir/preinstall"
    chmod 755 "$dir/preinstall"
}
write_preinstall "$PKG_WORK/scripts-vst3" "Library/Audio/Plug-Ins/VST3/Triad.vst3" "Library/Audio/Plug-Ins/VST3/Triad v2.vst3" "Library/Audio/Plug-Ins/VST3/Triad v3.vst3" "Library/Audio/Plug-Ins/VST3/Triad v4.vst3" "Library/Audio/Plug-Ins/VST3/Triad v5.vst3" "Library/Audio/Plug-Ins/VST3/Triad v6.vst3" "Library/Audio/Plug-Ins/VST3/Triad v7.vst3" "Library/Audio/Plug-Ins/VST3/Triad v8.vst3" "Library/Audio/Plug-Ins/VST3/Triad v9.vst3" "Library/Audio/Plug-Ins/VST3/Triad v10.vst3" "Library/Audio/Plug-Ins/VST3/Triad v11.vst3" "Library/Audio/Plug-Ins/VST3/Triad v12.vst3" "Library/Audio/Plug-Ins/VST3/Triad v13.vst3" "Library/Audio/Plug-Ins/VST3/Triad v14.vst3" "Library/Audio/Plug-Ins/VST3/Triad v15.vst3" "Library/Audio/Plug-Ins/VST3/Triad v16.vst3" "Library/Audio/Plug-Ins/VST3/Triad v17.vst3" "Library/Audio/Plug-Ins/VST3/Triad v18.vst3" "Library/Audio/Plug-Ins/VST3/Triad v19.vst3"
write_preinstall "$PKG_WORK/scripts-au" "Library/Audio/Plug-Ins/Components/Triad.component" "Library/Audio/Plug-Ins/Components/Triad v2.component" "Library/Audio/Plug-Ins/Components/Triad v3.component" "Library/Audio/Plug-Ins/Components/Triad v4.component" "Library/Audio/Plug-Ins/Components/Triad v5.component" "Library/Audio/Plug-Ins/Components/Triad v6.component" "Library/Audio/Plug-Ins/Components/Triad v7.component" "Library/Audio/Plug-Ins/Components/Triad v8.component" "Library/Audio/Plug-Ins/Components/Triad v9.component" "Library/Audio/Plug-Ins/Components/Triad v10.component" "Library/Audio/Plug-Ins/Components/Triad v11.component" "Library/Audio/Plug-Ins/Components/Triad v12.component" "Library/Audio/Plug-Ins/Components/Triad v13.component" "Library/Audio/Plug-Ins/Components/Triad v14.component" "Library/Audio/Plug-Ins/Components/Triad v15.component" "Library/Audio/Plug-Ins/Components/Triad v16.component" "Library/Audio/Plug-Ins/Components/Triad v17.component" "Library/Audio/Plug-Ins/Components/Triad v18.component" "Library/Audio/Plug-Ins/Components/Triad v19.component"
cat > "$PKG_WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
# Ask macOS to refresh its Audio Unit registry so hosts see Triad on their next scan.
/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod 755 "$PKG_WORK/scripts-au/postinstall"

pkgbuild --quiet --root "$VST3_ROOT" --component-plist "$PKG_WORK/vst3-components.plist" \
    --scripts "$PKG_WORK/scripts-vst3" \
    --identifier audio.thomasross.triad.vst3 --version "$VERSION" --install-location / \
    "$PKG_WORK/components/Triad-VST3.pkg"
pkgbuild --quiet --root "$AU_ROOT" --component-plist "$PKG_WORK/au-components.plist" \
    --scripts "$PKG_WORK/scripts-au" \
    --identifier audio.thomasross.triad.au --version "$VERSION" --install-location / \
    "$PKG_WORK/components/Triad-AU.pkg"

cat > "$PKG_WORK/resources/welcome.html" <<EOF
<html><body style="font-family: -apple-system, Helvetica; font-size: 13px;">
<h2>$PRODUCT ($VERSION)</h2>
<p>Multiband effects host: split your signal into up to 10 bands with draggable crossovers over a
live spectrum, run a chain of up to 10 VST3 or AU effects on every band (each with its own mix knob),
and blend each band with its Band Mix.</p>
<p>This installer adds, for all users:</p>
<ul>
<li><b>Audio Unit</b> &rarr; /Library/Audio/Plug-Ins/Components/$PRODUCT.component</li>
<li><b>VST3</b> &rarr; /Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3</li>
</ul>
<p>It replaces an earlier &ldquo;Triad&rdquo; if one is installed; projects made with it open in $PRODUCT.</p>
<p>Built natively for Apple Silicon (macOS 12 or later). Quit your DAWs before installing.
Use <i>Customize</i> to install only one format.</p>
</body></html>
EOF
cat > "$PKG_WORK/resources/conclusion.html" <<EOF
<html><body style="font-family: -apple-system, Helvetica; font-size: 13px;">
<h2>$PRODUCT is installed</h2>
<p>Open your DAW, rescan plug-ins if needed, and add <b>$PRODUCT</b> to a stereo audio track or mixer insert.</p>
<p>Effects hosted inside Triad must be native Apple Silicon VST3 or Audio Unit effects.</p>
</body></html>
EOF
cat > "$PKG_WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PRODUCT ($VERSION)</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check>
        <allowed-os-versions><os-version min="12.0"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>
    <choice id="au" title="$PRODUCT Audio Unit (AU)" description="Installs $PRODUCT.component in /Library/Audio/Plug-Ins/Components.">
        <pkg-ref id="audio.thomasross.triad.au"/>
    </choice>
    <choice id="vst3" title="$PRODUCT VST3" description="Installs $PRODUCT.vst3 in /Library/Audio/Plug-Ins/VST3.">
        <pkg-ref id="audio.thomasross.triad.vst3"/>
    </choice>
    <pkg-ref id="audio.thomasross.triad.au" version="$VERSION" onConclusion="none">Triad-AU.pkg</pkg-ref>
    <pkg-ref id="audio.thomasross.triad.vst3" version="$VERSION" onConclusion="none">Triad-VST3.pkg</pkg-ref>
</installer-gui-script>
EOF

rm -f "$PKG_PATH"
productbuild --quiet --distribution "$PKG_WORK/distribution.xml" \
    --package-path "$PKG_WORK/components" --resources "$PKG_WORK/resources" "$PKG_PATH"

# ---------------------------------------------------------------------------
step "Verifying the installer contents"
VERIFY="$PKG_WORK/verify"
rm -rf "$VERIFY"
pkgutil --expand-full "$PKG_PATH" "$VERIFY"
for entry in "Triad-VST3.pkg:Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3" \
             "Triad-AU.pkg:Library/Audio/Plug-Ins/Components/$PRODUCT.component"; do
    component="${entry%%:*}"; relative="${entry#*:}"
    bundle="$VERIFY/$component/Payload/$relative"
    [ -f "$bundle/Contents/MacOS/$PRODUCT" ] || fail "$component does not contain $relative"
    [ -x "$bundle/Contents/Helpers/Triad Scanner" ] || fail "$component is missing the Triad Scanner helper"
    [ -x "$VERIFY/$component/Scripts/preinstall" ] || fail "$component is missing the step that removes the old Triad"
    codesign --verify --deep --strict "$bundle" || fail "Signature check failed inside $component"
    if grep -q "<relocate>" "$VERIFY/$component/PackageInfo" 2>/dev/null; then
        fail "$component is relocatable; Installer could put the plugin somewhere other than /Library"
    fi
    echo "  $component -> /$relative ($(lipo -archs "$bundle/Contents/MacOS/$PRODUCT"), signature OK)"
done
{ installer -pkginfo -pkg "$PKG_PATH" 2>/dev/null || true; } | sed 's/^/  package: /'
pkg_bytes=$(stat -f %z "$PKG_PATH")
echo "  size: $((pkg_bytes / 1024)) KB   sha256: $(shasum -a 256 "$PKG_PATH" | awk '{print $1}')"
rm -rf "$VERIFY"

# ---------------------------------------------------------------------------
# A plug-in of its own, with its own installer (AU + VST3):
#   extra_installer <product> <cmake target> <bundle id> <pkg path> <welcome text (HTML)>
extra_installer()
{
    local product="$1" target="$2" id="$3" pkg="$4" about="$5"
    local work="$BUILD_DIR/pkg-$target"
    rm -rf "$work"
    local vst3_root="$work/vst3-root" au_root="$work/au-root"
    mkdir -p "$vst3_root/Library/Audio/Plug-Ins/VST3" "$au_root/Library/Audio/Plug-Ins/Components" \
             "$work/components" "$work/scripts-au" "$work/resources"
    local source destination binary archs
    for pair in "VST3/$product.vst3:$vst3_root/Library/Audio/Plug-Ins/VST3/$product.vst3" \
                "AU/$product.component:$au_root/Library/Audio/Plug-Ins/Components/$product.component"; do
        source="$BUILD_DIR/${target}_artefacts/Release/${pair%%:*}"; destination="${pair#*:}"
        [ -d "$source" ] || fail "Expected build output is missing: $source"
        ditto --norsrc --noextattr --noqtn --noacl "$source" "$destination"
        xattr -cr "$destination" 2>/dev/null || true
        binary="$destination/Contents/MacOS/$product"
        [ -f "$binary" ] || fail "Plugin binary missing: $binary"
        archs=$(lipo -archs "$binary")
        [[ " $archs " == *" arm64 "* ]] || fail "$binary is not an Apple Silicon binary (found: $archs)"
        xcrun strip -x "$binary"
        codesign --force --deep --sign - "$destination"
        codesign --verify --deep --strict "$destination"
        echo "  $(basename "$destination"): $archs, $(du -sh "$destination" | awk '{print $1}'), ad-hoc signed, id $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$destination/Contents/Info.plist")"
    done
    component_plist "$vst3_root" "$work/vst3-components.plist"
    component_plist "$au_root"   "$work/au-components.plist"
    printf '#!/bin/sh\n/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true\nexit 0\n' > "$work/scripts-au/postinstall"
    chmod 755 "$work/scripts-au/postinstall"
    pkgbuild --quiet --root "$vst3_root" --component-plist "$work/vst3-components.plist" \
        --identifier "$id.vst3" --version "$VERSION" --install-location / "$work/components/$target-VST3.pkg"
    pkgbuild --quiet --root "$au_root" --component-plist "$work/au-components.plist" --scripts "$work/scripts-au" \
        --identifier "$id.au" --version "$VERSION" --install-location / "$work/components/$target-AU.pkg"
    cat > "$work/resources/welcome.html" <<HTML
<html><body style="font-family: -apple-system, Helvetica; font-size: 13px;">
<h2>$product ($VERSION) by Thomas Ross</h2>
$about
<ul>
<li><b>Audio Unit</b> &rarr; /Library/Audio/Plug-Ins/Components/$product.component</li>
<li><b>VST3</b> &rarr; /Library/Audio/Plug-Ins/VST3/$product.vst3</li>
</ul>
<p>Built natively for Apple Silicon (macOS 12 or later). Quit your DAWs before installing.</p>
</body></html>
HTML
    cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$product ($VERSION)</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check>
        <allowed-os-versions><os-version min="12.0"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>
    <choice id="au" title="$product Audio Unit (AU)" description="Installs $product.component in /Library/Audio/Plug-Ins/Components.">
        <pkg-ref id="$id.au"/>
    </choice>
    <choice id="vst3" title="$product VST3" description="Installs $product.vst3 in /Library/Audio/Plug-Ins/VST3.">
        <pkg-ref id="$id.vst3"/>
    </choice>
    <pkg-ref id="$id.au" version="$VERSION" onConclusion="none">$target-AU.pkg</pkg-ref>
    <pkg-ref id="$id.vst3" version="$VERSION" onConclusion="none">$target-VST3.pkg</pkg-ref>
</installer-gui-script>
XML
    rm -f "$pkg"
    productbuild --quiet --distribution "$work/distribution.xml" \
        --package-path "$work/components" --resources "$work/resources" "$pkg"
    local verify="$work/verify" component relative bundle
    rm -rf "$verify"
    pkgutil --expand-full "$pkg" "$verify"
    for entry in "$target-VST3.pkg:Library/Audio/Plug-Ins/VST3/$product.vst3" \
                 "$target-AU.pkg:Library/Audio/Plug-Ins/Components/$product.component"; do
        component="${entry%%:*}"; relative="${entry#*:}"
        bundle="$verify/$component/Payload/$relative"
        [ -f "$bundle/Contents/MacOS/$product" ] || fail "$component does not contain $relative"
        codesign --verify --deep --strict "$bundle" || fail "Signature check failed inside $component"
        if grep -q "<relocate>" "$verify/$component/PackageInfo" 2>/dev/null; then
            fail "$component is relocatable; Installer could put the plugin somewhere other than /Library"
        fi
        echo "  $component -> /$relative ($(lipo -archs "$bundle/Contents/MacOS/$product"), signature OK)"
    done
    echo "  size: $(( $(stat -f %z "$pkg") / 1024 )) KB   sha256: $(shasum -a 256 "$pkg" | awk '{print $1}')"
    rm -rf "$verify"
}

step "Triad Time: its own installer"
extra_installer "$TIME_PRODUCT" TriadTime audio.thomasross.triadtime "$TIME_PKG_PATH" \
"<p>A time and volume gate: draw how far back it plays (repeats, half speed, tape stops, reverse) and a gate,
with Triad's LFO shapes, USER folder and shape generator. Synced to your song's tempo.</p>
<p>It is also built into $PRODUCT. This installer adds it as a plug-in of its own, for any track in any DAW:</p>"

step "Triad Split: its own installer"
extra_installer "$SPLIT_PRODUCT" TriadSplit audio.thomasross.triadsplit "$SPLIT_PKG_PATH" \
"<p>Split mode: Triad's band split as a plug-in of its own. Put one on each mixer insert or track that should hear
one band, and use your DAW's own plug-ins on that band. Triad Splits in the same LINK group share their dividers,
and every band also comes out on its own output (for FL Studio's Patcher).</p>
<p>This installer adds:</p>"

echo
echo "Installers ready:"
echo "  $PKG_PATH"
echo "  $TIME_PKG_PATH"
echo "  $SPLIT_PKG_PATH"
echo
echo "Double-click it to install the AU and VST3 for all users (asks for your Mac password)."
echo "It is signed only locally, so share it with other Macs only for testing (see README)."
[ -n "${CI:-}" ] || open -R "$PKG_PATH" 2>/dev/null || true
