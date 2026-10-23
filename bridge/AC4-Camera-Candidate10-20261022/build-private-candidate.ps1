$ErrorActionPreference='Stop'
$compilerRoot='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231'
$sdkRoot='C:\Program Files (x86)\Windows Kits\10'
$sdkVersion='10.0.26100.0'
$env:INCLUDE="$compilerRoot\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\um;$sdkRoot\Include\$sdkVersion\shared"
$env:LIB="$compilerRoot\lib\x86;$sdkRoot\Lib\$sdkVersion\ucrt\x86;$sdkRoot\Lib\$sdkVersion\um\x86"
$env:PATH="$compilerRoot\bin\Hostx64\x86;$sdkRoot\bin\$sdkVersion\x64;"+$env:PATH

& "$compilerRoot\bin\Hostx64\x86\cl.exe" /nologo /O2 /MT /EHsc /LD "$PSScriptRoot\source\bridge.cpp" "/Fo$PSScriptRoot\camera_candidate.obj" "/Fe$PSScriptRoot\dinput8-camera-candidate.dll" /link "/DEF:$PSScriptRoot\source\bridge.def" "/IMPLIB:$PSScriptRoot\camera_candidate.lib" /DYNAMICBASE /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" user32.lib advapi32.lib
if($LASTEXITCODE -ne 0){throw 'Private experimental DLL build failed'}
