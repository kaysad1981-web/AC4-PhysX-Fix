#include "../../AC4-Camera-Trace-20261022/camera_module_draft.h"
static bool ctStartupLoad=false,ctDiagnostics=false;
// Private release032 adapter. Included after Queue/Jump/Apply, before Initialize.
namespace camera_draft {
#include "camera_lean_draft.cpp"
}
namespace camera_clock {
#include "camera_clock_draft.cpp"
}
struct CtRender {
 LONGLONG qpc;DWORD frame,thread,mode,fx,singleton,enabled,bypass,mark;
 DWORD camera,cameraVtable; float fov;
 float alpha,base[4],finalQ[4],angles[4];
};
static CRITICAL_SECTION ctLock;
static CtRender ctRing[512],ctCopy[512];static unsigned ctN=0,ctLost=0;
static camera_draft::CameraLeanRecord ctTracks[camera_draft::RECORD_CAPACITY];
static bool ctQueued=false,ctF4=false;static volatile LONG ctArmed=0,ctFrame=0;
static DWORD ctLastFlush=0;static LONGLONG ctLast=0,ctStart=0;
static HANDLE ctRenderFile=INVALID_HANDLE_VALUE,ctTrackFile=INVALID_HANDLE_VALUE;
static DWORD ctRenderResume=0x413E2B;
static volatile LONG ctFix=0;

static char ctText[1024*1024];
static HANDLE ctEffectFile=INVALID_HANDLE_VALUE;
static camera_module::TraceRecord ctEffects[512];

