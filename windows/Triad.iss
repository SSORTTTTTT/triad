; Triad for Windows: one setup program for Triad, Triad Time and Triad Split (VST3, 64-bit).
; Built by .github/workflows/build-windows.yml:
;   iscc /DAppVersion=0.15.0 /DProductName="Triad v14" /DSourceDir=<folder with the three .vst3 bundles> windows\Triad.iss
; Installs into the standard VST3 folder (C:\Program Files\Common Files\VST3), where FL Studio, Ableton Live and
; every other Windows DAW look, and removes the earlier "Triad", "Triad v2" ... versions first (same plug-in ID:
; projects made with them open in the new one).

#ifndef AppVersion
  #define AppVersion "0.15.0"
#endif
#ifndef ProductName
  #define ProductName "Triad v14"
#endif
#ifndef SourceDir
  #define SourceDir "..\build-win\vst3"
#endif
#ifndef OutputName
  #define OutputName "Triad-v14-" + AppVersion + "-Windows-x64-Setup"
#endif

[Setup]
AppId={{6A0E2F5B-3C1D-4E8A-9B7F-5D2C1A4E7B90}
AppName=Triad
AppVersion={#AppVersion}
AppVerName={#ProductName} ({#AppVersion})
AppPublisher=Thomas Ross
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=Triad
UninstallDisplayName={#ProductName}, Triad Time and Triad Split
OutputDir=..\Installer
OutputBaseFilename={#OutputName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
SetupLogging=yes
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Thomas Ross
VersionInfoDescription={#ProductName} setup (VST3)

[Messages]
WelcomeLabel2=This installs [name/ver] on your computer: {#ProductName} (the multiband effects host), Triad Time (the time and volume gate) and Triad Split (one band per track), as 64-bit VST3 plug-ins.%n%nClose FL Studio, Ableton Live and any other music program before you continue.

[Components]
Name: "triad"; Description: "{#ProductName} (multiband effects host)"; Types: full custom compact; Flags: fixed
Name: "time"; Description: "Triad Time (time and volume gate)"; Types: full custom
Name: "split"; Description: "Triad Split (one band per track or mixer insert)"; Types: full custom

[InstallDelete]
; Earlier versions of Triad (the same plug-in under its earlier names): removed so a DAW never sees two copies.
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v2.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v3.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v4.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v5.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v6.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v7.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v8.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v9.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v10.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v11.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v12.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v13.vst3"
; This version's own bundles, replaced whole (no files left over from an earlier build).
Type: filesandordirs; Name: "{commoncf64}\VST3\{#ProductName}.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad Time.vst3"; Components: time
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad Split.vst3"; Components: split

[Files]
Source: "{#SourceDir}\{#ProductName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#ProductName}.vst3"; Components: triad; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Triad Time.vst3\*"; DestDir: "{commoncf64}\VST3\Triad Time.vst3"; Components: time; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Triad Split.vst3\*"; DestDir: "{commoncf64}\VST3\Triad Split.vst3"; Components: split; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#ProductName}.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad Time.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad Split.vst3"

[Run]
Filename: "{commoncf64}\VST3"; Description: "Show the VST3 folder"; Flags: postinstall shellexec skipifsilent unchecked
