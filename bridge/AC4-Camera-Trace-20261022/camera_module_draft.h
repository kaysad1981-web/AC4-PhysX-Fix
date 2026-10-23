// Unified research module. No installer, memory patcher, DLL entry point or file IO.
#pragma once
#include <windows.h>
#include "../AC4-Camera-Target-Filter-20261022/binding_filter_draft.h"
#include "../AC4-Camera-Target-Filter-20261022/copy_observer_draft.h"
namespace camera_module {
static_assert(sizeof(void*)==4,"x86 only");
static retirement_registry::Registry<32> shards[16];
static volatile LONG observing=0,enabled=0,sealed=0,applied=0,admissionRejected=0;
static bool configured=false;
static DWORD cameraPointerAddress=0;
static DWORD resumes[8]={};
static unsigned Shard(DWORD runtime){return ((runtime>>4)^(runtime>>12))&15;}
static DWORD Camera(){__try{return cameraPointerAddress?*(DWORD*)cameraPointerAddress:0;}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
// Classification is possible before binding construction. No owner/target reads here.
static bool HasCameraNode(DWORD runtime){
 __try {
  if(!camera_binding_filter::Span(runtime,0x158)||*(DWORD*)runtime!=0x1e66170)return false;
  DWORD groups=*(DWORD*)(runtime+0x8c);unsigned count=*(WORD*)(runtime+0x92);
  if(!count||count>64||!camera_binding_filter::Span(groups,count*0x1c))return false;
  for(unsigned g=0;g<count;g++){
   DWORD group=groups+g*0x1c,node=*(DWORD*)(group+4);
   if(*(DWORD*)group!=runtime)return false;
   if(camera_binding_filter::Span(node,4)&&*(DWORD*)node==0x1e659e0)return true;
  }
  return false;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static volatile LONG initBeginOK=0,initBeginFailed=0,initCameraOK=0,initCameraFailed=0;
static volatile LONG retireOK=0,retireFailed=0,targetRejected=0,exactRoll=0,identityMissing=0,identityMismatch=0,policyRejected=0;
// One-time call before exposing any adapter to other threads. Storage is process-lifetime.
static bool ConfigureBeforePublication(const DWORD* destinations,DWORD cameraAddress){
 if(configured||!cameraAddress||InterlockedCompareExchange(&sealed,0,0))return false;
 for(unsigned i=0;i<8;i++)if(!destinations[i])return false;
 for(unsigned i=0;i<8;i++)resumes[i]=destinations[i];cameraPointerAddress=cameraAddress;configured=true;
 InterlockedExchange(&observing,1);return true;
}
static bool SetEnabled(bool value){
 if(!value){InterlockedExchange(&enabled,0);return true;}
 if(!configured||InterlockedCompareExchange(&sealed,0,0)||!InterlockedCompareExchange(&observing,0,0))return false;
 InterlockedExchange(&enabled,1);
 if(InterlockedCompareExchange(&sealed,0,0)||!InterlockedCompareExchange(&observing,0,0)){InterlockedExchange(&enabled,0);return false;}
 return true;
}
// One-way disable. Does not remove detours or permit unloading/storage destruction.
static void Seal(){InterlockedExchange(&enabled,0);InterlockedExchange(&sealed,1);InterlockedExchange(&observing,0);}
static DWORD Runtime(unsigned kind,const DWORD* regs){return kind==2?regs[1]:(kind==3?regs[4]:(kind==6?regs[7]:regs[6]));}
static bool Observe(unsigned kind,const DWORD* regs,DWORD thread){
 if(kind>6||!InterlockedCompareExchange(&observing,0,0)||InterlockedCompareExchange(&sealed,0,0))return false;
 DWORD runtime=Runtime(kind,regs);auto& registry=shards[Shard(runtime)];
 if(kind==2&&!HasCameraNode(runtime)){InterlockedIncrement(&admissionRejected);registry.InvalidateRuntime(runtime);return false;}
 copy_observer::Bridge<32> observer(registry);bool ok=observer.Dispatch(kind,regs,thread);
 if(kind==1)InterlockedIncrement(ok?&initBeginOK:&initBeginFailed);
 if(kind==2)InterlockedIncrement(ok?&initCameraOK:&initCameraFailed);
 if(kind==3)InterlockedIncrement(ok?&retireOK:&retireFailed);
 return ok;
}
struct TraceRecord {LONGLONG qpc;DWORD thread,binding,index,outcome;retired_roll::Snapshot snapshot;float oldValue,delta;};
static volatile LONG traceEnabled=0;
static TraceRecord traceRing[512];static unsigned traceCount=0;static volatile LONG traceLock=0,traceLost=0;
static void RecordTrace(DWORD binding,DWORD index,const retired_roll::Snapshot& snapshot,BYTE* fx,DWORD outcome){
 if(!InterlockedCompareExchange(&traceEnabled,0,0))return;
 float old=*(float*)(fx+160),delta=*(float*)(fx+176);
 if(!std::isfinite(old)||!std::isfinite(delta)||(fabsf(old)<5&&fabsf(delta)<3))return;
 if(InterlockedCompareExchange(&traceLock,1,0)){InterlockedIncrement(&traceLost);return;}
 if(traceCount<512){TraceRecord& r=traceRing[traceCount++];LARGE_INTEGER q;QueryPerformanceCounter(&q);r.qpc=q.QuadPart;r.thread=GetCurrentThreadId();r.binding=binding;r.index=index;r.outcome=outcome;r.snapshot=snapshot;r.oldValue=old;r.delta=delta;}
 else InterlockedIncrement(&traceLost);
 InterlockedExchange(&traceLock,0);
}
static unsigned DrainTrace(TraceRecord* records,unsigned capacity){
 if(capacity<512||InterlockedCompareExchange(&traceLock,1,0))return 0;
 unsigned n=traceCount;for(unsigned i=0;i<n;i++)records[i]=traceRing[i];traceCount=0;
 InterlockedExchange(&traceLock,0);return n;
}
static bool Filter(DWORD binding,DWORD index,BYTE* savedFx){
 if(!InterlockedCompareExchange(&enabled,0,0)||InterlockedCompareExchange(&sealed,0,0))return false;
 DWORD camera=Camera();retired_roll::Snapshot snapshot={};
 if(!camera_binding_filter::ReadSnapshot(binding,index,camera,snapshot)){InterlockedIncrement(&targetRejected);return false;}
 InterlockedIncrement(&exactRoll);
 auto& registry=shards[Shard(snapshot.id.runtime)];retired_roll::Identity id={};
 if(!registry.Current(snapshot.id.runtime,id)){InterlockedIncrement(&identityMissing);RecordTrace(binding,index,snapshot,savedFx,1);return false;}
 if(id.definition!=snapshot.id.definition||id.reference!=snapshot.id.reference){InterlockedIncrement(&identityMismatch);RecordTrace(binding,index,snapshot,savedFx,2);return false;}
 snapshot.id=id;
 if(!std::isfinite(*(float*)(savedFx+160))||!std::isfinite(*(float*)(savedFx+176))||!registry.ShouldSuppress(snapshot,true)){InterlockedIncrement(&policyRejected);RecordTrace(binding,index,snapshot,savedFx,3);return false;}
 // Seal is not a quiescence barrier; lifecycle validity is still an engine precondition.
 if(InterlockedCompareExchange(&sealed,0,0))return false;
 RecordTrace(binding,index,snapshot,savedFx,4);*(DWORD*)(savedFx+176)=0;InterlockedIncrement(&applied);return true;
}
static __declspec(noinline) void __cdecl ObserveSaved(DWORD kind,DWORD* regs){DWORD error=GetLastError();Observe(kind,regs,GetCurrentThreadId());SetLastError(error);}
static __declspec(noinline) void __cdecl FilterSaved(DWORD binding,DWORD index,BYTE* savedFx){DWORD error=GetLastError();Filter(binding,index,savedFx);SetLastError(error);}
static __declspec(naked) void Observer0(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 0
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
xorps xmm0,xmm0
push ebx
push esi
mov esi,ecx
jmp dword ptr [resumes+0]
}}
static __declspec(naked) void Observer1(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 1
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
push ebp
mov ebp,esp
push ecx
mov al,[ebp+14h]
jmp dword ptr [resumes+4]
}}
static __declspec(naked) void Observer2(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 2
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
pop esi
pop ebx
mov esp,ebp
pop ebp
jmp dword ptr [resumes+8]
}}
static __declspec(naked) void Observer3(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 3
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
pop edi
xor al,al
pop ebx
mov esp,ebp
jmp dword ptr [resumes+12]
}}
static __declspec(naked) void Observer4(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 4
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
push ebp
mov ebp,esp
push ecx
push esi
jmp dword ptr [resumes+16]
}}
static __declspec(naked) void Observer5(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 5
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
push ebp
mov ebp,esp
mov edx,[ebp+8]
jmp dword ptr [resumes+20]
}}
static __declspec(naked) void Observer6(){__asm{
 pushfd
 cmp dword ptr [observing],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 sub dword ptr [edx+12],4
 push edx
 push 6
 call ObserveSaved
 add esp,8
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
mov esi,eax
push ebx
push esi
mov ecx,edi
jmp dword ptr [resumes+24]
}}
static __declspec(naked) void Additive(){__asm{
 mulss xmm1,dword ptr [ebx+0ch]
 pushfd
 cmp dword ptr [enabled],0
 je skip
 pushad
 cld
 mov edx,esp
 sub esp,544
 and esp,0fffffff0h
 fxsave [esp]
 mov [esp+512],edx
 mov eax,esp
 push eax
 push edi
 push esi
 call FilterSaved
 add esp,12
 fxrstor [esp]
 mov esp,[esp+512]
 popad
skip:
 popfd
 jmp dword ptr [resumes+28]
}}
}
