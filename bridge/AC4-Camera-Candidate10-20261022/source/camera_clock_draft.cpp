// Narrow FX clock correction, Steam x86 only. No memory patching or game calls here.
#include <windows.h>
static_assert(sizeof(void*)==4,"x86 only");
static volatile LONG mode=0,calls=0,smallSteps=0,eligible=0,bypassed=0,rejected=0,faults=0;
static DWORD resumeAddress=0x70BE85;
static bool Finite(float x){union{float f;DWORD u;}v;v.f=x;return(v.u&0x7f800000u)!=0x7f800000u;}
static void SetMode(LONG next){InterlockedExchange(&mode,next?1:0);}
static bool CameraOnly(DWORD runtime){
 // Do not retime effects that also contain sound/particles/gameplay nodes.
 if(!runtime||*(DWORD*)runtime!=0x1E66170)return false;
 DWORD count=*(WORD*)(runtime+0x92),groups=*(DWORD*)(runtime+0x8C);
 if(!groups||!count||count>8)return false;
 for(DWORD i=0;i<count;i++){
  DWORD group=groups+i*0x1C,definition=*(DWORD*)(group+4);
  if(*(DWORD*)group!=runtime||!definition||*(DWORD*)definition!=0x1E659E0)return false;
 }
 return true;
}
static __declspec(noinline) void __cdecl Filter(DWORD runtime,BYTE* saved,DWORD* savedFlags){
 InterlockedIncrement(&calls);
 __try{
  float raw=*(float*)(saved+160),step=*(float*)(saved+176),epsilon=*(float*)(saved+224);
  if(!Finite(raw)||raw<=0||!Finite(step)||!Finite(epsilon)||step<=0||step>epsilon)return;
  InterlockedIncrement(&smallSteps);
  if(!CameraOnly(runtime)){InterlockedIncrement(&rejected);return;}
  InterlockedIncrement(&eligible);
  if(InterlockedCompareExchange(&mode,0,0)){
   // Emulate COMISS(0, positiveStep) only for this gate: CF=1, ZF=PF=0.
   // XMM4 retains the game's epsilon for all subsequent boundary comparisons.
   *savedFlags=(*savedFlags&~0x45u)|1u;
   InterlockedIncrement(&bypassed);
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){InterlockedIncrement(&faults);}
}
extern "C" __declspec(naked) void Hook(){
 __asm{
  movaps xmm1,xmm0
  andps xmm1,xmm3
  comiss xmm4,xmm1
  pushfd
  pushad
  mov edx,esp
  sub esp,544
  and esp,0FFFFFFF0h
  fxsave [esp]
  mov dword ptr [esp+512],edx
  lea ecx,[edx+32]
  mov eax,esp
  push ecx
  push eax
  push esi
  call Filter
  add esp,12
  fxrstor [esp]
  mov esp,dword ptr [esp+512]
  popad
  popfd
  jmp dword ptr [resumeAddress]
 }
}
