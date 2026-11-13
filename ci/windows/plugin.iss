; Windows installer for one of Triad's plug-ins (Inno Setup 6). The CI passes:
;   /DAppId=... /DAppName=... /DAppVersion=... /DBundleDir=<built .vst3 folder> /DBundleName=<installed .vst3 name>
;   /DOutBase=<installer file name> /DReplaceOld=1 (Triad: remove earlier "Triad vN.vst3" so only one copy is installed)
; The plug-in goes to the standard VST3 folder, C:\Program Files\Common Files\VST3.

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Thomas Ross
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UninstallDisplayName={#AppName} (VST3)
UninstallFilesDir={commonpf64}\Thomas Ross\{#AppName}
OutputDir=Installer
OutputBaseFilename={#OutBase}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes

[Messages]
WelcomeLabel2=This installs [name/ver] as a VST3 plug-in in C:\Program Files\Common Files\VST3, where FL Studio, Ableton Live and other hosts find it.%n%nClose your DAW before continuing, then rescan plug-ins after installing.

[InstallDelete]
#ifdef ReplaceOld
; Earlier Triad releases ("Triad v21.vst3" ...): the plug-in IDs are the same, so projects open with this one.
Type: filesandordirs; Name: "{commoncf64}\VST3\Triad v*.vst3"
#endif
Type: filesandordirs; Name: "{commoncf64}\VST3\{#BundleName}"

[Files]
Source: "{#BundleDir}\*"; DestDir: "{commoncf64}\VST3\{#BundleName}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#BundleName}"
