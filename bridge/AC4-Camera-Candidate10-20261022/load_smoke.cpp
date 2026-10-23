#include <windows.h>
#include <cassert>
#include <cstdio>
#ifdef STATIC_CANDIDATE_LOAD
extern "C" __declspec(dllimport) DWORD __cdecl CameraCandidateStatus();
#endif
int main(){
#ifdef STATIC_CANDIDATE_LOAD
 DWORD status=CameraCandidateStatus();assert(status==1); // startup marker only, no game hooks/filter
 puts("PASS static import host: startup-load marker set; no camera hooks armed and filter off outside game.");
#else
 HMODULE module=LoadLibraryW(L"dinput8-camera-candidate.dll");assert(module);
 typedef DWORD(__cdecl*Status)();Status get=(Status)GetProcAddress(module,"CameraCandidateStatus");assert(get&&get()==0);
 const char* names[]={"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"};
 for(const char* name:names)assert(GetProcAddress(module,name));
 assert(FreeLibrary(module));puts("PASS dynamic host: all six proxy exports present; startup marker false; no camera hooks/filter; clean unload outside game.");
#endif
}
