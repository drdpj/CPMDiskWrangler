; Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler.
; Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
;
; The Windows installer (Inno Setup 6.3 or later), made from deploy/ as ci/build.sh windows leaves it, with LICENSE,
; THIRD-PARTY-NOTICES.md and licences/ copied in (the workflow's Package step). The workflow runs it for a release
; tag (and for Run workflow), from the source root:
;
;   iscc /DProgName=acorndiskwrangler "/DProgTitle=Acorn Disk Wrangler" "/DProgFolder=Acorn Disk Wrangler"
;        /DProgVersion=0.9.7-beta.2 /DProgNumVersion=0.9.7 "/DProgCopyright=Copyright (C) 2026 Daniel Jameson"
;        /DProgUrl=https://github.com/drdpj/AcornDiskWrangler /DDeployDir=C:\...\deploy /DOutDir=C:\...\dist
;        /DOutName=acorndiskwrangler-v0.9.7-beta.2-windows-x64-setup ci\installer.iss
;
; ProgFolder is ProgTitle made fit for a folder or shortcut name ("CP/M" -> "CPM"). It installs for all users by
; default, into Program Files (Windows asks for an administrator), and its first page offers "only for me" instead
; (no administrator, into %LOCALAPPDATA%\Programs; also /CURRENTUSER on the command line, as the workflow's check
; uses). An install over an older version replaces it. Each program keeps its own AppId below, for good: Windows
; knows an installed program by it.
;
; Qt's DLLs and plugin folders change between releases (MSYS2's Qt moves on), so an update first deletes the DLLs
; beside the program and each folder this version ships; the workflow lists those folders in
; build\installer-delete.iss. Anything else in the program's folder (a portable acorndiskwrangler.ini, say) is kept.

#ifndef ProgName
  #error ProgName (the program, e.g. acorndiskwrangler) must be given: /DProgName=...
#endif

#if ProgName == "acorndiskwrangler"
  #define AppGuid "{{88BAD00B-21F2-483B-8186-447786EAA280}"
#elif ProgName == "cpmdiskwrangler"
  #define AppGuid "{{4EF59823-E240-48C7-9454-6D5F7314CA66}"
#else
  #error Unknown program: add its own AppId (a new GUID) here
#endif

#ifndef ProgTitle
  #define ProgTitle ProgName
#endif
#ifndef ProgFolder
  #define ProgFolder ProgTitle
#endif
#ifndef ProgVersion
  #define ProgVersion "0.0.0"
#endif
#ifndef ProgNumVersion
  #define ProgNumVersion "0.0.0"
#endif
#ifndef ProgCopyright
  #define ProgCopyright "Copyright (C) 2026 Daniel Jameson"
#endif
#ifndef ProgUrl
  #define ProgUrl "https://github.com/drdpj"
#endif
#ifndef DeployDir
  #define DeployDir AddBackslash(SourcePath) + "..\deploy"
#endif
#ifndef OutDir
  #define OutDir AddBackslash(SourcePath) + "..\dist"
#endif
#ifndef OutName
  #define OutName ProgName + "-" + ProgVersion + "-windows-x64-setup"
#endif

[Setup]
AppId={#AppGuid}
AppName={#ProgTitle}
AppVersion={#ProgVersion}
AppVerName={#ProgTitle} {#ProgVersion}
AppPublisher=Daniel Jameson
AppPublisherURL={#ProgUrl}
AppSupportURL={#ProgUrl}/issues
AppUpdatesURL={#ProgUrl}/releases
AppCopyright={#ProgCopyright}
VersionInfoVersion={#ProgNumVersion}
VersionInfoProductName={#ProgTitle}
VersionInfoDescription={#ProgTitle} setup
DefaultDirName={autopf}\{#ProgFolder}
DefaultGroupName={#ProgFolder}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#AddBackslash(SourcePath)}..\LICENSE
SetupIconFile={#AddBackslash(SourcePath)}..\icons\{#ProgName}.ico
UninstallDisplayIcon={app}\{#ProgName}.exe
UninstallDisplayName={#ProgTitle}
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

#if FileExists(AddBackslash(SourcePath) + "..\build\installer-delete.iss")
  #include "..\build\installer-delete.iss"
#endif

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#ProgFolder}"; Filename: "{app}\{#ProgName}.exe"
Name: "{autodesktop}\{#ProgFolder}"; Filename: "{app}\{#ProgName}.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ProgName}.exe"; Description: "{cm:LaunchProgram,{#ProgFolder}}"; Flags: nowait postinstall skipifsilent
