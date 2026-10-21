param([string]$PackagePath)
$ErrorActionPreference='Stop'
$sourceRoot=$PSScriptRoot
$packageRoot=(Resolve-Path -LiteralPath $PackagePath).Path
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'generated'),(Join-Path $sourceRoot 'payload') -Force | Out-Null
$catalog=Get-Content -LiteralPath (Join-Path $packageRoot 'payload-hashes.json') -Raw | ConvertFrom-Json
$entries=@($catalog.PSObject.Properties | Sort-Object Name)
$header=[Collections.Generic.List[string]]::new()
$header.Add('#pragma once')
$header.Add('struct EmbeddedFile { int id; const char* path; const char* sha; };')
$header.Add('inline constexpr EmbeddedFile embeddedFiles[] = {')
$resource=[Collections.Generic.List[string]]::new()
$resource.Add('#include <windows.h>')
$resource.Add('200 ICON "../assets/ac4.ico"')
$resource.Add('201 RCDATA "../assets/background.jpg"')
$resource.Add('202 RCDATA "../Settings.template.ini"')
$resource.Add('203 RCDATA "../payload-hashes.json"')
$resource.Add('204 RCDATA "../vendor/LICENSE-json.txt"')
$resource.Add('1 VERSIONINFO')
$resource.Add('FILEVERSION 0,3,1,0')
$resource.Add('PRODUCTVERSION 0,3,0,0')
$resource.Add('FILEFLAGSMASK 0x3fL')
$resource.Add('FILEFLAGS 0x2L')
$resource.Add('FILEOS 0x40004L')
$resource.Add('FILETYPE 0x1L')
$resource.Add('BEGIN')
$resource.Add(' BLOCK "StringFileInfo" BEGIN BLOCK "040904b0" BEGIN')
$resource.Add(' VALUE "CompanyName", "AC4 PhysX Fix community project\0"')
$resource.Add(' VALUE "FileDescription", "AC4 PhysX Fix Native Installer\0"')
$resource.Add(' VALUE "FileVersion", "0.3.1.0\0"')
$resource.Add(' VALUE "ProductName", "AC4 Modular Patch\0"')
$resource.Add(' VALUE "ProductVersion", "0.3.0 alpha\0"')
$resource.Add(' VALUE "OriginalFilename", "AC4-PhysX-Fix-Setup.exe\0"')
$resource.Add(' END END')
$resource.Add(' BLOCK "VarFileInfo" BEGIN VALUE "Translation", 0x409, 1200 END')
$resource.Add('END')
$resourceId=1000
foreach($entry in $entries){
 $relative=$entry.Name
 if($relative -match '(^/|\\|:|(^|/)\.\.(/|$))'){throw "Unsafe package path $relative"}
 $inputFile=Join-Path (Join-Path $packageRoot 'payload') $relative
 if((Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw "Payload hash mismatch: $relative"}
 $outputFile=Join-Path (Join-Path $sourceRoot 'payload') $relative
 New-Item -ItemType Directory -Path (Split-Path $outputFile) -Force | Out-Null
 Copy-Item -LiteralPath $inputFile -Destination $outputFile
 $header.Add(('{{{0},"{1}","{2}"}},' -f $resourceId,$relative,$entry.Value))
 $resource.Add(('{0} RCDATA "../payload/{1}"' -f $resourceId,$relative))
 $resourceId++
}
$header.Add('};')
foreach($name in @('Settings.template.ini','payload-hashes.json')){Copy-Item -LiteralPath (Join-Path $packageRoot $name) -Destination (Join-Path $sourceRoot $name)}
[IO.File]::WriteAllLines((Join-Path $sourceRoot 'generated\payload.h'),$header,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines((Join-Path $sourceRoot 'generated\package.rc'),$resource,[Text.UTF8Encoding]::new($false))
Write-Output ('Verified and embedded {0} payload files.' -f $entries.Count)