static HANDLE CtOpen(const wchar_t* kind,const char* header) {
 wchar_t p[MAX_PATH];wcscpy_s(p,logPath);wchar_t* leaf=wcsrchr(p,L'\\');
 if(!leaf)return INVALID_HANDLE_VALUE;
 SYSTEMTIME st;GetLocalTime(&st);
 swprintf_s(leaf+1,MAX_PATH-(leaf+1-p),L"camera-%s-release032-%04u%02u%02u-%02u%02u%02u-%lu.csv",
  kind,st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,GetCurrentProcessId());
 HANDLE h=CreateFileW(p,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
 if(h!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(h,header,(DWORD)strlen(header),&n,0);}
 else Log("CAMERA 0.3.2: could not create a new diagnostic CSV.");
 return h;
}
static void CtWrite(HANDLE h,unsigned n) {
 if(h==INVALID_HANDLE_VALUE||!n)return;
 DWORD wrote=0;if(!WriteFile(h,ctText,n,&wrote,0)||wrote!=n)Log("CAMERA 0.3.2: diagnostic CSV write incomplete.");
}
static void CtFlush() {
 unsigned n=0,lost=0;
 if(TryEnterCriticalSection(&ctLock)){
  n=ctN;for(unsigned i=0;i<n;i++)ctCopy[i]=ctRing[i];
  ctN=0;lost=ctLost;ctLost=0;LeaveCriticalSection(&ctLock);
 }
 unsigned used=0;
 for(unsigned i=0;i<n;i++){
  CtRender& r=ctCopy[i];
  int len=sprintf_s(ctText+used,sizeof(ctText)-used,
   "%.3f,%lu,%lu,%lu,%08lx,%08lx,%lu,%lu,%lu,%.5f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.6f,%.6f,%.6f,%.6f,%08lx,%08lx,%.6f\r\n",
   (double)(r.qpc-ctStart)*1000.0/frequency,r.frame,r.thread,r.mode,r.fx,r.singleton,r.enabled,r.bypass,r.mark,r.alpha,
   r.base[0],r.base[1],r.base[2],r.base[3],r.finalQ[0],r.finalQ[1],r.finalQ[2],r.finalQ[3],
   r.angles[0],r.angles[1],r.angles[2],r.angles[3],r.camera,r.cameraVtable,r.fov);
  if(len>0)used+=(unsigned)len;
 }
 CtWrite(ctRenderFile,used);
 DWORD trackLost=0,tn=camera_draft::CameraLeanDraft_Drain(ctTracks,camera_draft::RECORD_CAPACITY,&trackLost);
 used=0;
 for(DWORD i=0;i<tn;i++){
  if(sizeof(ctText)-used<2048){CtWrite(ctTrackFile,used);used=0;}
  camera_draft::CameraLeanRecord& r=ctTracks[i];
  int len=sprintf_s(ctText+used,sizeof(ctText)-used,"%lu,%lu,%lu,%08lx,%lu,%08lx,%08lx,%08lx,%.7g,%.7g,%.7g,%.7g,%.7g,%lu,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%lu,%lu,%lu,%lu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%08lx,%08lx,%08lx,%08lx,%lu\r\n",
   r.frame,r.thread,r.mode,r.binding,r.index,r.owner,r.proxy,r.property,r.curve,r.oldValue,r.weightedDelta,r.scale,r.appliedDelta,
   r.lifecycleValid,r.group,r.sequence,r.sequenceDef,r.clipDef,r.trackDef,r.source,
   r.groupFlags,r.sequenceFlags,r.definitionFlags,r.trackFlags,
   r.time,r.previousTime,r.step,r.speed,r.clipStart,r.clipEnd,r.duration[0],r.duration[1],r.duration[2],r.weight,r.runtimeVtable,r.clipVtable,r.ref8Owner,r.refCOwner,r.definitionFlagsHi);
  if(len>0)used+=(unsigned)len;
 }
 CtWrite(ctTrackFile,used);
 unsigned effectN=camera_module::DrainTrace(ctEffects,512);used=0;
 for(unsigned i=0;i<effectN;i++){
  auto& r=ctEffects[i];auto& v=r.snapshot;
  int len=sprintf_s(ctText+used,sizeof(ctText)-used,"%.3f,%lu,%lu,%08lx,%lu,%08lx,%08lx,%08lx,%lu,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\r\n",
   1000.0*(r.qpc-ctStart)/frequency,r.thread,r.outcome,r.binding,r.index,v.id.runtime,v.id.definition,v.id.reference,v.id.generation,(unsigned)v.flags,v.time,v.previous,v.step,v.duration[0],v.duration[1],v.duration[2],r.oldValue,r.delta);
  if(len>0)used+=(unsigned)len;
 }
 CtWrite(ctEffectFile,used);

 static unsigned summary=0;
 if((++summary%5)==0||lost||trackLost){char b[160];sprintf_s(b,
  "CAMERA 0.3.2 2s: post-FX poses %u, matched additive roll updates %lu, render drops %u, track drops %lu, mode %ld",
  n,tn,lost,trackLost,ctFix);Log(b);
  unsigned poisoned=0;for(unsigned k=0;k<16;k++)poisoned+=camera_module::shards[k].Poisoned();
  char summaryText[256];sprintf_s(summaryText,"CAMERA 0.3.2 module: applied=%ld admissionRejected=%ld poisonedShards=%u enabled=%ld",camera_module::applied,camera_module::admissionRejected,poisoned,camera_module::enabled);Log(summaryText);
  char traceStats[96];sprintf_s(traceStats,"CAMERA 0.3.2 trace: lost=%ld",camera_module::traceLost);Log(traceStats);
  char admission[512];sprintf_s(admission,"CAMERA 0.3.2 admission: beginOK=%ld beginFailed=%ld cameraOK=%ld cameraFailed=%ld retireOK=%ld retireFailed=%ld exactRoll=%ld targetRejected=%ld identityMissing=%ld identityMismatch=%ld policyRejected=%ld",camera_module::initBeginOK,camera_module::initBeginFailed,camera_module::initCameraOK,camera_module::initCameraFailed,camera_module::retireOK,camera_module::retireFailed,camera_module::exactRoll,camera_module::targetRejected,camera_module::identityMissing,camera_module::identityMismatch,camera_module::policyRejected);Log(admission);
  char corpseReads[192];sprintf_s(corpseReads,"CORPSE READ10: groupReadFailed=%ld bodyReadFailed=%ld bodyAccepted=%ld",corpse_read::groupReadFailed,corpse_read::bodyReadFailed,corpse_read::bodyAccepted);Log(corpseReads);
  char groupStats[160];sprintf_s(groupStats,"CORPSE GROUP10: acquired=%ld released=%ld invalid=%ld",corpse_groups::acquired,corpse_groups::released,corpse_groups::invalid);Log(groupStats);
 }
}

static __declspec(noinline) void __cdecl CtCapture(DWORD frame,DWORD args) {
 if(!ctArmed||!ctStart||!TryEnterCriticalSection(&ctLock))return;
 __try {
  if(ctN<512){
   CtRender& r=ctRing[ctN];LARGE_INTEGER q;QueryPerformanceCounter(&q);r.qpc=q.QuadPart;
   r.frame=(DWORD)InterlockedCompareExchange(&ctFrame,0,0);r.thread=GetCurrentThreadId();
   r.mode=(DWORD)InterlockedCompareExchange(&ctFix,0,0);
   DWORD context=*(DWORD*)0x2ABE4D0;r.fx=context?*(DWORD*)(context+0x28):0;
   r.singleton=*(DWORD*)0x2AC2170;r.enabled=r.fx?*(BYTE*)(r.fx+0x48):0;r.bypass=*(BYTE*)0x2AC07FF;
   r.mark=(GetAsyncKeyState(VK_PRIOR)&0x8000)?1:0;r.alpha=*(float*)(args+8);
   memcpy(r.base,(void*)(frame-0x70),16);memcpy(r.finalQ,(void*)(frame-0x40),16);
   if(r.fx)memcpy(r.angles,(void*)(r.fx+0x14),12);else memset(r.angles,0,12);
   r.angles[3]=r.angles[2];
   r.camera=*(DWORD*)(frame-0x14);r.cameraVtable=r.camera?*(DWORD*)r.camera:0;
   r.fov=*(float*)(frame-8);
   ++ctN;
  }else ++ctLost;
 } __except(EXCEPTION_EXECUTE_HANDLER) {}
 LeaveCriticalSection(&ctLock);
}
__declspec(naked) static void CtRenderHook() {
 __asm {
  pushfd
  pushad
  mov edx,esp
  sub esp,544
  and esp,0FFFFFFF0h
  fxsave [esp]
  mov dword ptr [esp+512],edx
  push ebx
  push ebp
  call CtCapture
  add esp,8
  fxrstor [esp]
  mov esp,dword ptr [esp+512]
  popad
  popfd
  lea ecx,[ebp-40h]
  push ecx
  lea edx,[ebp-50h]
  jmp dword ptr [ctRenderResume]
 }
}
static bool CameraTest_Queue() {
 if(!CfgIntW(L"Physics",L"CameraFix",1,configPath)){Log("CAMERA 0.3.2: disabled in settings.");return false;}
 ctDiagnostics=CfgIntW(L"Debug",L"Diagnostics",0,configPath)!=0;
 InterlockedExchange(&camera_module::traceEnabled,ctDiagnostics?1:0);
 if(!ctStartupLoad){Log("CAMERA 0.3.2: camera module refused late/dynamic loading.");return false;}
 if(count+(ctDiagnostics?9:8)>sizeof(changes)/sizeof(changes[0])){Log("CAMERA 0.3.2: queue capacity exceeded; camera unchanged.");return false;}
 static const BYTE s0[]={0x0f,0x57,0xc0,0x53,0x56,0x8b,0xf1};
 if(memcmp((void*)0x707c60,s0,sizeof s0)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s1[]={0x55,0x8b,0xec,0x51,0x8a,0x45,0x14};
 if(memcmp((void*)0x70d3e0,s1,sizeof s1)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s2[]={0x5e,0x5b,0x8b,0xe5,0x5d};
 if(memcmp((void*)0x70d6c9,s2,sizeof s2)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s3[]={0x5f,0x32,0xc0,0x5b,0x8b,0xe5};
 if(memcmp((void*)0x68ecce,s3,sizeof s3)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s4[]={0x55,0x8b,0xec,0x51,0x56};
 if(memcmp((void*)0x705590,s4,sizeof s4)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s5[]={0x55,0x8b,0xec,0x8b,0x55,0x08};
 if(memcmp((void*)0x70bd40,s5,sizeof s5)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s6[]={0x8b,0xf0,0x53,0x56,0x8b,0xcf};
 if(memcmp((void*)0x70d1af,s6,sizeof s6)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE s7[]={0xf3,0x0f,0x59,0x4b,0x0c};
 if(memcmp((void*)0x705a0d,s7,sizeof s7)){Log("CAMERA 0.3.2: original bytes mismatch; camera unchanged.");return false;}
 static const BYTE render[]={0x8d,0x4d,0xc0,0x51,0x8d,0x55,0xb0};
 if(ctDiagnostics&&memcmp((void*)0x413e24,render,sizeof render)){Log("CAMERA 0.3.2: render signature mismatch; camera unchanged.");return false;}
 DWORD returns[8]={0x707c67,0x70d3e7,0x70d6ce,0x68ecd4,0x705595,0x70bd46,0x70d1b5,0x705a12};
 if(!camera_module::ConfigureBeforePublication(returns,0x2ac2170))return false;
 unsigned before=count;bool ok=true;
 ok=Jump(0x707c60,s0,sizeof s0,(void*)camera_module::Observer0)&&ok;
 ok=Jump(0x70d3e0,s1,sizeof s1,(void*)camera_module::Observer1)&&ok;
 ok=Jump(0x70d6c9,s2,sizeof s2,(void*)camera_module::Observer2)&&ok;
 ok=Jump(0x68ecce,s3,sizeof s3,(void*)camera_module::Observer3)&&ok;
 ok=Jump(0x705590,s4,sizeof s4,(void*)camera_module::Observer4)&&ok;
 ok=Jump(0x70bd40,s5,sizeof s5,(void*)camera_module::Observer5)&&ok;
 ok=Jump(0x70d1af,s6,sizeof s6,(void*)camera_module::Observer6)&&ok;
 ok=Jump(0x705a0d,s7,sizeof s7,(void*)camera_module::Additive)&&ok;
 if(ctDiagnostics)ok=Jump(0x413e24,render,sizeof render,CtRenderHook)&&ok;
 if(!ok){count=before;camera_module::Seal();Log("CAMERA 0.3.2: camera queue rejected; module sealed.");return false;}
 InitializeCriticalSection(&ctLock);camera_draft::CameraLeanDraft_Init();ctQueued=true;return true;
}
static void CameraTest_Applied() {
 if(!ctQueued)return;
 camera_draft::CameraLeanDraft_SetMode(0);
 bool on=camera_module::SetEnabled(true);
 InterlockedExchange(&ctFix,on?1:0);InterlockedExchange(&ctArmed,1);
 Log(on?"CAMERA 0.3.2: scoped retired-roll fix ON; clocks unchanged.":"CAMERA 0.3.2: activation refused; original contributions retained.");
}
static void CameraTest_Frame() {
 if(!ctArmed||!frequency||!ctDiagnostics)return;
 LARGE_INTEGER q;QueryPerformanceCounter(&q);
 if(!ctStart){
  ctStart=q.QuadPart;
  ctRenderFile=CtOpen(L"render","ms,frame,thread,mode,fx,singleton,fx_enabled,bypass,pageup,alpha,bqx,bqy,bqz,bqw,qx,qy,qz,qw,fx14,fx18,fx1c,roll_input,camera,camera_vtable,fov\r\n");
  ctEffectFile=CtOpen(L"effects","ms,thread,outcome,binding,index,runtime,definition,reference,generation,flags,time,previous,step,duration0,duration1,duration2,old,delta\r\n");
  ctTrackFile=CtOpen(L"tracks","frame,thread,mode,binding,index,owner,proxy,property,curve,old,delta,scale,applied,lifecycle_valid,group,sequence,sequence_def,clip_def,track_def,source,group_flags,sequence_flags,definition_flags,track_flags,time,previous_time,step,speed,clip_start,clip_end,duration0,duration1,duration2,weight,runtime_vtable,clip_vtable,ref8_owner,refc_owner,definition_flags_hi\r\n");
 }
 float dt=ctLast?(float)((double)(q.QuadPart-ctLast)/frequency):0;
 ctLast=q.QuadPart;camera_draft::CameraLeanDraft_Frame(dt);InterlockedIncrement(&ctFrame);
 DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
 bool f4=pid==gamePID&&(GetAsyncKeyState(VK_F4)&0x8000)!=0;
 if(f4&&!ctF4){LONG next=ctFix==0?1:0;if(!camera_module::SetEnabled(next!=0)){Log("CAMERA 0.3.2: enable refused; keeping filter off.");next=0;}InterlockedExchange(&ctFix,next);
  Log(next?"CAMERA 0.3.2 F4: retired CameraFX.roll suppression ON":"CAMERA 0.3.2 F4: original CameraFX contributions (observation)");}
 ctF4=f4;
 DWORD now=GetTickCount();if(now-ctLastFlush>=2000){ctLastFlush=now;CtFlush();}
}

