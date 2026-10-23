// Private x86 diagnostic/correction draft. All logging is nonblocking on workers.
#include <windows.h>
#include <stdint.h>
static_assert(sizeof(void*)==4,"x86 only");
struct CameraLeanRecord {
 DWORD frame,thread,binding,index,owner,proxy,property;
 float curve,oldValue,weightedDelta,scale,appliedDelta;
 DWORD mode;
 DWORD lifecycleValid,group,sequence,sequenceDef,clipDef,trackDef,source;
 DWORD groupFlags,sequenceFlags,definitionFlags,trackFlags;
 DWORD runtimeVtable,clipVtable,ref8Owner,refCOwner,definitionFlagsHi;
 float time,previousTime,step,speed,clipStart,clipEnd,duration[3],weight;

};
static const DWORD RECORD_CAPACITY=16384;
static CameraLeanRecord records[RECORD_CAPACITY];
static DWORD recordCount=0;
static volatile LONG dropped=0,frameId=0,mode=0,ownerThread=0;
static volatile LONG scale30Bits=0x3f800000,scale60Bits=0x3f800000;
static float smoothDt=0;
static CRITICAL_SECTION recordLock;
static volatile LONG ready=0;
static DWORD resumeAddress=0x705A12;
static volatile LONG stage[15]={0};
static bool Finite(float x){union{float f;DWORD u;}v;v.f=x;return(v.u&0x7f800000u)!=0x7f800000u;}
static LONG Bits(float x){union{float f;LONG l;}v;v.f=x;return v.l;}
static float ReadScale(volatile LONG* p){union{float f;LONG l;}v;v.l=InterlockedCompareExchange(p,0,0);return v.f;}
extern "C" void CameraLeanDraft_Init(){InitializeCriticalSection(&recordLock);InterlockedExchange(&ready,1);}
extern "C" void CameraLeanDraft_SetMode(LONG next){if(next>=0&&next<=3)InterlockedExchange(&mode,next);}
extern "C" void CameraLeanDraft_Frame(float seconds){
 LONG tid=(LONG)GetCurrentThreadId(),prior=InterlockedCompareExchange(&ownerThread,tid,0);
 if(prior&&prior!=tid)return;
 InterlockedIncrement(&frameId);
 float a=1,b=1;
 if(Finite(seconds)&&seconds>0&&seconds<=0.2f){
  if(smoothDt==0)smoothDt=seconds;else smoothDt+=0.05f*(seconds-smoothDt);
  a=smoothDt*30;if(a>4)a=4;b=smoothDt*60;if(b>4)b=4;
 }
 InterlockedExchange(&scale30Bits,Bits(a));InterlockedExchange(&scale60Bits,Bits(b));
}
extern "C" DWORD CameraLeanDraft_Drain(CameraLeanRecord* out,DWORD capacity,DWORD* droppedOut){
 if(!ready||!out||!TryEnterCriticalSection(&recordLock))return 0;
 DWORD n=recordCount<capacity?recordCount:capacity;
 for(DWORD i=0;i<n;i++)out[i]=records[i];
 for(DWORD i=n;i<recordCount;i++)records[i-n]=records[i];
 recordCount-=n;
 if(droppedOut)*droppedOut=(DWORD)InterlockedExchange(&dropped,0);
 LeaveCriticalSection(&recordLock);return n;
}
static float SafeCurve(DWORD args){
 __try{DWORD value=*(DWORD*)(args+8);return value?*(float*)value:0;}
 __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
// Read-only ancestry. Derived independently from 6FC5B0/6297E0/62BFA0/70BE10.
// Failure marks diagnostics incomplete; it never cancels a recognised correction.
static DWORD ResolveRef(DWORD ref){
 if(!ref)return 0;
 return ((LONG)*(DWORD*)(ref+8)<0)?*(DWORD*)ref:0;
}
static void SnapshotLifecycle(CameraLeanRecord* r,DWORD binding,DWORD args){
 __try{
  r->source=*(DWORD*)(args+8);r->weight=*(float*)(args+12);
  r->trackDef=*(DWORD*)(binding+4);
  r->trackFlags=r->trackDef?*(DWORD*)r->trackDef:0;
  r->group=*(DWORD*)binding;if(!r->group)return;
  r->sequence=*(DWORD*)r->group;r->clipDef=*(DWORD*)(r->group+4);
  r->groupFlags=*(BYTE*)(r->group+0x10);
  if(!r->sequence||!r->clipDef)return;
  r->time=*(float*)(r->sequence+0x14);r->previousTime=*(float*)(r->sequence+0x98);
  r->step=*(float*)(r->sequence+0x9C);r->speed=*(float*)(r->sequence+0x18);
  r->sequenceFlags=*(WORD*)(r->sequence+0x94);
  r->clipStart=*(float*)(r->clipDef+4);r->clipEnd=*(float*)(r->clipDef+8);
  r->sequenceDef=ResolveRef(*(DWORD*)(r->sequence+0x10));
  if(!r->sequenceDef)return;
  r->definitionFlags=*(DWORD*)(r->sequenceDef+8);
  r->definitionFlagsHi=*(DWORD*)(r->sequenceDef+0xC);
  r->runtimeVtable=*(DWORD*)r->sequence;r->clipVtable=*(DWORD*)r->clipDef;
  r->ref8Owner=ResolveRef(*(DWORD*)(r->sequence+8));
  r->refCOwner=ResolveRef(*(DWORD*)(r->sequence+0xC));
  for(int i=0;i<3;i++)r->duration[i]=*(float*)(r->sequenceDef+0x18+i*4);
  r->lifecycleValid=1;
 }__except(EXCEPTION_EXECUTE_HANDLER){r->lifecycleValid=0;}
}
static __declspec(noinline) void __cdecl Filter(DWORD binding,DWORD index,BYTE* savedState,DWORD args){
 InterlockedIncrement(&stage[0]);
 LONG frame=InterlockedCompareExchange(&frameId,0,0);
 if(!ready||!frame){InterlockedIncrement(&stage[1]);return;}
 // Counter only: tracks legitimately run on workers. Never reject by thread ID.
 if((LONG)GetCurrentThreadId()!=InterlockedCompareExchange(&ownerThread,0,0))InterlockedIncrement(&stage[2]);
 __try{
  DWORD fx=*(volatile DWORD*)0x2AC2170;
  if(!fx||*(DWORD*)fx!=0x1E5D688){InterlockedIncrement(&stage[3]);return;}
  DWORD descriptor=*(DWORD*)(binding+4);
  if(!descriptor){InterlockedIncrement(&stage[4]);return;}
  DWORD flags=*(DWORD*)descriptor;
  if((flags&7)!=1||((flags>>6)&7)!=1){InterlockedIncrement(&stage[5]);return;}
  if(index>=*(WORD*)(binding+0x8A)){InterlockedIncrement(&stage[6]);return;}
  DWORD list=*(DWORD*)(binding+0x84);
  if(!list){InterlockedIncrement(&stage[7]);return;}
  DWORD entry=list+index*0x1C,proxy=*(DWORD*)(entry+0x14),accessor=entry;
  if(proxy&&*(DWORD*)proxy)accessor=proxy+4;
  DWORD owner=*(DWORD*)(accessor+4),af=*(DWORD*)(accessor+8);
  if(owner!=fx){InterlockedIncrement(&stage[9]);return;}
  if(af&2){InterlockedIncrement(&stage[8]);return;}
  DWORD property=*(DWORD*)accessor;
  if(af&1){
   // A39A70: inline packed offset + primitive kind. 10 = float32.
   // Exact direct scalar +1C only; indirect/indexed/other kinds stay untouched.
   if(property!=0x0070000A){InterlockedIncrement(&stage[10]);return;}
  }else{
   if(property!=0x278B058){InterlockedIncrement(&stage[10]);return;}
   if(*(DWORD*)(property+4)!=0x1F3BD00A||*(DWORD*)(property+0xC)!=0x000A0000||*(DWORD*)(property+0x10)!=0x00700000){InterlockedIncrement(&stage[11]);return;}
  }
  float delta=*(float*)(savedState+176),old=*(float*)(savedState+160);
  if(!Finite(delta)||!Finite(old)){InterlockedIncrement(&stage[12]);return;}
  LONG active=InterlockedCompareExchange(&mode,0,0);
  float scale=active==1?ReadScale(&scale30Bits):active==2?ReadScale(&scale60Bits):active==3?0:1;
  float applied=delta*scale;if(!Finite(applied)){InterlockedIncrement(&stage[12]);return;}
  // Construct potentially faulting diagnostic data BEFORE entering the queue lock.
  CameraLeanRecord r={};r.frame=(DWORD)frame;r.thread=GetCurrentThreadId();r.binding=binding;
  r.index=index;r.owner=owner;r.proxy=proxy;r.property=property;r.oldValue=old;
  r.weightedDelta=delta;r.scale=scale;r.appliedDelta=applied;r.mode=(DWORD)active;
  // Curve is diagnostic only: an unreadable curve must not block correction.
  r.curve=SafeCurve(args);SnapshotLifecycle(&r,binding,args);
  InterlockedIncrement(&stage[13]);
  if(active)*(float*)(savedState+176)=applied;
  if(TryEnterCriticalSection(&recordLock)){
   if(recordCount<RECORD_CAPACITY)records[recordCount++]=r;else InterlockedIncrement(&dropped);
   LeaveCriticalSection(&recordLock);
  }else InterlockedIncrement(&dropped);
 }__except(EXCEPTION_EXECUTE_HANDLER){InterlockedIncrement(&stage[14]);}
}
extern "C" __declspec(naked) void CameraLeanDraft_Hook(){
 __asm{
  mulss xmm1,dword ptr [ebx+0Ch]
  pushfd
  pushad
  mov edx,esp
  sub esp,544
  and esp,0FFFFFFF0h
  fxsave [esp]
  mov dword ptr [esp+512],edx
  mov eax,esp
  push ebx
  push eax
  push edi
  push esi
  call Filter
  add esp,16
  fxrstor [esp]
  mov esp,dword ptr [esp+512]
  popad
  popfd
  jmp dword ptr [resumeAddress]
 }
}
