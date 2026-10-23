param([string]$Destination)
$ErrorActionPreference='Stop'
$nativeSource=$PSScriptRoot
$manualRoot=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $manualRoot){throw 'Choose a new output directory; this script does not overwrite an existing package.'}
$gameRoot=Join-Path $manualRoot 'GAME_FILES'
New-Item -ItemType Directory -Path (Join-Path $gameRoot 'AC4Fix') -Force | Out-Null
$manualCatalog=Get-Content -LiteralPath (Join-Path $nativeSource 'payload-hashes.json') -Raw | ConvertFrom-Json
foreach($manualEntry in $manualCatalog.PSObject.Properties){
 $name=$manualEntry.Name
 if($name -match '(^/|\\|:|(^|/)\.\.(/|$))'){throw 'Unsafe catalog path'}
 $sourceFile=Join-Path (Join-Path $nativeSource 'payload') $name
 if((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manualEntry.Value){throw "Payload mismatch: $name"}
 $targetFile=Join-Path $gameRoot $name
 New-Item -ItemType Directory -Path (Split-Path $targetFile) -Force | Out-Null
 Copy-Item -LiteralPath $sourceFile -Destination $targetFile
}
$manualIni=[IO.File]::ReadAllText((Join-Path $nativeSource 'Settings.template.ini'))
foreach($placeholder in @{CPU='1';FPS='1';LIMIT='0';DIAG='0';PRESET='game';CHARLOD='-1';LODSCALE='-1'}.GetEnumerator()){
 $manualIni=$manualIni.Replace(('{'+$placeholder.Key+'}'),$placeholder.Value)
}
[IO.File]::WriteAllText((Join-Path $gameRoot 'AC4Fix\Settings.ini'),$manualIni,[Text.UTF8Encoding]::new($false))
$manualFiles=@(Get-ChildItem -LiteralPath $gameRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($gameRoot.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$manualDirs=@(Get-ChildItem -LiteralPath $gameRoot -Directory -Recurse | ForEach-Object {$_.FullName.Substring($gameRoot.Length+1).Replace('\','/')})
$manualManifest=[ordered]@{product='AC4-Modular-Patch';version='0.3.2-native-manual';patchVersion='0.3.2-alpha';manual=$true;root='';exeSha256='bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150';build='Steam (2023)';fixes=$true;cpu=$true;physx=$true;limit=0;preset=0;diag=$false;baseline=@();dirs=$manualDirs;files=$manualFiles}
[IO.File]::WriteAllText((Join-Path $gameRoot 'AC4Fix\install-manifest.json'),($manualManifest|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
$list=@(Get-ChildItem -LiteralPath $gameRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
 $name=$_.FullName.Substring($gameRoot.Length+1).Replace('\','/')
 '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$name
})
[IO.File]::WriteAllLines((Join-Path $manualRoot 'FILELIST_SHA256.txt'),$list,[Text.UTF8Encoding]::new($false))
foreach($manualLanguage in @('RU','EN')){
 Copy-Item -LiteralPath (Join-Path $nativeSource ('manual-docs\README_'+$manualLanguage+'.txt')) -Destination (Join-Path $manualRoot ('README_'+$manualLanguage+'.txt'))
}
Write-Output ('Manual package: {0} game files; no executable installer or scripts.' -f $list.Count)
