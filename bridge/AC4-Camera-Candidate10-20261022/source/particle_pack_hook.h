#pragma once
#include "particle_pack.h"
static DWORD particlePackEnd=0x864282;
static unsigned particlePackCapacity=100000;
// Called only by the opt-in hook. No allocation, timing, logging or global mutable cache.
static int __stdcall TryParticlePack(const BYTE* desc,const BYTE* src,unsigned n){
#ifdef AC4_PARTICLE_METRICS
 return MeasureParticlePack(desc,src,n,particlePackCapacity,(BYTE*)stagingBase);
#else
 return PackParticleRecords((BYTE*)stagingBase,src,n,particlePackCapacity,desc)?1:0;
#endif
}
__declspec(naked) static void ParticlePackHook(){__asm{
 pushfd
 pushad
 mov ebx,esp
 sub esp,528
 and esp,-16
 fxsave [esp]
 push dword ptr [ebp+0ch]
 push dword ptr [ebx+16]
 push esi
 call TryParticlePack
 test eax,eax
 jz oldpath
 fxrstor [esp]
 mov esp,ebx
 popad
 popfd
 // Match the loop's live end state. EDX/XMM0 are dead before the next call.
 mov eax,dword ptr [ebp+0ch]
 shl eax,2
 mov dword ptr [ebp+8],eax
 mov ecx,eax
 mov edx,dword ptr [esi+0ch]
 imul edx,eax
 add ebx,edx
 shl eax,6
 mov edi,dword ptr [stagingFirst]
 add edi,eax
 mov eax,ecx
 cmp eax,ecx
 jmp dword ptr [particlePackEnd]
oldpath:
 fxrstor [esp]
 mov esp,ebx
 popad
 popfd
 jmp ParticleFillHook
}}
