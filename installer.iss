[Setup]
AppName=Rust Chroma Mod Client
AppVerName=Rust Chroma Mod Client 1.0
AppPublisher=Razer US Ltd.
AppPublisherURL=https://razer.com
AppSupportURL=https://support.razer.com
AppUpdatesURL=https://razer.com
DefaultDirName={userappdata}\RustChromaModClient
DefaultGroupName=Razer\RustChromaModClient
OutputBaseFilename=SetupRustChromaModClient
SetupIconFile=release_icon.ico
UninstallDisplayIcon=release_icon.ico
Compression=lzma
SolidCompression=yes
InfoBeforeFile=LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "Release\Cpp_RustChromaModClient.exe"; DestDir: "{userappdata}\RustChromaModClient\Cpp_RustChromaModClient.exe"; CopyMode: alwaysoverwrite
Source: "Animations\*.chroma"; DestDir: "{userappdata}\RustChromaModClient\Animations"; CopyMode: alwaysoverwrite

[Icons]
Name: "{group}\Rust Chroma Mod Client"; Filename: "{userappdata}\RustChromaModClient\Cpp_RustChromaModClient.exe"; WorkingDir: "{userappdata}\RustChromaModClient";
Name: "{commondesktop}\Rust Chroma Mod Client"; Filename: "{userappdata}\RustChromaModClient\Cpp_RustChromaModClient.exe"; WorkingDir: "{userappdata}\RustChromaModClient";
Name: "{group}\Uninstall Rust Chroma Mod Client"; Filename: "{uninstallexe}"

[Run]
Filename: "{userappdata}\RustChromaModClient\Cpp_RustChromaModClient.exe"; Description: "Launch Rust Chroma Mod Client"; Flags: postinstall skipifsilent runascurrentuser nowait; Parameters: "{userdesktop}\temp.chroma"; WorkingDir: "{userappdata}\RustChromaModClient"