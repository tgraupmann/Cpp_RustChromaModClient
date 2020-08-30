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
Source: "Release\Cpp_RustChromaModClient.exe"; DestDir: "{userappdata}\RustChromaModClient"; CopyMode: alwaysoverwrite
Source: "Animations\*.chroma"; DestDir: "{userappdata}\RustChromaModClient\Animations"; CopyMode: alwaysoverwrite
