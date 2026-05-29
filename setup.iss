[Setup]
AppName=Life After Life
AppVersion=0.1.0
AppPublisher=Mors
DefaultDirName={autopf}\LifeAfterLife
DefaultGroupName=Life After Life
OutputBaseFilename=LifeAfterLife-0.1.0-Setup
OutputDir=installer-output
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "out\Debug\LifeAfterLife.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "out\Debug\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\Life After Life"; Filename: "{app}\LifeAfterLife.exe"
Name: "{commondesktop}\Life After Life"; Filename: "{app}\LifeAfterLife.exe"

[Run]
Filename: "{app}\LifeAfterLife.exe"; Description: "Запустить Life After Life"; Flags: postinstall nowait

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
