$ErrorActionPreference='Stop'
$compilerRoot='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$sdkVersion='10.0.26100.0'
$env:INCLUDE="$compilerRoot\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\um;$sdkRoot\Include\$sdkVersion\shared"
$env:LIB="$compilerRoot\lib\x86;$sdkRoot\Lib\$sdkVersion\ucrt\x86;$sdkRoot\Lib\$sdkVersion\um\x86"
$env:PATH="$compilerRoot\bin\Hostx64\x86;$sdkRoot\bin\$sdkVersion\x64;"+$env:PATH
& "$compilerRoot\bin\Hostx64\x86\cl.exe" /nologo /O2 /MT /EHsc  "$PSScriptRoot\load_smoke.cpp"  "/Fo$PSScriptRoot\dynamic_load_smoke.obj" "/Fe$PSScriptRoot\dynamic_load_smoke.exe" /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
if($LASTEXITCODE -ne 0){throw 'Smoke host build failed'}
& "$compilerRoot\bin\Hostx64\x86\cl.exe" /nologo /O2 /MT /EHsc /DSTATIC_CANDIDATE_LOAD "$PSScriptRoot\load_smoke.cpp" "$PSScriptRoot\camera_candidate.lib" "/Fo$PSScriptRoot\static_load_smoke.obj" "/Fe$PSScriptRoot\static_load_smoke.exe" /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
if($LASTEXITCODE -ne 0){throw 'Smoke host build failed'}
