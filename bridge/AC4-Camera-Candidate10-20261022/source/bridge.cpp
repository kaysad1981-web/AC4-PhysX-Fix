#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>
#include <math.h>
#include <stdlib.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <timeapi.h>
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"psapi.lib")
#include "sites.h"
#include "mode_filter.h"

static wchar_t configPath[MAX_PATH], logPath[MAX_PATH];static char configPathA[MAX_PATH];
// v50: AC4Fix\User.ini (optional, never written or removed by the installer) overrides any key of
// Settings.ini, so personal tweaks survive reinstalls.
static wchar_t userPath[MAX_PATH];
static UINT CfgIntW(const wchar_t* sec,const wchar_t* key,INT def,const wchar_t* path){
 UINT v=GetPrivateProfileIntW(sec,key,def,path);return userPath[0]?GetPrivateProfileIntW(sec,key,(INT)v,userPath):v;}
static DWORD cpuLoop,cpuExit,cpuFail,timerExit,modeExit,modeSkip,waitExit;
static int autoMargin=3,minRefresh=60,maxModes=24,minWidth=1920,minHeight=1080;
static volatile LONGLONG ticks=0;
static LONGLONG frequency=0;
static int configuredFPS=0, activeFPS=60, safeMode=0;
static void LabTick();static void LabMeasure(DWORD self,float dt);static void ClothPostProbe();static volatile LONG labOn=0,labCollect=0,labCond=0;
static DWORD lastMonitorCheck=0, gamePID=0;
static bool previousF10=false;
static void Log(const char* text) {
 HANDLE f=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
 if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,text,(DWORD)strlen(text),&n,0);WriteFile(f,"\r\n",2,&n,0);CloseHandle(f);}
}
static volatile DWORD mainThreadId=0,mainStackLo=0,mainStackHi=0;static volatile LONGLONG frameStartQpc=0;static volatile LONG frameIndex=0;
static volatile LONG diagEnabled=0,diagSpikes=0,diagFrames=0;static volatile LONG diagWorstUs=0;static LONGLONG diagLastQpc=0;
static void PollDiagKeys();static volatile LONG lastFrameUs=0;
static void CameraTest_Frame();
struct FreshPart{DWORD rb;LONGLONG t;};static FreshPart freshTab[512];static volatile LONG freshI=0;static volatile LONG keepPhys=0;
static DWORD keptGroups[32];   // v46: collision groups (bits 16-25 of filter info) of corpses that lie still (kept table)
static volatile DWORD grpDeadMs[1024];   // v49: GetTickCount when a ragdoll of this collision group became dynamic (death / fall)
struct RejPair{volatile DWORD key,ta,tb;};static RejPair rejTab[256];static volatile LONG rejI=0,ccBorn=0;
static volatile LONG gfxOn=1;static volatile LONG lbMode;static volatile DWORD gfxObj=0;static void __stdcall GfxApply(DWORD o);
static volatile LONG gwOn=1;   // v51 cloth 30 FPS gateway
static volatile LONG dampFix=1;   // v54 cloth damping 30 FPS formula
static volatile LONG skinFix=1;   // v56 cloth pull-to-animation 30 FPS formula
static volatile LONG iterFix=1;   // v57 constraint iterations per second as at 30 FPS
static volatile LONG jkAuto=1;   // v59 projection softness from step length
static volatile LONG xpOn=0;   // v61 own XPBD thread solver for ClothLODInstance
static volatile LONG xpSoftOn=1;   // v67 the same for flags (SoftBodyLODInstance)
static float xpC=0.5f,xpD=4.0f,xpK=20.0f,xpKSoft=5.0f;static void XpSet(float c,float d);
static volatile LONG wdFrames=0;static bool windHookOk=false;static volatile LONG windMode=0;static void SetWindMode(int m);
static float lodScale=1.0f,lodScaleCfg=1.0f;static volatile DWORD lodCtx=0;static DWORD lodBack=0x869687;
static volatile LONG caOn=0,caSteps=0,caChanged=0,caBack=0,caSnaps=0;static volatile LONGLONG caPeriodUs=0;
struct CaInst{DWORD self;LONGLONG last,tB;float period;WORD n1,n2;float A[64][16],B[64][16];};
static CaInst* caTab=0;static CRITICAL_SECTION caLock;static const int CA_N=384;
// v52b: automatic test plan ([Debug] AutoTest=1, usually in User.ini): the frame limit alternates by
// itself between full FPS and 30 FPS every 40 s, with a beep (high = full FPS, low = 30 FPS) and a
// "AUTOTEST" line in the log, so the tester only has to walk the route.
static int autoTest=0,autoTestSec=20;static LONGLONG atStart=0;static int atPhase=-1;
static DWORD WINAPI AtBeep(LPVOID p){DWORD f=(DWORD)(DWORD_PTR)p;Beep(f,180);if(f<600){Sleep(120);Beep(f,180);}return 0;}
static DWORD WINAPI AtBeepN(LPVOID p){int n=(int)(DWORD_PTR)p;for(int i=0;i<n;i++){Beep(800,150);Sleep(150);}return 0;}
static void JkSet(float s);
static void AutoTestTick(){
 LARGE_INTEGER q;QueryPerformanceCounter(&q);if(!atStart)atStart=q.QuadPart;
 int ph=(int)((q.QuadPart-atStart)/(frequency*autoTestSec));if(ph==atPhase)return;atPhase=ph;
 {SYSTEMTIME st;GetLocalTime(&st);char tb[64];sprintf_s(tb,"AUTOTEST clock %02d:%02d:%02d.%03d",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);Log(tb);}
 if(autoTest==12){static const float fk[5]={0,0,2.0f,5.0f,10.0f};int m=ph%5;InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==0){InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);safeMode=2;}
  else{XpSet(0.5f,4.0f);xpK=20.0f;xpKSoft=fk[m];InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);InterlockedExchange(&xpSoftOn,1);safeMode=0;}
  char c[120];if(m==0)sprintf_s(c,"AUTOTEST phase %d: 30 FPS reference",ph);else sprintf_s(c,"AUTOTEST phase %d: full FPS, flags own solver, flag ripple damping %.0f /s",ph,fk[m]);Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==11){int m=ph%3;InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==0){InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);safeMode=2;}
  else{XpSet(0.5f,4.0f);xpK=20.0f;InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);InterlockedExchange(&xpSoftOn,m==2?1:0);safeMode=0;}
  char c[120];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"30 FPS reference":m==1?"full FPS, v66 (flags on game solver)":"full FPS, v67 (flags on own solver)");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==10){int m=ph%3;InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==1){XpSet(0.5f,4.0f);xpK=20.0f;InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);}
  else{InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);}
  safeMode=m==0?2:0;char c[120];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"30 FPS reference":m==1?"full FPS, own solver + ripple damping 20 (final)":"full FPS, game");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==9){static const float kv[5]={0,0,15.0f,40.0f,120.0f};int m=ph%5;
  InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==0){InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);safeMode=2;}
  else{XpSet(0.5f,4.0f);xpK=kv[m];InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);safeMode=0;}
  char c[120];if(m==0)sprintf_s(c,"AUTOTEST phase %d: 30 FPS reference",ph);else sprintf_s(c,"AUTOTEST phase %d: full FPS, own solver 0.5/4, ripple damping %.0f /s",ph,kv[m]);Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==8){int m=ph%3;InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);
  InterlockedExchange(&gwOn,m==1?3:0);safeMode=m==0?2:0;
  char c[120];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"30 FPS reference":m==1?"full FPS, fixed 1/30 s cloth + animated pose every frame (v64)":"full FPS, game");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==7){int m=ph%3;InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==2){XpSet(0.5f,4.0f);InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);}else{InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);}
  safeMode=m==0?2:0;char c[120];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"30 FPS reference":m==1?"full FPS, game":"full FPS, own solver 0.5/4");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==6){static const float cv[5]={0,0.5f,0.5f,1.5f,1.5f},dv[5]={0,1.0f,4.0f,1.0f,4.0f};int m=ph%5;
  InterlockedExchange(&gwOn,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  if(m==0){InterlockedExchange(&xpOn,0);InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);safeMode=2;}
  else{XpSet(cv[m],dv[m]);InterlockedExchange(&xpOn,1);InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);safeMode=0;}
  char c[120];if(m==0)sprintf_s(c,"AUTOTEST phase %d: 30 FPS reference (game solver)",ph);else sprintf_s(c,"AUTOTEST phase %d: full FPS, own XPBD solver softness %.2f damping %.1f",ph,cv[m],dv[m]);Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==5){int m=ph%3;InterlockedExchange(&dampFix,0);InterlockedExchange(&skinFix,0);InterlockedExchange(&iterFix,0);InterlockedExchange(&jkAuto,1);
  InterlockedExchange(&gwOn,m==1?1:0);safeMode=m==0?2:0;
  char c[110];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"30 FPS reference":m==1?"full FPS, cloth on physics tick + smooth (v60)":"full FPS, game");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==4){static const float sv[5]={1.0f,1.0f,0.4f,0.15f,0.05f};int m=ph%5;
  InterlockedExchange(&dampFix,1);InterlockedExchange(&skinFix,1);InterlockedExchange(&iterFix,1);safeMode=m==0?2:0;InterlockedExchange(&jkAuto,0);JkSet(m==0?1.0f:sv[m]);
  char c[110];sprintf_s(c,"AUTOTEST phase %d: %s, projection softness %.2f",ph,m==0?"30 FPS reference":"full FPS, cloth formulas",m==0?1.0f:sv[m]);Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==3){int m=ph%3;InterlockedExchange(&dampFix,m==1?1:0);InterlockedExchange(&skinFix,m==1?1:0);InterlockedExchange(&iterFix,m==1?1:0);InterlockedExchange(&jkAuto,1);safeMode=m==2?2:0;
  char c[110];sprintf_s(c,"AUTOTEST phase %d: %s",ph,m==0?"full FPS, cloth as game":m==1?"full FPS, cloth 30 FPS formulas":"30 FPS reference");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 if(autoTest==2){int m=ph%3;InterlockedExchange(&gwOn,m);safeMode=0;
  char c[96];sprintf_s(c,"AUTOTEST phase %d: cloth mode %d (%s)",ph,m,m==0?"game":m==1?"physics tick, smooth":"physics tick, latest");Log(c);
  HANDLE t=CreateThread(0,0,AtBeepN,(LPVOID)(DWORD_PTR)(m+1),0,0);if(t)CloseHandle(t);return;}
 safeMode=(ph&1)?2:0;
 char b[96];sprintf_s(b,"AUTOTEST phase %d: %s",ph,(ph&1)?"30 FPS":"full FPS");Log(b);
 HANDLE t=CreateThread(0,0,AtBeep,(LPVOID)(DWORD_PTR)((ph&1)?400:1000),0,0);if(t)CloseHandle(t);
}
// ---- CAMPROBE (0.3, diagnostics only): per-frame camera state -> AC4Fix\camera.csv -------------------------------
// The game keeps the final camera of the frame in globals (Steam 2023): position 0x2abe530, rotation quaternion
// 0x2abe540 (written by 0x40be50 under the lock 0x2abe550). One line per frame with the frame time and the F10 state;
// PageUp writes a mark so "the camera is swinging now" can be found in the data. Buffered, flushed every 2 s.
static char camBuf[65536];static int camLen=0;static DWORD camFlushTick=0;static LONGLONG camT0=0;static LONG camLines=0;static bool camPrevMark=false;
static wchar_t camPath[MAX_PATH]={0};static void MenuProbe();static volatile LONG camMouseX=0,camMouseY=0,diMouseStatesPub=0,diMouseEventsPub=0;
// gamepad sticks through the game's own XInput 1.3 (read-only state query)
struct XPad{DWORD pkt;WORD buttons;BYTE lt,rt;SHORT lx,ly,rx,ry;};typedef DWORD (WINAPI *XGetFn)(DWORD,XPad*);static XGetFn xGet=0;static bool xTried=false;
static void CamFlush(){if(!camLen||!camPath[0])return;HANDLE f=CreateFileW(camPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
 if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,camBuf,camLen,&n,0);CloseHandle(f);}camLen=0;}
static void CamReadFx(float* out){__try{DWORD o=*(volatile DWORD*)0x2ac2170;if(o)memcpy(out,(void*)(o+8),6*sizeof(float));}__except(EXCEPTION_EXECUTE_HANDLER){}}
static void CamProbe(LONGLONG qpc,LONG us){
 if(!camPath[0]||camLines>2000000)return;
 if(!camT0){camT0=qpc;const char* h="ms,frame_us,f10,mark,px,py,pz,qx,qy,qz,qw,mx,my,keys,lx,ly,rx,ry,fx08,fx0c,fx10,fx14,fx18,fx1c\r\n";memcpy(camBuf,h,strlen(h));camLen=(int)strlen(h);}
 // CameraFXInterface singleton (factory 0x67a260 stores it at 0x2ac2170): +8..+0x1c are the FX offsets/angles; the
 // sprint lean is an additive sequence track on one of them (AC Rogue, PatchFix camera_lean.cpp).
 float fx[6]={0,0,0,0,0,0};CamReadFx(fx);
 if(!xTried){xTried=true;HMODULE xm=GetModuleHandleW(L"xinput1_3.dll");if(xm)xGet=(XGetFn)GetProcAddress(xm,"XInputGetState");}
 XPad pad={};if(!xGet||xGet(0,&pad)!=0)memset(&pad,0,sizeof pad);
 // keys: W1 A2 S4 D8, E16 (pick up a body / interact), Shift32, Space64, left mouse 128, right mouse 256
 int keys=((GetAsyncKeyState('W')&0x8000)?1:0)|((GetAsyncKeyState('A')&0x8000)?2:0)|((GetAsyncKeyState('S')&0x8000)?4:0)|((GetAsyncKeyState('D')&0x8000)?8:0)
  |((GetAsyncKeyState('E')&0x8000)?16:0)|((GetAsyncKeyState(VK_SHIFT)&0x8000)?32:0)|((GetAsyncKeyState(VK_SPACE)&0x8000)?64:0)
  |((GetAsyncKeyState(VK_LBUTTON)&0x8000)?128:0)|((GetAsyncKeyState(VK_RBUTTON)&0x8000)?256:0);
 LONG mx=InterlockedExchange(&camMouseX,0),my=InterlockedExchange(&camMouseY,0);
 bool k=(GetAsyncKeyState(VK_PRIOR)&0x8000)!=0;int mark=k&&!camPrevMark;camPrevMark=k;
 if(mark){SYSTEMTIME st;GetLocalTime(&st);char b[96];sprintf_s(b,"CAMMARK PageUp at %02d:%02d:%02d.%03d",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);Log(b);}
 const float* p=(const float*)0x2abe530;const float* q=(const float*)0x2abe540;
 if(camLen>(int)sizeof camBuf-256)CamFlush();
 int n=sprintf_s(camBuf+camLen,sizeof camBuf-camLen,"%.3f,%ld,%d,%d,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%ld,%ld,%d,%d,%d,%d,%d,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\r\n",(double)(qpc-camT0)*1000.0/frequency,us,safeMode,mark,p[0],p[1],p[2],q[0],q[1],q[2],q[3],mx,my,keys,pad.lx,pad.ly,pad.rx,pad.ry,fx[0],fx[1],fx[2],fx[3],fx[4],fx[5]);
 if(n>0){camLen+=n;camLines++;}
 DWORD t=GetTickCount();if(t-camFlushTick>2000){camFlushTick=t;CamFlush();MenuProbe();
  static int inLines=0;LONG s=InterlockedExchange(&diMouseStatesPub,0),e=InterlockedExchange(&diMouseEventsPub,0);
  if(inLines<40){inLines++;char b[96];sprintf_s(b,"CAMINPUT 2s: mouse state reads %ld, buffered events %ld",s,e);Log(b);}}
}
// ---- MENUPROBE (0.3, diagnostics only): what decides whether "PhysX particles" is offered in the options --------
// The options screen (0x1388600) adds the APEXPARTICLES item only if 0x82b910 returns true: graphic settings
// (0x2ace6b0, built on first use, guard bit 0x2acecf0) +0xb0 and +0x23 set, and the renderer object [0x4dcf0f8]
// +0x700 (APEX effects object made by 0x85cc50) not null. Logged whenever one of them changes.
// [Debug] FakeGpuVendor=1 makes the game's "is NVIDIA" (0x965850) answer no and "is AMD" (0x965840) yes, so an
// NVIDIA PC shows what an AMD PC would get from the game's own vendor checks.
static void MenuProbe(){
 static int last=-2;int a=-1,b=-1,c=-1;
 if(*(volatile DWORD*)0x2acecf0&1){a=*(volatile BYTE*)(0x2ace6b0+0xb0);b=*(volatile BYTE*)(0x2ace6b0+0x23);}
 DWORD r=*(volatile DWORD*)0x4dcf0f8;if(r)c=*(volatile DWORD*)(r+0x700)!=0;
 int k=(a&0xff)|((b&0xff)<<8)|((c&0xff)<<16);if(k==last)return;last=k;
 char t[160];sprintf_s(t,"MENUPROBE: PhysX menu item %s - settings+0xb0=%d settings+0x23=%d apexObject=%d",(a>0&&b>0&&c>0)?"OFFERED":"hidden",a,b,c);Log(t);}
static void FakeVendor(){
 static const BYTE isNv[]={0x81,0xb9,0xcc,0x04,0,0,0xde,0x10,0,0},isAmd[]={0x81,0xb9,0xcc,0x04,0,0,0x02,0x10,0,0};
 if(memcmp((void*)0x965850,isNv,sizeof isNv)||memcmp((void*)0x965840,isAmd,sizeof isAmd)){Log("FAKEVENDOR: vendor checks not recognised; unchanged.");return;}
 static const BYTE no[]={0x32,0xc0,0xc3},yes[]={0xb0,0x01,0xc3};   // xor al,al / mov al,1 ; ret
 DWORD old;if(VirtualProtect((void*)0x965840,0x20,PAGE_EXECUTE_READWRITE,&old)){memcpy((void*)0x965850,no,3);memcpy((void*)0x965840,yes,3);VirtualProtect((void*)0x965840,0x20,old,&old);FlushInstructionCache(GetCurrentProcess(),(void*)0x965840,0x20);
  Log("FAKEVENDOR: game vendor checks answer AMD (test only, [Debug] FakeGpuVendor=1).");}}
static void __cdecl UpdateLimit() {
 InterlockedIncrement(&wdFrames);
 CameraTest_Frame();
 if(diagEnabled){LARGE_INTEGER q;QueryPerformanceCounter(&q);
  if(diagLastQpc&&frequency){LONG us=(LONG)((q.QuadPart-diagLastQpc)*1000000/frequency);diagFrames++;lastFrameUs=us;if(us>25000)diagSpikes++;if(us>diagWorstUs)diagWorstUs=us;
   // 0.3: a frame over 50 ms gets a line with the wall-clock time (diagnostics only, at most 300 per session)
   if(us>50000){static LONG hitches=0;if(hitches++<300){SYSTEMTIME st;GetLocalTime(&st);char hb[96];sprintf_s(hb,"HITCH %ld ms frame at %02d:%02d:%02d.%03d",us/1000,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);Log(hb);}}}
  if(diagLastQpc)CamProbe(q.QuadPart,(LONG)((q.QuadPart-diagLastQpc)*1000000/frequency));
  diagLastQpc=q.QuadPart;
  if(!mainThreadId){mainThreadId=GetCurrentThreadId();NT_TIB* tib=(NT_TIB*)NtCurrentTeb();mainStackLo=(DWORD)tib->StackLimit;mainStackHi=(DWORD)tib->StackBase;
   char b[64];sprintf_s(b,"DIAG: frame (main) thread id %lu",mainThreadId);Log(b);}
  frameStartQpc=q.QuadPart;InterlockedIncrement(&frameIndex);}
 HWND w=GetForegroundWindow();DWORD pid=0;if(w)GetWindowThreadProcessId(w,&pid);
 if(pid==gamePID){
  bool key=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
  if(key&&!previousF10){safeMode=safeMode?0:1;if(diagEnabled)Log(safeMode?"KEY F10: 60 FPS":"KEY F10: full FPS");}   // 0.3: our limit <-> 60 FPS (no 30)
  previousF10=key;
  if(diagEnabled)PollDiagKeys();
  if(autoTest)AutoTestTick();
  if(labOn)LabTick();
  DWORD now=GetTickCount();
  if(!configuredFPS && (now-lastMonitorCheck>2000 || !lastMonitorCheck)){
   MONITORINFOEXW mi={};mi.cbSize=sizeof(mi);
   if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&mi)){
    DEVMODEW dm={};dm.dmSize=sizeof(dm);
    if(EnumDisplaySettingsW(mi.szDevice,ENUM_CURRENT_SETTINGS,&dm) && dm.dmDisplayFrequency>=30 && dm.dmDisplayFrequency<=1000){int target=(int)dm.dmDisplayFrequency-autoMargin;activeFPS=target<30?30:target;}
   }
   lastMonitorCheck=now;
  }
 }else previousF10=false;
 ticks=frequency/(safeMode==1?60:safeMode==2?30:activeFPS);
}
__declspec(naked) static void TimerHook(){__asm {
 adc ebx,ecx
 pushfd
 pushad
 mov ebx,esp
 sub esp,528
 and esp,-16
 fxsave [esp]
 call UpdateLimit
 fxrstor [esp]
 mov esp,ebx
 popad
 popfd
 mov eax,dword ptr [ticks]
 mov edx,dword ptr [ticks+4]
 jmp dword ptr [timerExit]
}}
static void __cdecl WaitFrame(void* ptr){
 LONGLONG deadline=*(LONGLONG*)((BYTE*)ptr+0x70);LARGE_INTEGER now;QueryPerformanceCounter(&now);
 LONGLONG remaining=deadline-now.QuadPart;
 if(remaining>frequency/500)Sleep(1);else if(remaining>0)SwitchToThread();
}
__declspec(naked) static void WaitHook(){__asm {
 pushfd
 pushad
 mov ebx,esp
 sub esp,528
 and esp,-16
 fxsave [esp]
 push esi
 call WaitFrame
 add esp,4
 fxrstor [esp]
 mov esp,ebx
 popad
 popfd
 jmp dword ptr [waitExit]
}}
__declspec(naked) static void CpuHook(){__asm {
 cmp ecx,32
 jb again
 cmp dword ptr [ebp-8],0
 jne done
 push dword ptr [ebp-14h]
 push edi
 call SetThreadAffinityMask
 push 0
 call Sleep
 jmp dword ptr [cpuFail]
again:
 test esi,esi
 jne loopagain
done:
 jmp dword ptr [cpuExit]
loopagain:
 jmp dword ptr [cpuLoop]
}}
static const ModeEntry* selBase=0;static DWORD selTotal=0,selLast=0xffffffff;
static int __cdecl AcceptMode(const ModeEntry* modes,DWORD index,DWORD count){
 if(modes!=selBase || count!=selTotal || selLast==0xffffffff || index<=selLast){
  unsigned n=SelectModes(modes,count,minRefresh,maxModes,(uint32_t)minWidth,(uint32_t)minHeight);
  selBase=modes;selTotal=count;
  char b[96];sprintf_s(b,"Display modes: %lu reported by driver, %u listed in menu.",(unsigned long)count,n);Log(b);
 }
 selLast=index;
 return index<kModeCap && modeSelected[index];
}
// Runs for every mode the game itself accepts (>=800x600, supported). Selected modes become
// their own menu entry; the rest are skipped so the list stays short.
__declspec(naked) static void ModesHook(){__asm {
 pushfd
 pushad
 mov ebx,esp
 sub esp,528
 and esp,-16
 fxsave [esp]
 push dword ptr [ebp-8h]
 push dword ptr [ebp-0ch]
 push esi
 call AcceptMode
 add esp,12
 mov dword ptr [ebx+28],eax
 fxrstor [esp]
 mov esp,ebx
 popad
 popfd
 test eax,eax
 jz skip
 mov eax,dword ptr [ebp-0ch]
 mov dword ptr [ebp-10h],eax
 inc dword ptr [ebp-0ch]
 jmp dword ptr [modeExit]
skip:
 jmp dword ptr [modeSkip]
}}

// ---- PhysX particles: simulate at most N times per second -------------------------------
// ApexParticles_x86.dll accumulates frame time and runs the APEX scene once the accumulator
// reaches a minimum step (the game passes ~0, so it runs every frame). Its turbulence solver
// always advances a fixed 1/60 s per run, so at 141 FPS smoke moved ~2.4x too fast and cost
// ~2.4x the GPU/bridge work. We raise the minimum step to 1/SimRate (default 60 Hz).
static float physxMinStep=1.0f/60;static DWORD apexBack=0;
static void __stdcall OnApexStep(DWORD obj);
__declspec(naked) static void ApexStepHook(){__asm {
 pushad
 push esi
 call OnApexStep
 popad
 fld dword ptr [esi+18h]
 fld dword ptr [physxMinStep]
 fcom st(1)
 fnstsw ax
 test ah,1
 jnz useorig
 fstp st(1)
 jmp docmp
useorig:
 fstp st(0)
docmp:
 fcomp dword ptr [esi+7Ch]
 fnstsw ax
 jmp dword ptr [apexBack]
}}
static volatile LONG apexPatched=0;
static bool PatchApex(HMODULE m){
 if(InterlockedExchange(&apexPatched,1))return true;
 static const BYTE pat[]={0xd9,0x46,0x18,0xd8,0x5e,0x7c,0xdf,0xe0,0xf6,0xc4,0x41,0x7a};
 IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)((BYTE*)m+((IMAGE_DOS_HEADER*)m)->e_lfanew);
 IMAGE_SECTION_HEADER* sec=IMAGE_FIRST_SECTION(nt);BYTE* hit=0;int hits=0;
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)if(sec[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){
  BYTE* p=(BYTE*)m+sec[i].VirtualAddress;DWORD n=sec[i].Misc.VirtualSize;
  for(DWORD k=0;k+sizeof(pat)<=n;k++)if(p[k]==0xd9&&!memcmp(p+k,pat,sizeof(pat))){hit=p+k;hits++;}
 }
 if(hits!=1){Log("PhysX: ApexParticles_x86.dll not recognised; particle rate unchanged.");return false;}
 apexBack=(DWORD)hit+8;
 BYTE code[8];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)ApexStepHook-(DWORD)hit-5;code[5]=code[6]=code[7]=0x90;
 DWORD old;if(!VirtualProtect(hit,8,PAGE_EXECUTE_READWRITE,&old))return false;
 LONGLONG expected;memcpy(&expected,hit,8);LONGLONG wanted;memcpy(&wanted,code,8);
 bool ok=InterlockedCompareExchange64((volatile LONGLONG*)hit,wanted,expected)==expected;
 FlushInstructionCache(GetCurrentProcess(),hit,8);DWORD ignored;VirtualProtect(hit,8,old,&ignored);
 char b[96];sprintf_s(b,"PhysX: particle simulation limited to %d Hz.",(int)(1.0f/physxMinStep+0.5f));Log(ok?b:"PhysX: patch failed.");
 return ok;
}
static void PatchSpriteWrite(HMODULE m);
static DWORD WINAPI ApexWatcher(LPVOID){
 for(int i=0;i<30000;i++){  // up to ~10 minutes
  HMODULE m=GetModuleHandleW(L"ApexParticles_x86.dll");
  if(m){PatchApex(m);PatchSpriteWrite(m);return 0;}
  Sleep(20);
 }
 return 0;
}

// ---- PhysX turbulence around the player: don't block the frame on a running simulation --
// ApexParticles runs the APEX scene asynchronously: it starts simulate() and on later frames
// polls until the results are in (flag at +0x78 while a simulation is running). Every frame the
// game moves the "acbf_character_grid" turbulence actor that follows the player (0x855160:
// setPose and two parameter calls). Touching an APEX actor while the scene simulates blocks
// until the simulation is done, so in crowds - where the turbulence grid has many character
// shapes to collide with - the main thread sat in that call for 20-40 ms. With BusySkip on we
// leave the grid where it is for that frame and move it on the next free frame instead; the
// smoke itself only advances at SimRate (60 Hz) anyway.
static volatile DWORD apexObj=0,apexThread=0;static int busySkip=1;static bool previousF9=false;
static DWORD turbBack=0x855629,turbSkip=0x8556f9,listBack=0x85761d;
struct ApexStats{double dtSum;float dtMin,dtMax;LONG dtClamped;LONG turbCalls,turbSlow,skipped,listCalls,launches,latN,stepCalls;LONGLONG turbUs,turbMaxUs,listUs,listMaxUs,listFrameMaxUs,latUs,latMaxUs;};
static ApexStats apexStats;static CRITICAL_SECTION apexLock;static bool apexLockReady=false;
static LONGLONG apexLaunchQpc=0,listFrame=0,listFrameUs=0;
// v50: smoke/fire particles are written to their D3D11 vertex buffer (0x864100: staging fill, Map
// WRITE_DISCARD at 0x8642be, copy) by every actor in the list +0x738 on EVERY rendered frame, although
// the particles only change when the APEX scene runs (SimRate, <= 60/s). At 141 FPS more than half of
// these writes repeat the same data (in fights near fires: ~2 ms of main-thread time per frame).
// SmokeReuse: an actor's update is skipped when no APEX simulation was started since its last update;
// the GPU keeps drawing the buffer written last time. F6 (Diagnostics) toggles it.
static volatile LONG apexSeq=0,lsSkipped=0,lsDone=0;static volatile LONG smokeReuse=1;
struct LsEnt{DWORD self;LONG seq;};static LsEnt lsTab[256];
static LONGLONG Us(LONGLONG d){return frequency?d*1000000/frequency:0;}
static void __stdcall OnApexStep(DWORD obj){
 apexObj=obj;apexThread=GetCurrentThreadId();
 {float a0=*(float*)(obj+0x7c),m0=*(float*)(obj+0x18);if(physxMinStep>m0)m0=physxMinStep;if(a0>=m0)InterlockedIncrement(&apexSeq);}
 if(!diagEnabled||!apexLockReady)return;
 LARGE_INTEGER now;QueryPerformanceCounter(&now);
 float acc=*(float*)(obj+0x7c),mn=*(float*)(obj+0x18);if(physxMinStep>mn)mn=physxMinStep;
 EnterCriticalSection(&apexLock);apexStats.stepCalls++;
 if(apexLaunchQpc){LONGLONG l=Us(now.QuadPart-apexLaunchQpc);apexStats.latUs+=l;apexStats.latN++;if(l>apexStats.latMaxUs)apexStats.latMaxUs=l;apexLaunchQpc=0;}
 if(acc>=mn){apexLaunchQpc=now.QuadPart;
  // Observe the wrapper's existing upper clamp at +0x1c; do not change the clock.
  float ceiling=*(float*)(obj+0x1c);float dt=acc;
  if(dt>ceiling){dt=ceiling;apexStats.dtClamped++;}
  if(!apexStats.launches||dt<apexStats.dtMin)apexStats.dtMin=dt;
  if(!apexStats.launches||dt>apexStats.dtMax)apexStats.dtMax=dt;
  apexStats.dtSum+=dt;apexStats.launches++;
 }
 LeaveCriticalSection(&apexLock);
}
static void CallThis1(DWORD fn,DWORD self,DWORD arg){__asm{mov ecx,self
 push arg
 call fn}}
static float smokeFadeNear=6,smokeFadeFar=15,smokeFadeInv=1.0f/9,smokeActivate=20,smokeKeepFire=20,smokeKeepShots=30,smokeKeepOther=25;static bool smokeCustom=false,smokeGame=false,previousF8=false;
static float smokeMine[6];
static void PollF9(){
 if(!diagEnabled)return; // test keys only with Diagnostics=1
 // F8: switch the PhysX smoke distances between ours and the game's, live (for comparison).
 bool k8=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
 if(k8&&!previousF8&&smokeCustom){
  smokeGame=!smokeGame;
  static float keepOtherMine=25;
  if(smokeGame){keepOtherMine=smokeKeepOther;smokeFadeNear=6;smokeFadeFar=15;smokeFadeInv=1.0f/9;smokeActivate=20;smokeKeepFire=20;smokeKeepOther=20;smokeKeepShots=30;}
  else{smokeFadeNear=smokeMine[0];smokeFadeFar=smokeMine[1];smokeFadeInv=smokeMine[2];smokeActivate=smokeMine[3];smokeKeepFire=smokeMine[4];smokeKeepOther=keepOtherMine;smokeKeepShots=smokeMine[5];}
  Log(smokeGame?"PhysX: smoke distances = game (F8)":"PhysX: smoke distances = patch (F8)");
 }
 previousF8=k8;
 bool key=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
 if(key&&!previousF9){busySkip=!busySkip;Log(busySkip?"PhysX: BusySkip ON (F9)":"PhysX: BusySkip OFF (F9)");}
 previousF9=key;
}
static int __stdcall TurbCall(DWORD actor,DWORD fn,DWORD arg){
 PollF9();
 static LONGLONG lastMove=0;LARGE_INTEGER t;QueryPerformanceCounter(&t);
 // Never leave the grid behind for more than 100 ms, even if the scene is always busy.
 if(busySkip&&apexObj&&*(volatile BYTE*)(apexObj+0x78)&&Us(t.QuadPart-lastMove)<100000){
  if(diagEnabled&&apexLockReady){EnterCriticalSection(&apexLock);apexStats.skipped++;LeaveCriticalSection(&apexLock);}
  return 1;
 }
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);CallThis1(fn,actor,arg);QueryPerformanceCounter(&b);lastMove=b.QuadPart;
 if(diagEnabled&&apexLockReady){LONGLONG u=Us(b.QuadPart-a.QuadPart);EnterCriticalSection(&apexLock);apexStats.turbCalls++;apexStats.turbUs+=u;if(u>apexStats.turbMaxUs)apexStats.turbMaxUs=u;if(u>5000)apexStats.turbSlow++;LeaveCriticalSection(&apexLock);}
 return 0;
}
__declspec(naked) static void TurbHook(){__asm{
 lea edx,[ebp-0fch]
 push edx
 push eax
 push ecx
 call TurbCall
 test eax,eax
 jnz skip
 jmp dword ptr [turbBack]
skip:
 jmp dword ptr [turbSkip]
}}
// The list at +0x738 holds objects that get their PhysX shapes built on the main thread
// (PhysX3Cooking, sometimes a shader compile). Timed here; no behaviour change yet.
static volatile LONG inList=0;
static void __stdcall ListCall(DWORD self,DWORD fn,DWORD arg){
 if(smokeReuse&&apexPatched){
  UINT h=((self>>4)*2654435761u)>>24;LsEnt* e=0;
  for(int i=0;i<4;i++){LsEnt& c=lsTab[(h+i)&255];if(c.self==self){e=&c;break;}if(!e&&!c.self)e=&c;}
  if(!e)e=&lsTab[h];
  if(e->self==self&&e->seq==apexSeq){lsSkipped++;return;}
  e->self=self;e->seq=apexSeq;lsDone++;
 }
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);inList=1;CallThis1(fn,self,arg);inList=0;QueryPerformanceCounter(&b);
 if(diagEnabled&&apexLockReady){LONGLONG u=Us(b.QuadPart-a.QuadPart);EnterCriticalSection(&apexLock);apexStats.listCalls++;apexStats.listUs+=u;if(u>apexStats.listMaxUs)apexStats.listMaxUs=u;
  if(listFrame!=frameStartQpc){listFrame=frameStartQpc;listFrameUs=0;}listFrameUs+=u;if(listFrameUs>apexStats.listFrameMaxUs)apexStats.listFrameMaxUs=listFrameUs;LeaveCriticalSection(&apexLock);}
}
__declspec(naked) static void ListHook(){__asm{
 push ebx
 mov edx,[ecx]
 mov eax,[edx+54h]
 push eax
 push ecx
 call ListCall
 jmp dword ptr [listBack]
}}
// ---- Smoke particles to the GPU: one clear instead of one per vertex ---------------------
// The game writes every smoke particle as 4 vertices of 64 bytes into a staging buffer
// (0x864130), clearing each vertex with its own memset call before filling the fields the
// material uses, then copies the buffer into a D3D11 vertex buffer. With tens of thousands of
// particles that per-vertex memset was half of all the time the main thread spent on smoke.
// We clear the used part of the staging buffer once, then skip the per-vertex call.
static DWORD fillBack=0x8641a0;static void* (__cdecl *crtMemset)(void*,int,size_t)=memset;
// The staging buffer is a static 25.6 MB array sized for the game's 100 000-particle fire pool
// (256 bytes per particle). A bigger pool needs a bigger buffer, so both users of the array
// are pointed at our own allocation.
static DWORD stagingBase=0x2dced80,stagingFirst=0x2dced9c;
__declspec(naked) static void ParticleFillHook(){__asm{
 pushad
 shl eax,6
 push eax
 push 0
 push dword ptr [stagingBase]
 call dword ptr [crtMemset]
 add esp,12
 popad
 mov edi,dword ptr [stagingFirst]
 jmp dword ptr [fillBack]
}}

#define AC4_PARTICLE_METRICS
#include "particle_metrics.h"
#include "particle_pack_hook.h"
#include "particle_pack_signature.h"

// ---- Which smoke effects start, and how far away the camera is when they do -------------
struct FxStat{DWORD name;LONG n;float minD,maxD;};static FxStat fxStats[16];static int fxCount=0;static DWORD fxBack=0x850d9a;
static void __stdcall NoteFxCreate(DWORD name,DWORD distBits){
 if(!diagEnabled||!apexLockReady)return;float d=*(float*)&distBits;
 EnterCriticalSection(&apexLock);
 int i=0;for(;i<fxCount;i++)if(fxStats[i].name==name)break;
 if(i==fxCount&&fxCount<16){fxStats[i].name=name;fxStats[i].n=0;fxStats[i].minD=1e9f;fxStats[i].maxD=0;fxCount++;}
 if(i<fxCount){fxStats[i].n++;if(d<fxStats[i].minD)fxStats[i].minD=d;if(d>fxStats[i].maxD)fxStats[i].maxD=d;}
 LeaveCriticalSection(&apexLock);
}
__declspec(naked) static void FxCreateHook(){__asm{
 pushad
 push dword ptr [ebp-10h]
 push dword ptr [esi]
 call NoteFxCreate
 popad
 mov ecx,dword ptr [ecx+700h]
 jmp dword ptr [fxBack]
}}
static volatile LONG fxFailed=0,fxFailedFire=0;static DWORD fxAfterBack=0x850dac;
static void __stdcall NoteFxResult(DWORD name,DWORD actor){
 if(!actor){InterlockedIncrement(&fxFailed);if(name==0x1e7e3a4)InterlockedIncrement(&fxFailedFire);}
}
__declspec(naked) static void FxAfterHook(){__asm{
 mov dword ptr [esi+4],eax
 pushad
 push eax
 push dword ptr [esi]
 call NoteFxResult
 popad
 cmp eax,ebx
 jmp dword ptr [fxAfterBack]
}}
static void LogFxStats(){
 {LONG f=InterlockedExchange(&fxFailed,0),ff=InterlockedExchange(&fxFailedFire,0);if(f){char b[96];sprintf_s(b,"FX refused by APEX: %ld (campfires %ld)",f,ff);Log(b);}}

 if(!apexLockReady)return;
 EnterCriticalSection(&apexLock);FxStat a[16];int n=fxCount;memcpy(a,fxStats,sizeof(a));fxCount=0;LeaveCriticalSection(&apexLock);
 for(int i=0;i<n;i++){char b[160];const char* nm=(a[i].name>0x1e7e000&&a[i].name<0x1e7f000)?(const char*)a[i].name:"?";
  sprintf_s(b,"FX started: %s x%ld at %.1f..%.1f m",nm,a[i].n,a[i].minD,a[i].maxD);Log(b);}
}

static void LogApexStats(){
 if(!apexLockReady)return;
 EnterCriticalSection(&apexLock);ApexStats a=apexStats;memset(&apexStats,0,sizeof(apexStats));LeaveCriticalSection(&apexLock);
 {LONG d=InterlockedExchange(&lsDone,0),k=InterlockedExchange(&lsSkipped,0);if(d||k){char c[160];sprintf_s(c,"SMOKE 2s: reuse %s | particle buffer writes %ld, repeats skipped %ld",smokeReuse?"on":"off",d,k);Log(c);}}
 char b[400];sprintf_s(b,"APEX 2s: BusySkip %s | step calls %ld, sims %ld, sim done after avg %.1f max %.1f ms | grid moves %ld, avg %.2f max %.1f ms, >5ms %ld, skipped %ld | shape builds %ld, total %.1f ms, max %.1f ms, worst frame %.1f ms | step thread %s",
  busySkip?"on":"off",a.stepCalls,a.launches,a.latN?a.latUs/1000.0/a.latN:0,a.latMaxUs/1000.0,a.turbCalls,a.turbCalls?a.turbUs/1000.0/a.turbCalls:0,a.turbMaxUs/1000.0,a.turbSlow,a.skipped,
  a.listCalls,a.listUs/1000.0,a.listMaxUs/1000.0,a.listFrameMaxUs/1000.0,apexThread==mainThreadId?"main":"other");
 Log(b);
 char clock[240];sprintf_s(clock,"APEX CLOCK 2s: launches %ld | requested_dt sum %.6f s, min %.6f max %.6f s | upper_clamped %ld | mode %s | target_fps %d | wrapper observation before simulate",
  a.launches,a.dtSum,a.dtMin,a.dtMax,a.dtClamped,safeMode==1?"60":safeMode==2?"30":"full",safeMode==1?60:safeMode==2?30:activeFPS);Log(clock);
}

// ---- PhysX collision meshes: cook each distinct mesh only once ------------------------------
// ApexParticles builds PhysX triangle meshes for the collision proxies the game hands it (people,
// props near the smoke) on the main thread, through PxCooking::cookTriangleMesh. In crowds that
// was 150-200 ms of cooking per second with single builds up to 40 ms - the same meshes again and
// again. Cooking is deterministic, so we keep the cooked bytes per distinct input (vertices,
// indices, flags, cooking parameters) and replay them into the output stream on a repeat.
// PhysX3Cooking_x86.dll 3.3.0 as shipped with the game; anything else is left alone.
typedef bool(__fastcall *CookFn)(void* self,void* edx,const BYTE* desc,void* stream);
typedef UINT(__fastcall *WriteFn)(void* self,void* edx,const void* src,UINT count);
static CookFn realCook=0;static CRITICAL_SECTION cookLock;static bool cookReady=false;
struct CookEntry{unsigned long long key;UINT size;BYTE* data;};
static CookEntry cookTable[8192];static size_t cookBytes=0;static LONG cookEntries=0;
static volatile LONG cookHits=0,cookMisses=0,cookSkipped=0;static volatile LONGLONG cookMissUs=0,cookHitUs=0,cookMaxUs=0;
struct Grow{BYTE* p;UINT n,cap;};
struct ProxyStream{void** vtbl;void* real;Grow* buf;};
static UINT __fastcall ProxyWrite(ProxyStream* self,void*,const void* src,UINT count){
 Grow* g=self->buf;
 if(g->p&&g->n+count>g->cap){UINT c=g->cap*2;while(c<g->n+count)c*=2;BYTE* q=(BYTE*)HeapReAlloc(GetProcessHeap(),0,g->p,c);if(q){g->p=q;g->cap=c;}else{HeapFree(GetProcessHeap(),0,g->p);g->p=0;}}
 if(g->p){memcpy(g->p+g->n,src,count);g->n+=count;}
 WriteFn w=(WriteFn)(*(void***)self->real)[0];return w(self->real,0,src,count);
}
static void* __fastcall ProxyDtor(ProxyStream* self,void*,UINT){return self;}
static void* proxyVtbl[2]={(void*)ProxyWrite,(void*)ProxyDtor};
static inline void Mix(unsigned long long& h,const BYTE* p,UINT n){for(UINT i=0;i<n;i++){h^=p[i];h*=0x100000001b3ULL;}}
static bool CookKey(void* self,const BYTE* d,unsigned long long& h){
 UINT ps=*(UINT*)(d+0),pc=*(UINT*)(d+8),ts=*(UINT*)(d+0x0c),tc=*(UINT*)(d+0x14),ms=*(UINT*)(d+0x1c);
 const BYTE* pd=*(const BYTE**)(d+4);const BYTE* td=*(const BYTE**)(d+0x10);const BYTE* md=*(const BYTE**)(d+0x20);WORD fl=*(WORD*)(d+0x18);
 UINT te=(fl&2)?6:12;
 if(!pd||!td||ps<12||ts<te||pc>2000000||tc>4000000)return false;
 h=0xcbf29ce484222325ULL;Mix(h,(const BYTE*)self+4,32);Mix(h,d+0x18,2);Mix(h,d+0x24,4);Mix(h,(const BYTE*)&pc,4);Mix(h,(const BYTE*)&tc,4);
 for(UINT i=0;i<pc;i++)Mix(h,pd+i*ps,12);
 for(UINT i=0;i<tc;i++)Mix(h,td+i*ts,te);
 if(md){if(ms<2)return false;for(UINT i=0;i<tc;i++)Mix(h,md+i*ms,2);}
 if(!h)h=1;return true;
}
static bool __fastcall CookHook(void* self,void* edx,const BYTE* desc,void* stream){
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);unsigned long long key;
 if(!CookKey(self,desc,key)){InterlockedIncrement(&cookSkipped);return realCook(self,edx,desc,stream);}
 UINT slot=(UINT)(key%8192);BYTE* hit=0;UINT hitSize=0;
 EnterCriticalSection(&cookLock);
 for(UINT i=0;i<16;i++){CookEntry& e=cookTable[(slot+i)%8192];if(!e.key)break;if(e.key==key){hitSize=e.size;hit=(BYTE*)HeapAlloc(GetProcessHeap(),0,hitSize);if(hit)memcpy(hit,e.data,hitSize);break;}}
 LeaveCriticalSection(&cookLock);
 if(hit){
  WriteFn w=(WriteFn)(*(void***)stream)[0];w(stream,0,hit,hitSize);HeapFree(GetProcessHeap(),0,hit);
  QueryPerformanceCounter(&b);InterlockedIncrement(&cookHits);InterlockedExchangeAdd64(&cookHitUs,Us(b.QuadPart-a.QuadPart));return true;
 }
 Grow g={(BYTE*)HeapAlloc(GetProcessHeap(),0,65536),0,65536};ProxyStream ps={proxyVtbl,stream,&g};
 bool ok=realCook(self,edx,desc,&ps);
 QueryPerformanceCounter(&b);LONGLONG u=Us(b.QuadPart-a.QuadPart);
 InterlockedIncrement(&cookMisses);InterlockedExchangeAdd64(&cookMissUs,u);if(u>cookMaxUs)cookMaxUs=u;
 if(ok&&g.p&&g.n){
  EnterCriticalSection(&cookLock);
  if(cookBytes+g.n>(96u<<20)||cookEntries>6000){for(UINT i=0;i<8192;i++)if(cookTable[i].key){HeapFree(GetProcessHeap(),0,cookTable[i].data);cookTable[i].key=0;}cookBytes=0;cookEntries=0;}
  for(UINT i=0;i<16;i++){CookEntry& e=cookTable[(slot+i)%8192];if(!e.key){e.key=key;e.size=g.n;e.data=g.p;g.p=0;cookBytes+=e.size;cookEntries++;break;}}
  LeaveCriticalSection(&cookLock);
 }
 if(g.p)HeapFree(GetProcessHeap(),0,g.p);
 return ok;
}
static volatile LONG cookPatched=0;
static void PatchCooking(HMODULE m){
 if(!m||InterlockedExchange(&cookPatched,1))return;
 IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)((BYTE*)m+((IMAGE_DOS_HEADER*)m)->e_lfanew);
 DWORD base=(DWORD)m;void** vt=(void**)(base+0x48a4c);
 const DWORD rva[]={0x8690,0x8610,0x8650,0x8660,0x8a30};
 bool match=nt->OptionalHeader.SizeOfImage==0x61000;
 for(int i=0;match&&i<5;i++)if((DWORD)vt[i]!=base+rva[i])match=false;
 if(!match){Log("PhysX: cooking DLL not recognised; mesh cache off.");return;}
 InitializeCriticalSection(&cookLock);cookReady=true;realCook=(CookFn)vt[4];
 DWORD old;if(!VirtualProtect(&vt[4],4,PAGE_READWRITE,&old)){Log("PhysX: mesh cache could not be installed.");return;}
 InterlockedExchange((volatile LONG*)&vt[4],(LONG)CookHook);DWORD ig;VirtualProtect(&vt[4],4,old,&ig);
 Log("PhysX: collision mesh cache active.");
}
static DWORD WINAPI CookWatcher(LPVOID){
 for(int i=0;i<30000;i++){HMODULE m=GetModuleHandleW(L"PhysX3Cooking_x86.dll");if(m){PatchCooking(m);return 0;}Sleep(20);}
 return 0;
}
static void LogCookStats(){
 if(!cookReady)return;
 LONG h=InterlockedExchange(&cookHits,0),m=InterlockedExchange(&cookMisses,0),sk=InterlockedExchange(&cookSkipped,0);
 LONGLONG mu=InterlockedExchange64(&cookMissUs,0),hu=InterlockedExchange64(&cookHitUs,0),mx=InterlockedExchange64(&cookMaxUs,0);
 char b[256];sprintf_s(b,"COOK 2s: from cache %ld (%.1f ms), cooked %ld (%.1f ms, max %.1f ms), not cached %ld | cache %ld meshes, %.1f MB",
  h,hu/1000.0,m,mu/1000.0,mx/1000.0,sk,cookEntries,cookBytes/1048576.0);Log(b);
}

// ---- Hybrid CPUs (Intel 12th-14th gen, Core Ultra): keep the game on performance cores --
// The engine creates one worker per logical CPU and pins each one. On a 14900K that puts
// half of the frame jobs on E-cores, and every frame waits for the slowest of them. When the
// CPU has two core classes, we report only the performance cores to the game and restrict
// the process to them. CPUs with a single core class are left untouched.
static DWORD_PTR perfMask=0;static DWORD perfCount=0;
typedef void (WINAPI *GetSystemInfoFn)(LPSYSTEM_INFO);static GetSystemInfoFn realGetSystemInfo=0;
static void WINAPI HookGetSystemInfo(LPSYSTEM_INFO si){
 realGetSystemInfo(si);
 if(perfMask){si->dwNumberOfProcessors=perfCount;si->dwActiveProcessorMask=perfMask;}
}
static bool FindPerformanceCores(){
 typedef BOOL (WINAPI *Fn)(PSYSTEM_CPU_SET_INFORMATION,ULONG,PULONG,HANDLE,ULONG);
 Fn fn=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetSystemCpuSetInformation");
 if(!fn)return false;
 ULONG len=0;fn(0,0,&len,GetCurrentProcess(),0);if(!len||len>65536)return false;
 BYTE* buf=(BYTE*)HeapAlloc(GetProcessHeap(),0,len);if(!buf)return false;
 BYTE best=0,worst=255;DWORD_PTR mask=0;
 if(fn((PSYSTEM_CPU_SET_INFORMATION)buf,len,&len,GetCurrentProcess(),0)){
  for(int pass=0;pass<2;pass++)
   for(ULONG o=0;o<len;){
    SYSTEM_CPU_SET_INFORMATION* c=(SYSTEM_CPU_SET_INFORMATION*)(buf+o);if(!c->Size)break;
    if(c->Type==CpuSetInformation && c->CpuSet.Group==0 && c->CpuSet.LogicalProcessorIndex<32){
     BYTE e=c->CpuSet.EfficiencyClass;
     if(pass==0){if(e>best)best=e;if(e<worst)worst=e;}
     else if(e==best)mask|=(DWORD_PTR)1<<c->CpuSet.LogicalProcessorIndex;
    }
    o+=c->Size;
   }
 }
 HeapFree(GetProcessHeap(),0,buf);
 if(best==worst||!mask)return false;              // not a hybrid CPU
 DWORD n=0;for(DWORD_PTR m=mask;m;m>>=1)n+=(DWORD)(m&1);
 if(mask!=(((DWORD_PTR)1<<n)-1)){Log("CPU: performance cores are not numbered first; core layout unchanged.");return false;}
 DWORD_PTR proc=0,sys=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&proc,&sys)||(proc&mask)!=mask)return false;
 perfMask=mask;perfCount=n;return true;
}
static bool HookImportIn(HMODULE module,const char* dll,const char* name,void* hook,void** original){
 BYTE* image=(BYTE*)module;IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
 IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return false;
 for(IMAGE_IMPORT_DESCRIPTOR* d=(IMAGE_IMPORT_DESCRIPTOR*)(image+dir.VirtualAddress);d->Name;d++){
  if(_stricmp((char*)(image+d->Name),dll)||!d->OriginalFirstThunk)continue;
  IMAGE_THUNK_DATA* names=(IMAGE_THUNK_DATA*)(image+d->OriginalFirstThunk);IMAGE_THUNK_DATA* iat=(IMAGE_THUNK_DATA*)(image+d->FirstThunk);
  for(;names->u1.AddressOfData;names++,iat++){
   if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
   if(strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(image+names->u1.AddressOfData))->Name,name))continue;
   DWORD old;if(!VirtualProtect(&iat->u1.Function,sizeof(DWORD),PAGE_READWRITE,&old))return false;
   *original=(void*)iat->u1.Function;iat->u1.Function=(DWORD)hook;
   DWORD ignored;VirtualProtect(&iat->u1.Function,sizeof(DWORD),old,&ignored);return true;
  }
 }
 return false;
}
static bool HookImport(const char* dll,const char* name,void* hook,void** original){return HookImportIn(GetModuleHandleW(0),dll,name,hook,original);}
static void ApplyCoreLayout(){
 if(!FindPerformanceCores())return;
 if(!HookImport("KERNEL32.dll","GetSystemInfo",(void*)HookGetSystemInfo,(void**)&realGetSystemInfo)){perfMask=0;return;}
 SetProcessAffinityMask(GetCurrentProcess(),perfMask);
 char b[96];sprintf_s(b,"CPU: hybrid CPU, game limited to %lu performance-core threads.",(unsigned long)perfCount);Log(b);
}

static bool Queue(DWORD va,const BYTE* expected,const BYTE* replacement,unsigned size);
static bool Jump(DWORD va,const BYTE* expected,unsigned size,void* fn);
// ---- v58 calibration: softness of one thread projection at short steps ----------------------------------
// A thread projection (0x53af60, Jakobsen) removes the whole stretch of a thread at once. The wind stretches
// the cloth by F*dt^2 per step: 22x less at 7 ms than at 33 ms, while every projection still removes all of it,
// so at high FPS the sail cannot stay stretched (= filled) even with the same projections per second. Here the
// correction of normal projections (not the hard 1.5x stretch limit) is scaled by jkScale. AutoTest=4 sweeps it.
static __declspec(align(16)) float jkScale[4]={1,1,1,1};static volatile LONG jkCalls=0;static float jkCur=1.0f;
static DWORD jkBack=0x53b125;
__declspec(naked) static void JkHook(){__asm{
 mulps xmm5,xmmword ptr [jkScale]
 mulps xmm5,xmm2
 test edx,edx
 jmp dword ptr [jkBack]
}}
static bool InstallJk(){static const BYTE b[5]={0x0f,0x59,0xea,0x85,0xd2};return Jump(0x53b120,b,5,JkHook);}
static void JkSet(float s){jkCur=s;for(int i=0;i<4;i++)jkScale[i]=s;}
// v59: calibrated from the v58 sweep (sails, 141 FPS): softness 0.15 matched the 30 FPS reference (82-86 cm vs
// 77-104 cm), 0.4 too stiff, 0.05 too loose -> softness = (dt*30)^1.2, 1 at 30 FPS. Set per step by the step wrapper.
static float jkLastE=-1;
static void JkForStep(float e){if(!jkAuto)return;if(!(e>0.0001f&&e<4.0f))return;if(e==jkLastE)return;jkLastE=e;float s=e>=1.0f?1.0f:powf(e,1.2f);if(s<0.02f)s=0.02f;JkSet(s);}

// ---- PhysX smoke: denser campfires, visible from further away -------------------------------
// The turbulence manager (0x850b40) starts a PhysX smoke effect when the camera is within 20 m
// and scales it by distance: full strength up to 6 m, fading out to nothing at 15 m. That is the
// thin wisp that suddenly turns into thick smoke a few steps away. Those four distances are
// shared engine constants, so instead of changing them we point these loads at our own values.
// Campfire density: ApexParticles reads the "Fire" emitter (rate 5000-8000 particles/s) from
// turbulencemedia\EffectPackageEmittersDatabase.apb; we hand it a copy with a higher rate.
// The game's own files are never modified.
static int fireDensity=100,fireParticles=100000;static bool wantSimRate=false,wantMeshCache=false;
// All campfires share one particle pool (FireIOS, 100 000 particles). One fire alone uses
// ~30 000, so with several fires in range the pool runs dry: fires flicker on and off and
// each gets thinner. The pool size lives in EffectPackageParticleSimulationsDatabase.apb.
static bool PatchEmitters(BYTE* d,DWORD n){
 float* rate=(float*)(d+0x13ec);
 if(n!=20036||*(DWORD*)(d+0x13a8)!=0x24b||memcmp(d+0x24b,"Fire\0",5)||rate[0]!=5000.0f||rate[1]!=8000.0f)return false;
 rate[0]*=fireDensity/100.0f;rate[1]*=fireDensity/100.0f;
 char b[96];sprintf_s(b,"PhysX: campfire smoke density %d%%.",fireDensity);Log(b);return true;
}
static bool PatchSimulations(BYTE* d,DWORD n){
 if(n!=7552||*(DWORD*)(d+0xc78)!=0x1c8||memcmp(d+0x1c8,"FireIOS\0",8)||*(DWORD*)(d+0xc88)!=100000)return false;
 *(DWORD*)(d+0xc88)=(DWORD)fireParticles;
 char b[96];sprintf_s(b,"PhysX: campfire particle pool %d (game 100000).",fireParticles);Log(b);return true;
}
struct Redirect{const wchar_t* file;bool(*patch)(BYTE*,DWORD);bool wanted;volatile LONG state;wchar_t copy[MAX_PATH];};
static Redirect redirects[2]={{L"EffectPackageEmittersDatabase.apb",PatchEmitters},{L"EffectPackageParticleSimulationsDatabase.apb",PatchSimulations}};
static Redirect* FindRedirect(const wchar_t* name){
 size_t n=wcslen(name);
 if(n>4&&!_wcsicmp(name+n-4,L".apb")){static volatile LONG seen=0;if(InterlockedIncrement(&seen)<=8){char b[MAX_PATH+40];sprintf_s(b,"PhysX: database %ls",name);Log(b);}}
 for(int i=0;i<2;i++){
  Redirect& r=redirects[i];if(!r.wanted)continue;
  wchar_t tail[96];swprintf_s(tail,L"turbulencemedia\\%s",r.file);size_t t=wcslen(tail);
  if(n>=t&&!_wcsicmp(name+n-t,tail)&&(n==t||name[n-t-1]==L'\\'||name[n-t-1]==L'/'))return &r;
 }
 return 0;
}
static bool BuildCopy(Redirect* r,const wchar_t* original){
 if(r->state)return r->state==1;
 r->state=2;
 HANDLE h=CreateFileW(original,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return false;
 DWORD size=GetFileSize(h,0);if(size==INVALID_FILE_SIZE||size>1048576){CloseHandle(h);return false;}
 BYTE* d=(BYTE*)HeapAlloc(GetProcessHeap(),0,size);DWORD got=0;BOOL ok=d&&ReadFile(h,d,size,&got,0);CloseHandle(h);
 if(!ok||got!=size||memcmp(d,"Z[\\]",4)||!r->patch(d,size)){char b[160];sprintf_s(b,"PhysX: %ls not recognised; unchanged.",r->file);Log(b);if(d)HeapFree(GetProcessHeap(),0,d);return false;}
 wcscpy_s(r->copy,configPath);wchar_t* slash=wcsrchr(r->copy,L'\\');if(!slash){HeapFree(GetProcessHeap(),0,d);return false;}
 wcscpy_s(slash+1,MAX_PATH-(slash+1-r->copy),r->file);
 h=CreateFileW(r->copy,GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
 DWORD put=0;ok=h!=INVALID_HANDLE_VALUE&&WriteFile(h,d,size,&put,0)&&put==size;if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);HeapFree(GetProcessHeap(),0,d);
 if(!ok)return false;
 r->state=1;return true;
}
// [Modules] SkipIntro=1: the start-up logo videos are reported as missing, so the game goes straight
// to the title screen. The game already copes with a missing one (ubi_logo.bik is referenced by the
// EXE but not shipped). The files on disk stay untouched; loading-screen videos are not affected.
static bool skipIntro=false;
static bool IsIntroVideo(const wchar_t* name){
 static const wchar_t* list[]={L"TWIMTBP.bik",L"abstergo_logo.bik",L"ac4bf_logo.bik",L"ubi_logo.bik",L"warning_disclaimer.bik"};
 size_t n=wcslen(name);
 for(int i=0;i<5;i++){size_t t=wcslen(list[i]);if(n>=t&&!_wcsicmp(name+n-t,list[i])&&(n==t||name[n-t-1]==L'\\'||name[n-t-1]==L'/')){
   static volatile LONG seen=0;if(InterlockedIncrement(&seen)<=8){char b[MAX_PATH+40];sprintf_s(b,"Intro: skipped %ls",name);Log(b);}return true;}}
 return false;
}
typedef HANDLE(WINAPI*CreateFileWFn)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef HANDLE(WINAPI*CreateFileAFn)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static CreateFileWFn realCreateFileW=0;static CreateFileAFn realCreateFileA=0;
static HANDLE WINAPI HookCreateFileW(LPCWSTR name,DWORD acc,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disp,DWORD flags,HANDLE tmpl){
 if(skipIntro&&name&&!(acc&GENERIC_WRITE)&&IsIntroVideo(name)){SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_HANDLE_VALUE;}
 if(name&&!(acc&GENERIC_WRITE)){Redirect* r=FindRedirect(name);if(r&&BuildCopy(r,name)){HANDLE h=realCreateFileW(r->copy,acc,share,sa,disp,flags,tmpl);if(h!=INVALID_HANDLE_VALUE)return h;}}
 return realCreateFileW(name,acc,share,sa,disp,flags,tmpl);
}
static HANDLE WINAPI HookCreateFileA(LPCSTR name,DWORD acc,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disp,DWORD flags,HANDLE tmpl){
 if(name&&!(acc&GENERIC_WRITE)){
  wchar_t w[MAX_PATH];
  if(MultiByteToWideChar(CP_ACP,0,name,-1,w,MAX_PATH)&&skipIntro&&IsIntroVideo(w)){SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_HANDLE_VALUE;}
  if(MultiByteToWideChar(CP_ACP,0,name,-1,w,MAX_PATH)){Redirect* r=FindRedirect(w);if(r&&BuildCopy(r,w)){HANDLE h=realCreateFileW(r->copy,acc,share,sa,disp,flags,tmpl);if(h!=INVALID_HANDLE_VALUE)return h;}}
 }
 return realCreateFileA(name,acc,share,sa,disp,flags,tmpl);
}
// Hook CreateFile in every module loaded so far (the game, APEX, PhysX, the C runtime ...),
// since we don't know which of them opens the effect databases.
static void WatchDatabases(){
 // v24: may run twice (at start for SkipIntro, again when APEX loads for the effect databases);
 // re-hooking an import that already points to our hook is harmless.
 realCreateFileA=(CreateFileAFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateFileA");
 realCreateFileW=(CreateFileWFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateFileW");
 HMODULE self=0;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)WatchDatabases,&self);
 HMODULE mods[512];DWORD need=0;int hooked=0;void* ignore;
 if(K32EnumProcessModules(GetCurrentProcess(),mods,sizeof(mods),&need)){
  for(DWORD i=0;i<need/sizeof(HMODULE)&&i<512;i++){
   if(mods[i]==self)continue;
   if(HookImportIn(mods[i],"KERNEL32.dll","CreateFileA",(void*)HookCreateFileA,&ignore))hooked++;
   if(HookImportIn(mods[i],"KERNEL32.dll","CreateFileW",(void*)HookCreateFileW,&ignore))hooked++;
  }
 }
 char b[96];sprintf_s(b,"Files: watching effect databases / intro videos (%d hooks).",hooked);Log(b);
}
#include "sprite_first.h"
static void OnApexLoaded(HMODULE m){
 PatchSpriteWrite(m);
 if(wantSimRate)PatchApex(m);
 if(wantMeshCache)PatchCooking(GetModuleHandleW(L"PhysX3Cooking_x86.dll"));
 if(redirects[0].wanted||redirects[1].wanted)WatchDatabases();
}
typedef HMODULE(WINAPI*LoadAFn)(LPCSTR);static LoadAFn realLoadA=0;
static HMODULE WINAPI HookLoadA(LPCSTR name){
 HMODULE m=realLoadA(name);
 if(m&&name){const char* b=name;for(const char* p=name;*p;p++)if(*p=='\\'||*p=='/')b=p+1;if(!_stricmp(b,"ApexParticles_x86.dll"))OnApexLoaded(m);}
 return m;
}
// Only campfires (acbf_fire) get the longer distances; chimneys, guns and the rest keep the
// game's. Each of these loads is replaced by a stub that checks the effect's name pointer.
static const DWORD fireName=0x1e7e3a4;static const float gFar=15,gNear=6,gInv=1.0f/9,gAct=20;
static DWORD sdBack1=0x850bcb,sdBack2=0x850c48,sdBack3=0x850c66,sdBack4=0x850cbd,sdBack5=0x850cd2,sdBack6=0x85d341;
__declspec(naked) static void SdFar(){__asm{
 cmp dword ptr [edi+10h],1e7e3a4h
 jne g
 movss xmm6,dword ptr [smokeFadeFar]
 jmp dword ptr [sdBack1]
g: movss xmm6,dword ptr [gFar]
 jmp dword ptr [sdBack1]
}}
__declspec(naked) static void SdNear(){__asm{
 cmp dword ptr [esi],1e7e3a4h
 jne g
 comiss xmm1,dword ptr [smokeFadeNear]
 jmp dword ptr [sdBack2]
g: comiss xmm1,dword ptr [gNear]
 jmp dword ptr [sdBack2]
}}
__declspec(naked) static void SdInv(){__asm{
 cmp dword ptr [esi],1e7e3a4h
 jne g
 mulss xmm0,dword ptr [smokeFadeInv]
 jmp dword ptr [sdBack3]
g: mulss xmm0,dword ptr [gInv]
 jmp dword ptr [sdBack3]
}}
__declspec(naked) static void SdAct1(){__asm{
 cmp dword ptr [esi],1e7e3a4h
 jne g
 movss xmm0,dword ptr [smokeActivate]
 jmp dword ptr [sdBack4]
g: movss xmm0,dword ptr [gAct]
 jmp dword ptr [sdBack4]
}}
__declspec(naked) static void SdAct2(){__asm{
 cmp dword ptr [esi],1e7e3a4h
 jne g
 movss xmm0,dword ptr [smokeActivate]
 jmp dword ptr [sdBack5]
g: movss xmm0,dword ptr [gAct]
 jmp dword ptr [sdBack5]
}}
// v37: hysteresis. The game starts AND stops every smoke effect at the same 20 m, so walking along that
// line created and released the APEX effect again and again (suspected source of the stale particle
// children: hang +0x31560, crash +0x32221). Effects now stop only beyond the keep distance (default 25 m).
__declspec(naked) static void SdKeep(){__asm{
 cmp esi,1e7e3a4h
 jne g
 movss xmm0,dword ptr [smokeKeepFire]
 jmp dword ptr [sdBack6]
g: movss xmm0,dword ptr [smokeKeepOther]
 jmp dword ptr [sdBack6]
}}
static bool QueueSmokeDistances(){
 smokeFadeInv=1.0f/(smokeFadeFar-smokeFadeNear);
 if(memcmp((void*)fireName,"acbf_fire",10))return false;
 static const BYTE bFar[]={0xf3,0x0f,0x10,0x35,0x28,0x23,0xe4,0x01};
 static const BYTE bNear[]={0x0f,0x2f,0x0d,0x9c,0x45,0xe4,0x01};
 static const BYTE bInv[]={0xf3,0x0f,0x59,0x05,0x08,0x2d,0xe7,0x01};
 static const BYTE bAct[]={0xf3,0x0f,0x10,0x05,0x98,0x1a,0xe4,0x01};
 if(!Jump(0x850bc3,bFar,8,SdFar)||!Jump(0x850c41,bNear,7,SdNear)||!Jump(0x850c5e,bInv,8,SdInv)||
    !Jump(0x850cb5,bAct,8,SdAct1)||!Jump(0x850cca,bAct,8,SdAct2)||!Jump(0x85d339,bAct,8,SdKeep))return false;
 smokeCustom=true;smokeMine[0]=smokeFadeNear;smokeMine[1]=smokeFadeFar;smokeMine[2]=smokeFadeInv;smokeMine[3]=smokeActivate;smokeMine[4]=smokeKeepFire;smokeMine[5]=smokeKeepShots;
 char b[200];sprintf_s(b,"PhysX: campfire smoke full strength to %.0f m, fades out by %.0f m, starts at %.0f m, stops beyond %.0f m (game: 6 / 15 / 20 / 20 m).",smokeFadeNear,smokeFadeFar,smokeActivate,smokeKeepFire);Log(b);
 return true;
}

// ---- Diagnostics: how often Havok and the cloth system step, and with which dt --------------
// Only active with [Debug] Diagnostics=1. Swaps vtable entries (Steam 2023 build only) for thin
// wrappers that count calls and record dt, then call the original. Writes one line per 2 s.
struct DiagStat{volatile LONG calls;float minDt,maxDt,sumDt;};
static DiagStat diagStep={0,1e9f,0,0},diagBegin={0,1e9f,0,0};static volatile LONG diagCloth=0;
static CRITICAL_SECTION diagLock;
static void DiagAdd(DiagStat& s,float dt){EnterCriticalSection(&diagLock);s.calls++;if(dt<s.minDt)s.minDt=dt;if(dt>s.maxDt)s.maxDt=dt;s.sumDt+=dt;LeaveCriticalSection(&diagLock);}
typedef int (__thiscall *StepFn)(void*,float);typedef int (__thiscall *BeginFn)(void*,void*,float);typedef void (__thiscall *ClothFn)(void*,void*);
static volatile LONG callerCount=0;static DWORD callers[8];
// Walks two ebp frames up: [our caller (Havok)] -> [its caller] -> [their caller].
static void DiagCaller(DWORD ebpValue){
 NT_TIB* tib=(NT_TIB*)NtCurrentTeb();DWORD lo=(DWORD)tib->StackLimit,hi=(DWORD)tib->StackBase;
 DWORD chain[3]={0,0,0};DWORD f=ebpValue;
 for(int i=0;i<3;i++){ if(f<lo||f+8>hi)break; chain[i]=((DWORD*)f)[1]; f=((DWORD*)f)[0]; }
 DWORD key=chain[0]^(chain[1]<<1)^(chain[2]<<2);
 for(LONG i=0;i<callerCount&&i<8;i++)if(callers[i]==key)return;
 LONG n=InterlockedIncrement(&callerCount)-1;if(n>=8)return;callers[n]=key;
 char b[128];sprintf_s(b,"DIAG: havok call chain %08lx <- %08lx <- %08lx",(unsigned long)chain[0],(unsigned long)chain[1],(unsigned long)chain[2]);Log(b);
}
static StepFn origStep[3];static StepFn origWhole[3];static volatile LONG diagStepUsMax=0,diagStepUsSum=0;static volatile LONG diagClothUs=0;
static LONGLONG QpcNow(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return q.QuadPart;}
static int WholeTimed(int i,void* s,float dt){LONGLONG a=QpcNow();int r=origWhole[i](s,dt);LONG us=(LONG)((QpcNow()-a)*1000000/(frequency?frequency:1));
 InterlockedExchangeAdd(&diagStepUsSum,us);if(us>diagStepUsMax)diagStepUsMax=us;return r;}
static int __fastcall WholeHook0(void* s,void*,float dt){return WholeTimed(0,s,dt);}
static int __fastcall WholeHook1(void* s,void*,float dt){return WholeTimed(1,s,dt);}
static int __fastcall WholeHook2(void* s,void*,float dt){return WholeTimed(2,s,dt);}static BeginFn origBegin;static ClothFn origCloth;
// ---- Ragdoll lab ([Experiment] AutoLab=2) -------------------------------------------------------
// Bodies seen in the Havok world are snapshotted right before and right after every physics step
// (30 Hz). Before-step vs previous after-step shows what game code changed between steps
// (velocities set by ragdoll/animation drivers, bodies moved by hand); after vs before shows what
// the solver did (contacts, constraints). A lying body that is suddenly launched is logged with
// the side that did it and the fastest non-dynamic body (character capsule, keyframed prop) near
// it. setLinearVelocity / setAngularVelocity / applyLinearImpulse of hkpMotion are probed too, per
// caller. Layout (Havok 2011.3, verified by the code that uses it): simulation+0x0C = world,
// world+0x28 = active islands; rigid body+0xE0 = motion (vtable), motion+0x40 = position,
// motion+0xD0 = linear velocity, motion+0xE0 = angular velocity, motion+0xC0.w = 1/mass.
static bool HookEntry(DWORD va,const BYTE* expect,void* to);
static bool RgFinite(float f){DWORD u;memcpy(&u,&f,4);return (u&0x7f800000)!=0x7f800000;}   // false for NaN and inf
static const DWORD rgVt[8]={0x25db36c,0x25db3dc,0x25db454,0x25db4cc,0x25db54c,0x25db5bc,0x25db62c,0x25db69c};
static int RgClass(DWORD vt){for(int i=0;i<8;i++)if(rgVt[i]==vt)return i;return -1;}
struct RgBody{DWORD rb,ud;int cls,type;float p[3],v[3],w[3];};
static const int RG_MAX=2048;
struct RgHash{DWORD rb;int idx;};
// one state per Havok world (v14: the game steps several worlds; a 200 m/s body was in one we did not scan)
struct RgW{DWORD world;LONG steps;double bodies;RgBody a[RG_MAX],b[RG_MAX],c[RG_MAX],post[RG_MAX];RgBody *pre,*prevPost,*prevPre;int preN,prevPostN,prevPreN,postN;RgHash h1[4096],h2[4096],h3[4096];};
static RgW* rgWorlds[4];static RgW* W=0;
static RgW* RgSelect(DWORD world){for(int i=0;i<4;i++){RgW* w=rgWorlds[i];if(!w){w=(RgW*)VirtualAlloc(0,sizeof(RgW),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!w)return 0;w->world=world;w->pre=w->a;w->prevPost=w->b;w->prevPre=w->c;rgWorlds[i]=w;
   char b[96];sprintf_s(b,"RAG: Havok world #%d at %08lx",i,world);Log(b);return w;}
  if(w->world==world)return w;}return 0;}
static volatile LONG rgOn=0,rgFaults=0;static int rgIslOff=-1,rgEntIslOff=-1;static bool rgCensus=false;
static CRITICAL_SECTION rgLock;static bool rgLockReady=false;

static void RgBuild(RgHash* h,const RgBody* a,int n){memset(h,0,sizeof(RgHash)*4096);for(int i=0;i<n;i++){UINT k=(a[i].rb>>4)*2654435761u>>20;for(int j=0;j<64;j++){RgHash& e=h[(k+j)&4095];if(!e.rb){e.rb=a[i].rb;e.idx=i;break;}}}}
static int RgFind(const RgHash* h,DWORD rb){UINT k=(rb>>4)*2654435761u>>20;for(int j=0;j<64;j++){const RgHash& e=h[(k+j)&4095];if(e.rb==rb)return e.idx;if(!e.rb)return -1;}return -1;}
static bool RgPtr(DWORD a){return a>=0x10000&&a<0xffff0000&&!(a&3);}   // v24: the game is large-address-aware (filter at 0xfd0c1770)
static int RgFindEntities(DWORD isl,DWORD world){
 if(*(DWORD*)(isl+0x18)!=world)return -1;
 for(int off=0x1c;off<0xd0;off+=4){
  DWORD p=*(DWORD*)(isl+off);int sz=*(int*)(isl+off+4);int cap=*(int*)(isl+off+8)&0x3fffffff;
  if(!RgPtr(p)||sz<=0||sz>cap||cap>65536)continue;
  DWORD e=*(DWORD*)p;if(!RgPtr(e))continue;
  if(RgClass(*(DWORD*)(e+0xe0))>=0)return off;
 }
 return -1;
}
static int RgScanInner(DWORD world,RgBody* out){
 int n=0;DWORD isl=*(DWORD*)(world+0x28);int ni=*(int*)(world+0x2c);
 if(!RgPtr(isl)||ni<0||ni>8192)return 0;
 for(int i=0;i<ni&&n<RG_MAX;i++){DWORD s=((DWORD*)isl)[i];if(!RgPtr(s))continue;
  if(rgIslOff<0){rgIslOff=RgFindEntities(s,world);
   if(rgIslOff>=0){char b[200];const float* g=(const float*)(world+0x10);sprintf_s(b,"RAG: world %08lx gravity %.2f %.2f %.2f, %d active islands, island entities at +0x%x",world,g[0],g[1],g[2],ni,rgIslOff);Log(b);}
   else continue;}
  DWORD ea=*(DWORD*)(s+rgIslOff);int ne=*(int*)(s+rgIslOff+4);if(!RgPtr(ea)||ne<0||ne>8192)continue;
  for(int k=0;k<ne&&n<RG_MAX;k++){DWORD rb=((DWORD*)ea)[k];if(!RgPtr(rb))continue;
   const BYTE* m=(const BYTE*)(rb+0xe0);int c=RgClass(*(DWORD*)m);if(c<0)continue;
   // v20: where a body keeps its island (hkpEntity::m_simulationIsland) - learned from the first body
   if(rgEntIslOff<0){for(int off=0x14;off<0x240;off+=4)if(off!=0xe0&&*(DWORD*)(rb+off)==s){rgEntIslOff=off;char b[160];sprintf_s(b,"RAG: body island pointer at rb+0x%x, world pointer rb+8 = %08lx (world %08lx)",off,*(DWORD*)(rb+8),world);Log(b);break;}}
   RgBody& o=out[n++];o.rb=rb;o.ud=*(DWORD*)(rb+0xc);o.cls=c;o.type=m[8];
   memcpy(o.p,m+0x40,12);memcpy(o.v,m+0xd0,12);memcpy(o.w,m+0xe0,12);}
 }
 return n;
}
static int RgScan(DWORD world,RgBody* out){__try{return RgScanInner(world,out);}__except(EXCEPTION_EXECUTE_HANDLER){InterlockedIncrement(&rgFaults);return 0;}}
static float RgLen(const float* a){return sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
static float RgDist(const float* a,const float* b){float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return RgLen(d);}
// per motion class, per lab phase
struct RgStat{double snaps,speed,outDv,outDvSum,outW,outDp,inDv,rev,revN,setCalls;double big;};
static RgStat rgStat[8];static volatile LONG rgSteps=0,rgEvents=0,rgShotAsk=0;static double rgBodiesSum=0;
// v15: last 4096 velocity/spin/impulse sets (any size); an event lists the sets of that body.
struct RgSet{DWORD motion,ret;float m;LONG frame;LONG kind;};static RgSet rgRing[4096];static volatile LONG rgRingI=0;
static int RgHistory(DWORD motion,char* out,int cap){
 int n=0,found=0;LONG top=rgRingI,f=frameIndex;
 for(LONG i=0;i<4096&&found<8;i++){const RgSet& e=rgRing[(top-i)&4095];if(e.motion!=motion)continue;if(f-e.frame>90)break;
  n+=sprintf_s(out+n,cap-n," %s %.1f by %08lx %ld fr ago;",e.kind==0?"v":e.kind==1?"w":"imp",e.m,e.ret,f-e.frame);found++;}
 if(!found)n+=sprintf_s(out+n,cap-n," none in the last 90 frames (set inside Havok or written directly)");
 return n;
}
static volatile LONG wkN[2],wkBig[2];static volatile LONGLONG wkSum[2],wkWSum[2];static volatile LONG wkMax[2];
static void RgEvent(const char* kind,const RgBody& b,const float* v0,const float* v1,const float* w0,const float* w1,bool fromRest,const RgBody* nb){
 if(InterlockedIncrement(&rgEvents)>40)return;
 char t[1000];int n=sprintf_s(t,"RAG EVENT %s: body %08lx (owner %08lx) class %d type %d at %.1f %.1f %.1f%s | v %.2f -> %.2f m/s, w %.1f -> %.1f rad/s",kind,b.rb,b.ud,b.cls,b.type,b.p[0],b.p[1],b.p[2],fromRest?" FROM REST":"",RgLen(v0),RgLen(v1),RgLen(w0),RgLen(w1));
 if(nb)n+=sprintf_s(t+n,sizeof(t)-n," | nearest other body %08lx (owner %08lx) class %d type %d at %.2f m, speed %.2f m/s",nb->rb,nb->ud,nb->cls,nb->type,RgDist(nb->p,b.p),RgLen(nb->v));
 n+=sprintf_s(t+n,sizeof(t)-n," | recent sets:");n+=RgHistory(b.rb+0xe0,t+n,sizeof(t)-n);
 Log(t);InterlockedExchange(&rgShotAsk,1);
}
static void RgCensus(const RgBody* a,int n){
 int cnt[8][16]={};for(int i=0;i<n;i++)cnt[a[i].cls][a[i].type&15]++;
 char b[400];int k=sprintf_s(b,"RAG census (%d bodies): class/type=count",n);
 for(int c=0;c<8;c++)for(int t=0;t<16;t++)if(cnt[c][t])k+=sprintf_s(b+k,sizeof(b)-k," %d/%d=%d",c,t,cnt[c][t]);
 Log(b);
}
// v17: why do corpses not collide with each other? 1) which collision filter the world uses (RTTI
// names of the objects the world points to), 2) the collision filter info of bodies (rb+0x2C:
// hkpCollidable at +0x10, broadphase handle +0x14, filter info +0x08 in it): layer = bits 0-4,
// system group = bits 16-31 (Havok group-filter convention, to be confirmed by the values).
static const char* RgRtti(DWORD obj){
 DWORD vt=*(DWORD*)obj;if(vt<0x1e3a000||vt>=0x2743000)return 0;DWORD col=*(DWORD*)(vt-4);if(col<0x1e3a000||col>=0x2743000)return 0;
 DWORD td=*(DWORD*)(col+0xc);if(td<0x2000000||td>=0x4f00000)return 0;const char* n=(const char*)(td+8);return n[0]=='.'&&n[1]=='?'?n:0;
}
static const char* RgRttiSafe(DWORD p){__try{return RgRtti(p);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
// a Havok group filter keeps a 32-entry table "layer i collides with layers (bitmask)"; it is symmetric
static int RgFindLayerTable(DWORD obj,int maxOff=0x100){
 __try{for(int off=8;off<maxOff;off+=4){const DWORD* t=(const DWORD*)(obj+off);int nz=0;bool sym=true;
   for(int i=0;i<32&&sym;i++){if(t[i])nz++;for(int j=0;j<32;j++)if(((t[i]>>j)&1)!=((t[j]>>i)&1)){sym=false;break;}}
   int diff=0;for(int i=1;i<32;i++)if(t[i]!=t[0])diff++;   // v21: 32 equal words (all 0xFFFFFFFF) are not a table
   if(sym&&nz>=8&&diff>=4)return off;}}__except(EXCEPTION_EXECUTE_HANDLER){}
 return -1;
}
static DWORD rgFilterObj=0;static int rgFilterTab=-1;
static void RgWorldReportInner(DWORD world){
 char b[2400];int k=sprintf_s(b,"RAG FILTER world %08lx objects:",world);
 // v20: v19 tried the layer table only in objects with an RTTI name and found none; now every
 // pointer of the world is tried, and unnamed ones are printed with their vtable
 for(int off=0;off<0x200&&k<2200;off+=4){DWORD p=0;__try{p=*(DWORD*)(world+off);}__except(EXCEPTION_EXECUTE_HANDLER){break;}if(!RgPtr(p))continue;const char* n=RgRttiSafe(p);
  DWORD vt=0;__try{vt=*(DWORD*)p;}__except(EXCEPTION_EXECUTE_HANDLER){continue;}
  if(n)k+=sprintf_s(b+k,sizeof(b)-k," +%x=%s;",off,n);else if(vt>=0x400000&&vt<0x2800000)k+=sprintf_s(b+k,sizeof(b)-k," +%x=vt %08lx;",off,vt);
  int t=RgFindLayerTable(p);if(t>=0&&rgFilterTab<0){rgFilterObj=p;rgFilterTab=t;k+=sprintf_s(b+k,sizeof(b)-k,"[layer table at +%x]",t);}}
 Log(b);
 // v21: the filter can sit one pointer deeper (e.g. collision input -> filter) and its table further in
 if(rgFilterTab<0)for(int off=0;off<0x200&&rgFilterTab<0;off+=4){DWORD p=0;__try{p=*(DWORD*)(world+off);}__except(EXCEPTION_EXECUTE_HANDLER){break;}if(!RgPtr(p))continue;
  int t=RgFindLayerTable(p,0x400);if(t>=0){rgFilterObj=p;rgFilterTab=t;sprintf_s(b,"RAG FILTER layer table found in world+%x object at +%x",off,t);Log(b);break;}
  for(int j=0;j<0x100&&rgFilterTab<0;j+=4){DWORD q=0;__try{q=*(DWORD*)(p+j);}__except(EXCEPTION_EXECUTE_HANDLER){break;}if(!RgPtr(q))continue;
   t=RgFindLayerTable(q,0x400);if(t>=0){rgFilterObj=q;rgFilterTab=t;const char* n=RgRttiSafe(q);sprintf_s(b,"RAG FILTER layer table found in [world+%x]+%x object (%s) at +%x",off,j,n?n:"no RTTI",t);Log(b);}}}
 if(rgFilterTab<0)Log("RAG FILTER: no symmetric 32x32 layer table within two pointers of the world");
 // v23: dump the collision filter itself (hkpWorld::m_collisionFilter at +0x74 by the Havok layout)
 __try{DWORD f=*(DWORD*)(world+0x74);int k=sprintf_s(b,"RAG FILTER m_collisionFilter (world+0x74) = %08lx",f);
  if(RgPtr(f)){const char* n=RgRttiSafe(f);k+=sprintf_s(b+k,sizeof(b)-k," %s |",n?n:"(no RTTI)");
   for(int o=0;o<0x180&&k<sizeof(b)-24;o+=4)k+=sprintf_s(b+k,sizeof(b)-k,"%s%08lx",o%32?" ":(o?" | +":" +"),*(DWORD*)(f+o));
   Log(b);
   for(int o=0;o<0x20;o+=4){DWORD vt=*(DWORD*)(f+o);if(vt<0x1e3a000||vt>=0x2abe000)continue;k=sprintf_s(b,"RAG FILTER vtable at filter+%x = %08lx:",o,vt);
    for(int i=0;i<12;i++)k+=sprintf_s(b+k,sizeof(b)-k," %08lx",((DWORD*)vt)[i]);Log(b);}}
  else Log(b);}__except(EXCEPTION_EXECUTE_HANDLER){Log("RAG FILTER: filter dump read fault");}
 if(rgFilterTab>=0){const DWORD* t=(const DWORD*)(rgFilterObj+rgFilterTab);k=sprintf_s(b,"RAG FILTER layer table (layer: collides with):");
  for(int i=0;i<32;i++)if(t[i])k+=sprintf_s(b+k,sizeof(b)-k," %d:%08lx",i,t[i]);Log(b);}
}
// all bodies of the world: active islands (+0x28) and inactive ones (+0x34) - lying corpses sleep
static int RgAllBodies(DWORD world,DWORD* rbs,int* cls,int max){
 int n=0;if(rgIslOff<0)return 0;
 for(int arr=0x28;arr<=0x34;arr+=0xc){DWORD isl=*(DWORD*)(world+arr);int ni=*(int*)(world+arr+4);if(!RgPtr(isl)||ni<0||ni>8192)continue;
  for(int i=0;i<ni&&n<max;i++){DWORD s=((DWORD*)isl)[i];if(!RgPtr(s))continue;DWORD ea=*(DWORD*)(s+rgIslOff);int ne=*(int*)(s+rgIslOff+4);if(!RgPtr(ea)||ne<0||ne>8192)continue;
   for(int k=0;k<ne&&n<max;k++){DWORD rb=((DWORD*)ea)[k];if(!RgPtr(rb))continue;int c=RgClass(*(DWORD*)(rb+0xe0));if(c<0)continue;rbs[n]=rb;cls[n]=c;n++;}}}
 return n;
}
static void RgFilterReportInner(DWORD world){
 static DWORD rbs[4096];static int cl[4096];int n=RgAllBodies(world,rbs,cl,4096);
 struct Row{DWORD info;int cls;int cnt;};Row rows[64];int nr=0;
 for(int i=0;i<n;i++){DWORD info=*(DWORD*)(rbs[i]+0x2c);int c=cl[i];if(c==3)continue;
  int j=0;for(;j<nr;j++)if(rows[j].info==info&&rows[j].cls==c){rows[j].cnt++;break;}
  if(j==nr&&nr<48){rows[nr].info=info;rows[nr].cls=c;rows[nr].cnt=1;nr++;}}
 char b[1600];int k=sprintf_s(b,"RAG FILTER infos, %d bodies active+sleeping (class: info=layer/sysgroup/sub x count):",n);
 for(int j=0;j<nr&&k<1500;j++)k+=sprintf_s(b+k,sizeof(b)-k," c%d:%08lx=L%lu/G%lu/S%lu x%d;",rows[j].cls,rows[j].info,rows[j].info&31,rows[j].info>>16,(rows[j].info>>5)&31,rows[j].cnt);
 Log(b);
}
static void RgFilterReport(DWORD world,const RgBody* a,int n){
 static bool worldDone=false;static DWORD last=0;
 DWORD now=GetTickCount();if(last&&now-last<20000)return;last=now;
 __try{static int tries=0;if(!worldDone){if(++tries>=3)worldDone=true;RgWorldReportInner(world);if(rgFilterTab>=0)worldDone=true;}RgFilterReportInner(world);}__except(EXCEPTION_EXECUTE_HANDLER){Log("RAG FILTER: read fault");}
}
// v20: life of a corpse. v19 showed the driven ragdoll parts (class 5) are never in the islands of
// the stepped world, active or sleeping. The first part of every new ragdoll is tracked for 90 s:
// its world (rb+8), its island, whether that island is in the active / inactive list, motion
// class/type and speed - logged when it changes.
static volatile DWORD rgTrBody[8],rgTrOwner[8];static DWORD rgTrT[8];static char rgTrPrev[8][160];
static void RgTrackAdd(DWORD body,DWORD owner){
 if(!body)return;for(int i=0;i<8;i++)if(rgTrOwner[i]==owner)return;
 for(int i=0;i<8;i++)if(!rgTrBody[i]&&InterlockedCompareExchange((volatile LONG*)&rgTrBody[i],(LONG)body,0)==0){rgTrOwner[i]=owner;rgTrT[i]=GetTickCount();rgTrPrev[i][0]=0;return;}
}
static int RgInList(DWORD world,int arr,DWORD isl){DWORD a=*(DWORD*)(world+arr);int n=*(int*)(world+arr+4);if(!RgPtr(a)||n<0||n>8192)return -1;for(int i=0;i<n;i++)if(((DWORD*)a)[i]==isl)return i;return -1;}
static void RgWhere(DWORD rb,char* o,int cap,float* speed){
 *speed=-1;DWORD m=rb+0xe0;int c=RgClass(*(DWORD*)m);int type=((BYTE*)m)[8];DWORD wo=*(DWORD*)(rb+8);
 DWORD isl=rgEntIslOff>=0?*(DWORD*)(rb+rgEntIslOff):0;DWORD iw=0;const char* lst="no island";
 if(RgPtr(isl)){iw=*(DWORD*)(isl+0x18);lst="island in no list";if(RgPtr(iw)){if(RgInList(iw,0x28,isl)>=0)lst="ACTIVE island";else if(RgInList(iw,0x34,isl)>=0)lst="SLEEPING island";}}
 *speed=RgLen((const float*)(m+0xd0));
 sprintf_s(o,cap,"class %d type %d info %08lx | world %08lx | island %08lx (world %08lx) %s",c,type,*(DWORD*)(rb+0x2c),wo,isl,iw,lst);
}
static void RgTrackReport(){
 DWORD now=GetTickCount();
 for(int i=0;i<8;i++){DWORD rb=rgTrBody[i];if(!rb)continue;DWORD age=now-rgTrT[i];
  char st[160];float sp=-1;bool ok=true;__try{RgWhere(rb,st,sizeof(st),&sp);}__except(EXCEPTION_EXECUTE_HANDLER){ok=false;strcpy_s(st,"read fault (body freed?)");}
  if(strcmp(st,rgTrPrev[i])||age>=90000||!ok){strcpy_s(rgTrPrev[i],st);char b[300];sprintf_s(b,"RAG TRACK owner %08lx body %08lx at %.1f s: %s | speed %.2f m/s",rgTrOwner[i],rb,age/1000.0,st,sp);Log(b);}
  if(age>=90000||!ok){rgTrOwner[i]=0;InterlockedExchange((volatile LONG*)&rgTrBody[i],0);}}
}
static void RgPre(void* sim){
 if(!rgOn||!rgLockReady)return;
 DWORD world=*(DWORD*)((BYTE*)sim+0xc);if(!RgPtr(world))return;
 W=RgSelect(world);if(!W)return;
 W->preN=RgScan(world,W->pre);if(!W->preN)return;W->steps++;W->bodies+=W->preN;
 // NaN guard: a body with a non-finite velocity is stopped (otherwise it flies "into the stratosphere")
 for(int i=0;i<W->preN;i++){RgBody& b=W->pre[i];if(!RgFinite(b.v[0]+b.v[1]+b.v[2]+b.w[0]+b.w[1]+b.w[2])){float* m=(float*)(b.rb+0xe0);
   static volatile LONG nn=0;if(InterlockedIncrement(&nn)<=30){char t[200];sprintf_s(t,"RAG NAN GUARD: body %08lx (owner %08lx) class %d at %.1f %.1f %.1f had a non-finite velocity; stopped",b.rb,b.ud,b.cls,b.p[0],b.p[1],b.p[2]);Log(t);}
   for(int k=0;k<3;k++){m[0xd0/4+k]=0;m[0xe0/4+k]=0;b.v[k]=0;b.w[k]=0;}}}
 if(!rgCensus&&W->preN>20){rgCensus=true;RgCensus(W->pre,W->preN);}
 RgFilterReport(world,W->pre,W->preN);
 {static DWORD lastT=0;DWORD now=GetTickCount();if(now-lastT>=2000){lastT=now;static DWORD rbs[4096];static int cl[4096];static char prevLine[600];
   __try{int n=RgAllBodies(world,rbs,cl,4096);int cnt[8][32]={};for(int i=0;i<n;i++)cnt[cl[i]][(*(DWORD*)(rbs[i]+0x2c))&31]++;
    char line[600];line[0]=0;int k=0;for(int c=0;c<8;c++)for(int L=0;L<32;L++)if(cnt[c][L]&&c!=3)k+=sprintf_s(line+k,sizeof(line)-k," c%d/L%d=%d",c,L,cnt[c][L]);
    if(strcmp(line,prevLine)){strcpy_s(prevLine,line);char b[700];sprintf_s(b,"RAG BODIES (class/layer=count):%s",line);Log(b);}}__except(EXCEPTION_EXECUTE_HANDLER){}
   RgTrackReport();}}
 RgBuild(W->h1,W->prevPost,W->prevPostN);RgBuild(W->h3,W->prevPre,W->prevPreN);
 EnterCriticalSection(&rgLock);
 InterlockedIncrement(&rgSteps);rgBodiesSum+=W->preN;
 for(int i=0;i<W->preN;i++){const RgBody& b=W->pre[i];RgStat& s=rgStat[b.cls];float sp=RgLen(b.v);s.snaps++;s.speed+=sp;
  int j=RgFind(W->h1,b.rb);
  if(j>=0){const RgBody& q=W->prevPost[j];float dv[3]={b.v[0]-q.v[0],b.v[1]-q.v[1],b.v[2]-q.v[2]},dw[3]={b.w[0]-q.w[0],b.w[1]-q.w[1],b.w[2]-q.w[2]};float d=RgLen(dv);
   if(d>0.5f)s.outDv++;s.outDvSum+=d;if(RgLen(dw)>3.0f)s.outW++;if(RgDist(b.p,q.p)>0.05f)s.outDp++;
   if((d>4.0f||RgLen(dw)>50.0f)&&b.cls>=4){LeaveCriticalSection(&rgLock);RgEvent("set between steps (game code)",b,q.v,b.v,q.w,b.w,RgLen(q.v)<0.3f,0);EnterCriticalSection(&rgLock);}}
  else if(b.cls>=4){int g=b.cls==4?0:1;InterlockedIncrement(&wkN[g]);InterlockedExchangeAdd64(&wkSum[g],(LONGLONG)(sp*1000));InterlockedExchangeAdd64(&wkWSum[g],(LONGLONG)(RgLen(b.w)*1000));
   if((LONG)(sp*1000)>wkMax[g])wkMax[g]=(LONG)(sp*1000);if(sp>2.0f)InterlockedIncrement(&wkBig[g]);}
  if(j<0&&sp>2.0f&&b.cls>=4){LeaveCriticalSection(&rgLock);float z[3]={0,0,0};RgEvent("woke up already fast",b,z,b.v,z,b.w,false,0);EnterCriticalSection(&rgLock);}
  int k=RgFind(W->h3,b.rb);
  if(k>=0){const RgBody& q=W->prevPre[k];float lq=RgLen(q.v);if(sp>0.3f&&lq>0.3f){s.revN++;if(b.v[0]*q.v[0]+b.v[1]*q.v[1]+b.v[2]*q.v[2]<0)s.rev++;}}
 }
 LeaveCriticalSection(&rgLock);
}
static void RgPost(void* sim){
 if(!rgOn||!rgLockReady)return;
 DWORD world=*(DWORD*)((BYTE*)sim+0xc);if(!RgPtr(world))return;
 W=RgSelect(world);if(!W||!W->preN)return;
 W->postN=RgScan(world,W->post);
 RgBuild(W->h2,W->pre,W->preN);
 for(int i=0;i<W->postN;i++){const RgBody& b=W->post[i];int j=RgFind(W->h2,b.rb);if(j<0)continue;const RgBody& q=W->pre[j];
  float dv[3]={b.v[0]-q.v[0],b.v[1]-q.v[1],b.v[2]-q.v[2]};float d=RgLen(dv);
  if(d>4.0f){EnterCriticalSection(&rgLock);rgStat[b.cls].inDv++;LeaveCriticalSection(&rgLock);
   if(b.cls>=4&&RgLen(b.v)>4.0f){
    const RgBody* nb=0;float best=1e9f;
    for(int k=0;k<W->postN;k++){const RgBody& o=W->post[k];if(o.cls==b.cls&&o.cls>=4&&o.ud==b.ud)continue;if(o.rb==b.rb||o.cls==3)continue;float dd=RgDist(o.p,b.p);if(dd<2.0f&&(dd<best)){best=dd;nb=&o;}}
    RgEvent("pushed inside the step (contact/constraint)",b,q.v,b.v,q.w,b.w,RgLen(q.v)<0.3f,nb);}}
 }
 // rotate: post -> prevPost, pre -> prevPre
 memcpy(W->prevPost,W->post,sizeof(RgBody)*W->postN);W->prevPostN=W->postN;
 RgBody* t=W->prevPre;W->prevPre=W->pre;W->pre=t;W->prevPreN=W->preN;W->preN=0;
}
// 0x5fdec0(obj, float s): drives a dynamic body to a moving target (v = target motion * s plus a
// correction); called from the component update 0x4c49e0 for attachments of type 1. Seen setting
// up to 86 m/s / 190 rad/s, 1450 calls/s at full FPS vs 260 at 60. s is recorded with the frame time.
static volatile LONG fdN=0,fdSMinB=0x7f7fffff,fdSMaxB=0;static volatile LONGLONG fdSSum=0,fdSDt=0;static volatile LONG fdLastSBits=0,fdFrames=0,fdLastFrame=-1;
// v9/v10 result: s = 1/frame time exactly. The target motion since this object's PREVIOUS update is
// multiplied by 1/(this frame's time): wrong whenever the object was not updated in the previous
// frame (newly activated, intermittent updates: stale target -> metres of "motion" x 141) and on
// very short frames (s up to 1500). Candidate fix (lab condition, later [Physics] RagdollFollowFix):
// s = 1 / (real time since this object's previous update), never larger than the game's s; first
// update or a gap over 0.25 s -> s = 4 (the stale target must not turn into a launch).
__declspec(thread) static const char* tlJob=0;   // last engine job dispatched on this thread (set by the job profiler)
static volatile LONG tpGuard=0;                   // teleport guard: lab-only until verified (v16 review)
struct FdJob{const char* name;volatile LONG calls,tele,big;};static FdJob fdJobs[16];
static FdJob* FdJobSlot(){const char* nm=tlJob?tlJob:"(no job)";for(int i=0;i<16;i++){if(fdJobs[i].name==nm)return &fdJobs[i];if(!fdJobs[i].name&&InterlockedCompareExchange((volatile LONG*)&fdJobs[i].name,(LONG)nm,0)==0)return &fdJobs[i];}return 0;}
static volatile LONG rgDriveGen=0;
struct FdObj{DWORD obj;LONGLONG qpc;LONG frame;};static FdObj fdObjs[2048];static CRITICAL_SECTION fdLock;static bool fdLockReady=false;
static volatile LONG fdFix=0,fdChanged=0,fdGap[6];
__declspec(thread) static DWORD tlObj=0;__declspec(thread) static float tlPrev[3],tlS,tlS0,tlGapMs;__declspec(thread) static LONG tlGapFr;
static DWORD __stdcall FollowNote(DWORD self,DWORD obj,DWORD sBits){
 float sv=*(float*)&sBits;LARGE_INTEGER q;QueryPerformanceCounter(&q);LONG f=frameIndex;double gap=-1;LONG gfr=-1;
 if(fdLockReady&&frequency){EnterCriticalSection(&fdLock);UINT h=(obj>>4)*2654435761u>>21;
  for(int j=0;j<32;j++){FdObj& e=fdObjs[(h+j)&2047];if(e.obj==obj||!e.obj){if(e.obj==obj){gap=(double)(q.QuadPart-e.qpc)/frequency;gfr=f-e.frame;}e.obj=obj;e.qpc=q.QuadPart;e.frame=f;break;}}
  LeaveCriticalSection(&fdLock);}
 float out=sv;   // v11 entry fix removed: objects are updated every frame (gap 1 frame in 99%), see v12
 if(rgOn){InterlockedIncrement(&fdN);fdLastSBits=sBits;{FdJob* jb=FdJobSlot();if(jb)InterlockedIncrement(&jb->calls);}
  InterlockedExchangeAdd64(&fdSSum,(LONGLONG)(sv*1000.0f));InterlockedExchangeAdd64(&fdSDt,(LONGLONG)(sv*lastFrameUs));   // s*frameDt in 1e-6
  if((LONG)sBits<fdSMinB)fdSMinB=sBits;if((LONG)sBits>fdSMaxB)fdSMaxB=sBits;
  if(fdLastFrame!=f){fdLastFrame=f;InterlockedIncrement(&fdFrames);}
  InterlockedIncrement(&fdGap[gfr<0?5:gfr==0?0:gfr==1?1:gfr==2?2:gfr<=5?3:4]);
  if(out<sv*0.95f)InterlockedIncrement(&fdChanged);
  tlObj=obj;tlS=out;tlS0=sv;tlGapMs=(float)(gap*1000.0);tlGapFr=gfr;
  __try{memcpy(tlPrev,(void*)(obj+0x10),12);}__except(EXCEPTION_EXECUTE_HANDLER){tlObj=0;}}
 return *(DWORD*)&out;
}
// v12: the TARGET (bone pose x pivot) of most driven bodies does not change every frame; when it
// does, the jump since its last change is still multiplied by 1/(this frame). Probe after the
// target is computed (0x5fdf4f): compare with the previous target [obj+0x10]; if it moved, the
// real time since it last moved is known. Fix (lab condition): s = 1/(that time), never above the
// game's s. Histogram: frames between target changes.
// v13 (v12 showed the target DOES move every frame, sometimes 0.3 m in one frame): the velocity
// set in a frame is kept by Havok for the whole next 30 Hz step, so only the LAST frame before a
// step counts, and it was computed for one 7 ms frame -> the body moves 33/7 = 4.7x the target's
// last-frame motion (2x at 60 FPS, exact at 30 FPS = the original game). Fix (lab condition):
// measure the target motion from its position at the last physics step and divide by the real
// time since then (= the 30 FPS behaviour at any frame rate). First update or pause >0.25 s:
// motion counts from now (a stale target never becomes a launch).
static volatile LONG hkSteps=0;
// v53: physics tick (one per Havok step, the 30 Hz world the ship sails are computed in) for the cloth gateway
static volatile LONG hkTick=0;static volatile LONGLONG hkTickQpc=0;
static void HkTickNote(){LARGE_INTEGER q;QueryPerformanceCounter(&q);if(q.QuadPart-hkTickQpc>frequency/200){hkTickQpc=q.QuadPart;InterlockedIncrement(&hkTick);}}
struct FtObj{DWORD obj;LONGLONG qpc;LONG frame,step;float base[3];LONGLONG baseQpc;};static FtObj ftObjs[2048];
static volatile LONG ftStill=0,ftMoved=0,ftHist[7],ftTeleports=0;
// v21: who takes a corpse out of the physics world. v20: a ragdoll part lives in the world 2-8 s,
// then becomes keyframed (class 1) and is removed (rb+8 = 0, no island). Probe on Havok's
// per-entity removal 0xc5a8e0 (world, entity): for layer-6 bodies (ragdoll parts) the call chain
// (frame-pointer walk) is logged, each distinct chain once, with counts per phase.
static volatile LONG rmL6=0,rmAll=0;struct RmChain{DWORD h;LONG n;};static RmChain rmCh[32];static int rmChN=0;
static void __stdcall RemoveNote(DWORD ebpv,DWORD ret,DWORD ent){
 if(!rgOn)return;InterlockedIncrement(&rmAll);
 DWORD info=0;int c=-1;float sp=0;__try{info=*(DWORD*)(ent+0x2c);c=RgClass(*(DWORD*)(ent+0xe0));sp=RgLen((const float*)(ent+0xe0+0xd0));}__except(EXCEPTION_EXECUTE_HANDLER){return;}
 if((info&31)!=6)return;InterlockedIncrement(&rmL6);
 DWORD ch[14];int n=0;ch[n++]=ret;DWORD f=ebpv;
 __try{while(n<14&&RgPtr(f)){DWORD r=*(DWORD*)(f+4);if(r<0x401000||r>=0x1e3a000)break;ch[n++]=r;DWORD nf=*(DWORD*)f;if(nf<=f)break;f=nf;}}__except(EXCEPTION_EXECUTE_HANDLER){}
 DWORD h=2166136261u;for(int i=0;i<n;i++)h=(h^ch[i])*16777619u;
 if(!fdLockReady)return;EnterCriticalSection(&fdLock);int j=0;for(;j<rmChN;j++)if(rmCh[j].h==h){rmCh[j].n++;break;}
 bool fresh=j==rmChN&&rmChN<32;if(fresh){rmCh[rmChN].h=h;rmCh[rmChN].n=1;rmChN++;}LeaveCriticalSection(&fdLock);
 if(fresh){char b[400];int k=sprintf_s(b,"RAG REMOVE chain #%d: body %08lx owner %08lx class %d speed %.2f m/s | callers:",j,ent,*(DWORD*)(ent+0xc),c,sp);
  for(int i=0;i<n;i++)k+=sprintf_s(b+k,sizeof(b)-k," %08lx",ch[i]);Log(b);}
}
// v23: who switches ragdoll parts between dynamic and keyframed. hkpRigidBody::setMotionType 0xc4e4c0
// (thiscall rb, type, activation, filter update; ret 0xc). Havok motion types: 1 dynamic, 3 box
// inertia (ragdoll parts while alive physics), 4 keyframed, 5 fixed. For layer-6 bodies the call
// chain is logged once per distinct chain+type; counts per phase.
static volatile LONG mtN=0;struct MtChain{DWORD h;LONG n;int type;};static MtChain mtCh[40];static int mtChN=0;
// v34: ragdoll parts get collision quality DEBRIS (2) - Havok's hkpRigidBody ctor (0xc4f4fa: dynamic ->
// 2, keyframed -> 1, fixed -> 0) - and DEBRIS has no continuous (TOI) collision: a hand at 10-30 m/s moves
// 0.3-1 m per 30 Hz step and can pass through a wall or fence. When a layer-6 body (ragdoll part) is made
// dynamic, its quality becomes MOVING (4: TOI against fixed and keyframed bodies). Written to rb+0x2A and to
// the stored quality rb+0x1FC that Havok copies back when the motion type changes (0xc5b8e8).
static int corpseQuality=-1;static volatile LONG qualN=0,qualSeen[16];
static void __stdcall MotionNote(DWORD ebpv,DWORD rb,DWORD ret,DWORD type){
 // v42: a ragdoll part made dynamic is protected against solver pushes for its first seconds (before the
 // corpse lands and joins the kept table): two NPCs dying into each other now collide (CorpseCollide).
 if(keepPhys&&(type&0xff)==1){__try{DWORD inf=*(DWORD*)(rb+0x2c);if((inf&31)==6){LONG i=InterlockedIncrement(&freshI)&511;freshTab[i].t=QpcNow();freshTab[i].rb=rb;
   DWORD g=(inf>>16)&0x3ff;if(g){DWORD now=GetTickCount()|1,t=grpDeadMs[g];if(!t||now-t>6000)grpDeadMs[g]=now;}}}__except(EXCEPTION_EXECUTE_HANDLER){}}
 if(corpseQuality>=0&&(type&0xff)==1){__try{DWORD inf=*(DWORD*)(rb+0x2c);if((inf&31)==6&&((inf>>16)&0x3ff)!=0){   /* v41: ragdoll parts only (own group); items (group 0, e.g. dropped sabres) keep DEBRIS */BYTE q=*(BYTE*)(rb+0x2a);if(q<16)InterlockedIncrement(&qualSeen[q]);
   if(q!=(BYTE)corpseQuality&&q!=0&&q!=1){*(BYTE*)(rb+0x2a)=(BYTE)corpseQuality;*(BYTE*)(rb+0x1fc)=(BYTE)corpseQuality;
    LONG k=InterlockedIncrement(&qualN);if(k<=6||k==100||k==1000){char b[160];sprintf_s(b,"Corpses: ragdoll part %08lx made dynamic - collision quality %d -> %d (continuous collision) (#%ld)",rb,q,corpseQuality,k);Log(b);}}}}__except(EXCEPTION_EXECUTE_HANDLER){}}
 if(!rgOn)return;type&=0xff;
 DWORD info=0;int c=-1;float sp=0;__try{info=*(DWORD*)(rb+0x2c);c=RgClass(*(DWORD*)(rb+0xe0));sp=RgLen((const float*)(rb+0xe0+0xd0));}__except(EXCEPTION_EXECUTE_HANDLER){return;}
 if((info&31)!=6)return;InterlockedIncrement(&mtN);
 DWORD ch[14];int n=0;ch[n++]=ret;DWORD f=ebpv;
 __try{while(n<14&&RgPtr(f)){DWORD r=*(DWORD*)(f+4);if(r<0x401000||r>=0x1e3a000)break;ch[n++]=r;DWORD nf=*(DWORD*)f;if(nf<=f)break;f=nf;}}__except(EXCEPTION_EXECUTE_HANDLER){}
 DWORD h=2166136261u^type;for(int i=0;i<n;i++)h=(h^ch[i])*16777619u;
 if(!fdLockReady)return;EnterCriticalSection(&fdLock);int j=0;for(;j<mtChN;j++)if(mtCh[j].h==h){mtCh[j].n++;break;}
 bool fresh=j==mtChN&&mtChN<40;if(fresh){mtCh[mtChN].h=h;mtCh[mtChN].n=1;mtCh[mtChN].type=type;mtChN++;}LeaveCriticalSection(&fdLock);
 if(fresh){char b[420];int k=sprintf_s(b,"RAG MOTION chain #%d: body %08lx owner %08lx class %d -> type %lu, speed %.2f m/s, in world %s | callers:",j,rb,*(DWORD*)(rb+0xc),c,type,sp,*(DWORD*)(rb+8)?"yes":"no");
  for(int i=0;i<n;i++)k+=sprintf_s(b+k,sizeof(b)-k," %08lx",ch[i]);Log(b);}
}
static void __stdcall FollowTarget(DWORD obj,const float* t,float* splat){
 float* prev=(float*)(obj+0x10);
 float dx=t[0]-prev[0],dy=t[1]-prev[1],dz=t[2]-prev[2];bool moved=dx*dx+dy*dy+dz*dz>=1e-10f;
 if(!fdLockReady||!frequency)return;
 LARGE_INTEGER q;QueryPerformanceCounter(&q);LONG f=frameIndex,st=hkSteps;double gap=-1;LONG gfr=-1;bool first=true;float base[3];LONGLONG bq=q.QuadPart;
 EnterCriticalSection(&fdLock);UINT h=(obj>>4)*2654435761u>>21;
 // v36: entries were never freed - after a few hundred deaths every probe chain was full and the fix
 // silently stopped (base left unset, first=true -> game behaviour). Now: the object's own entry if it
 // has one, else the first empty or stale (> 2 s unused) slot.
 {int own=-1,freeS=-1;for(int j=0;j<32;j++){FtObj& e=ftObjs[(h+j)&2047];if(e.obj==obj){own=j;break;}if(freeS<0&&(!e.obj||q.QuadPart-e.qpc>2*frequency))freeS=j;}
  if(own<0&&freeS>=0){FtObj& e=ftObjs[(h+freeS)&2047];e.obj=0;h+=freeS;} else if(own>=0)h+=own;}
 for(int j=0;j<1;j++){FtObj& e=ftObjs[(h+j)&2047];if(e.obj==obj||!e.obj){
  if(e.obj==obj){gap=(double)(q.QuadPart-e.qpc)/frequency;gfr=f-e.frame;first=gap>0.25;}
  if(first){e.obj=obj;memcpy(e.base,t,12);e.baseQpc=q.QuadPart;e.step=st;}
  else if(e.step!=st){memcpy(e.base,prev,12);e.baseQpc=e.qpc;e.step=st;}
  e.qpc=q.QuadPart;e.frame=f;memcpy(base,e.base,12);bq=e.baseQpc;break;}}
 LeaveCriticalSection(&fdLock);
 if(rgOn){if(!moved)InterlockedIncrement(&ftStill);else{InterlockedIncrement(&ftMoved);InterlockedIncrement(&ftHist[gfr<0||gap>0.25?6:gfr<=1?0:gfr>=6?5:gfr-1]);}}
 if(fdFix){
  if(first){memcpy(prev,t,12);
   if(rgOn){__try{DWORD bd=*(DWORD*)(obj+0x50);if(RgPtr(bd))RgTrackAdd(bd,*(DWORD*)(bd+0xc));}__except(EXCEPTION_EXECUTE_HANDLER){}}
   if(rgOn){static volatile LONG fs=0;if(InterlockedIncrement(&fs)<=24){DWORD body=0,info=0;int c=-1;__try{body=*(DWORD*)(obj+0x50);info=*(DWORD*)(body+0x2c);c=RgClass(*(DWORD*)(body+0xe0));}__except(EXCEPTION_EXECUTE_HANDLER){}
    char b[200];sprintf_s(b,"RAG FILTER driven body %08lx class %d owner %08lx info %08lx = L%lu/G%lu/S%lu",body,c,body?*(DWORD*)(body+0xc):0,info,info&31,info>>16,(info>>5)&31);Log(b);}}
   return;}                  // no motion counted from a stale target
  double el=(double)(q.QuadPart-bq)/frequency;if(el<0.0005)return;
  // Teleport guard (lab-only, under review): a target that jumps faster than 20 m/s (sync-kill
  // alignment, pose snap) is moved, not followed. Every use is logged with job, body and jump size.
  {float ex=t[0]-base[0],ey=t[1]-base[1],ez=t[2]-base[2];float d2=ex*ex+ey*ey+ez*ez;
   if(rgOn&&d2>400.0*el*el){FdJob* jb=FdJobSlot();if(jb)InterlockedIncrement(&jb->tele);static volatile LONG tn=0;static LONG tg=-1;
    if(tg!=rgDriveGen){tg=rgDriveGen;tn=0;}
    if(InterlockedIncrement(&tn)<=12){DWORD body=0,cls=0;__try{body=*(DWORD*)(obj+0x50);cls=RgClass(*(DWORD*)(body+0xe0));}__except(EXCEPTION_EXECUTE_HANDLER){}
     char b[300];sprintf_s(b,"RAG TELEPORT %s: job %s, follow obj %08lx, body %08lx class %ld at %.1f %.1f %.1f, target jumped %.2f m in %.1f ms (= %.0f m/s)",tpGuard?"skipped":"followed (guard off)",tlJob?tlJob:"(no job)",obj,body,cls,t[0],t[1],t[2],sqrtf(d2),el*1000,sqrtf(d2)/el);Log(b);}}
   if(tpGuard&&d2>400.0*el*el){
   memcpy(prev,t,12);EnterCriticalSection(&fdLock);for(int j=0;j<32;j++){FtObj& e=ftObjs[(h+j)&2047];if(e.obj==obj){memcpy(e.base,t,12);e.baseQpc=q.QuadPart;break;}}LeaveCriticalSection(&fdLock);
   if(rgOn)InterlockedIncrement(&ftTeleports);return;}}
  memcpy(prev,base,12);float sg=splat[0],sn=(float)(1.0/el);
  if(sn<sg*0.95f){splat[0]=splat[1]=splat[2]=splat[3]=sn;if(rgOn)InterlockedIncrement(&fdChanged);}}
}
static DWORD ftBack=0x5fdf5a;
__declspec(naked) static void FollowTargetProbe(){__asm{
 pushad
 sub esp,128
 movups [esp],xmm0
 movups [esp+16],xmm1
 movups [esp+32],xmm2
 movups [esp+48],xmm3
 movups [esp+64],xmm4
 movups [esp+80],xmm5
 movups [esp+96],xmm6
 movups [esp+112],xmm7
 lea eax,[ebp-0x10]
 push eax
 lea eax,[esp+4]
 push eax
 push esi
 call FollowTarget
 movups xmm0,[esp]
 movups xmm1,[esp+16]
 movups xmm2,[esp+32]
 movups xmm3,[esp+48]
 movups xmm4,[esp+64]
 movups xmm5,[esp+80]
 movups xmm6,[esp+96]
 movups xmm7,[esp+112]
 add esp,128
 popad
 movaps xmm4,xmmword ptr [esi+0x30]
 mov eax,0x1e56780
 movaps xmm6,xmmword ptr [eax]
 jmp dword ptr [ftBack]
}}
static volatile LONG fvN=0,fvBig=0,fwBig=0,rgDriveN=0;static volatile LONGLONG fvSum=0,fwSum=0;static volatile LONG fvMax=0,fwMax=0;
// hkpMotion setters, per caller
static volatile LONG nanGuard=0;
struct RgCaller{volatile LONG ret,kind,n,cls;volatile LONG maxMm;};static RgCaller rgCallers[64];
static void __stdcall RgVelNote(DWORD kind,DWORD motion,DWORD ret,const float* vec){
 if(!rgOn&&!nanGuard)return;
 float m=0;int cls=-1;
 __try{m=RgLen(vec);cls=RgClass(*(DWORD*)motion);if(kind==2)m*=((const float*)(motion+0xc0))[3];}__except(EXCEPTION_EXECUTE_HANDLER){return;}
 if(!rgOn){if(!RgFinite(m)){static volatile LONG ng=0;if(InterlockedIncrement(&ng)<=30){char b[200];sprintf_s(b,"NAN GUARD: %s non-finite (%s) from %08lx on motion %08lx (class %d); replaced by zero",kind==0?"velocity":kind==1?"spin":"impulse",m!=m?"NaN":"inf",ret,motion,cls);Log(b);}
   __try{float* v=(float*)vec;v[0]=v[1]=v[2]=0;}__except(EXCEPTION_EXECUTE_HANDLER){}}return;}
 LONG mm=(LONG)(m*1000.0f);
 {LONG k=InterlockedIncrement(&rgRingI)&4095;RgSet& e=rgRing[k];e.motion=motion;e.ret=ret;e.m=m;e.frame=frameIndex;e.kind=kind;}
 bool fol=(ret==0x5fe0eb||ret==0x5fe71c);
 if(fol){if(ret==0x5fe0eb){InterlockedIncrement(&fvN);InterlockedExchangeAdd64(&fvSum,mm);if(mm>fvMax)fvMax=mm;if(m>8.0f)InterlockedIncrement(&fvBig);}
  else{InterlockedExchangeAdd64(&fwSum,mm);if(mm>fwMax)fwMax=mm;if(m>20.0f)InterlockedIncrement(&fwBig);}}
 if(!RgFinite(m)){static volatile LONG nf=0;if(InterlockedIncrement(&nf)<=30){char b[200];sprintf_s(b,"RAG NAN GUARD: %s non-finite (%s) from %08lx on motion %08lx (class %d); replaced by zero",kind==0?"velocity":kind==1?"spin":"impulse",m!=m?"NaN":"inf",ret,motion,cls);Log(b);}
  __try{float* v=(float*)vec;v[0]=v[1]=v[2]=0;}__except(EXCEPTION_EXECUTE_HANDLER){}return;}
 bool capOk=false;
 if(cls>=4&&kind<2&&m>(kind?20.0f:8.0f)){static volatile LONG cr[16],cn[16];static LONG seen=-1;if(rgDriveGen!=seen){seen=rgDriveGen;for(int i=0;i<16;i++)cn[i]=0;}   // 5 lines per caller per phase (only big sets count)
  for(int i=0;i<16;i++){if(cr[i]==(LONG)ret){capOk=InterlockedIncrement(&cn[i])<=5;break;}if(!cr[i]&&InterlockedCompareExchange(&cr[i],(LONG)ret,0)==0){capOk=InterlockedIncrement(&cn[i])<=5;break;}}}
 if(capOk){char b[400];float before=0;__try{before=RgLen((const float*)(motion+(kind?0xe0:0xd0)));}__except(EXCEPTION_EXECUTE_HANDLER){}
  int n=sprintf_s(b,"RAG DRIVE: %s %.1f %s (was %.1f) set by %08lx on motion %08lx (class %d), frame %.2f ms",kind?"spin":"velocity",m,kind?"rad/s":"m/s",before,ret,motion,cls,lastFrameUs/1000.0);
  if(fol&&tlObj){float d=0;__try{d=RgDist((const float*)(tlObj+0x10),tlPrev);}__except(EXCEPTION_EXECUTE_HANDLER){}
   n+=sprintf_s(b+n,sizeof(b)-n," | follow obj %08lx: target moved %.3f m since its previous update %s%.1f ms / %ld frames ago, s game %.1f used %.1f%s",tlObj,d,tlGapFr<0?"(first seen) ":"",tlGapFr<0?0.0f:tlGapMs,tlGapFr,tlS0,tlS,fdFix?" [FIX]":"");}
  Log(b);}
 for(int i=0;i<64;i++){RgCaller& c=rgCallers[i];
  if(c.ret==(LONG)ret&&c.kind==(LONG)kind&&c.cls==cls){InterlockedIncrement(&c.n);if(mm>c.maxMm)c.maxMm=mm;break;}
  if(!c.ret&&InterlockedCompareExchange(&c.ret,-1,0)==0){c.kind=kind;c.cls=cls;c.maxMm=mm;c.n=1;InterlockedExchange(&c.ret,(LONG)ret);break;}}
}
static BYTE* nanTramp=0;
static bool RgProbe(DWORD va,int len,const BYTE* expect,DWORD kind,BYTE* t){
 if(*(BYTE*)va==0xe9&&nanTramp&&(DWORD)(va+5+*(int*)(va+1))>=(DWORD)nanTramp&&(DWORD)(va+5+*(int*)(va+1))<(DWORD)nanTramp+4096)return true;   // already in (NaN guard)
 if(memcmp((void*)va,expect,len))return false;
 int k=0;t[k++]=0x60;                                                     // pushad
 t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;                          // push [esp+36]  (vector)
 t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;                          // push [esp+36]  (return address)
 t[k++]=0x51;                                                             // push ecx       (motion)
 t[k++]=0x68;*(DWORD*)(t+k)=kind;k+=4;                                    // push kind
 t[k++]=0xe8;*(DWORD*)(t+k)=(DWORD)RgVelNote-(DWORD)(t+k+4);k+=4;         // call RgVelNote
 t[k++]=0x61;                                                             // popad
 memcpy(t+k,(void*)va,len);k+=len;
 t[k++]=0xe9;*(DWORD*)(t+k)=(va+len)-(DWORD)(t+k+4);k+=4;
 BYTE code[16];memset(code,0x90,16);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)t-(va+5);
 DWORD old;if(!VirtualProtect((void*)va,len,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)va,code,len);FlushInstructionCache(GetCurrentProcess(),(void*)va,len);DWORD ig;VirtualProtect((void*)va,len,old,&ig);return true;
}
// Permanent fix ([Physics] RagdollFollowFix=1): the target hook below + a Havok step counter.
static bool ragFixQueued=false;static StepFn origMini[3];
static void CorpseTick();static void CorpsePre();static void CorpsePost();
static int __fastcall MiniStep0(void* s,void*,float dt){InterlockedIncrement(&hkSteps);HkTickNote();CorpsePre();int r=origMini[0](s,dt);CorpsePost();CorpseTick();return r;}
static int __fastcall MiniStep1(void* s,void*,float dt){InterlockedIncrement(&hkSteps);HkTickNote();CorpsePre();int r=origMini[1](s,dt);CorpsePost();CorpseTick();return r;}
static int __fastcall MiniStep2(void* s,void*,float dt){InterlockedIncrement(&hkSteps);HkTickNote();CorpsePre();int r=origMini[2](s,dt);CorpsePost();CorpseTick();return r;}
static bool SwapSlot(DWORD slot,void* hook,void** orig,DWORD expected);
static bool Jump(DWORD va,const BYTE* expected,unsigned size,void* fn);
static const BYTE ftIn[]={0x0f,0x28,0x66,0x30,0x0f,0x28,0x35,0x80,0x67,0xe5,0x01};   // movaps xmm4,[esi+30h]; movaps xmm6,[01E56780h]
static bool QueueRagdollFollowFix(bool diag){
 if(!fdLockReady){InitializeCriticalSection(&fdLock);fdLockReady=true;}
 if(!Jump(0x5fdf4f,ftIn,11,FollowTargetProbe))return false;
 if(!diag){bool ok=SwapSlot(0x25db8dc+0x10,(void*)MiniStep0,(void**)&origMini[0],0xc4b560);
  ok=SwapSlot(0x25dba5c+0x10,(void*)MiniStep1,(void**)&origMini[1],0xc4b560)&&ok;ok=SwapSlot(0x25dbb94+0x10,(void*)MiniStep2,(void**)&origMini[2],0xc4b560)&&ok;
  if(!ok)return false;}                         // with Diagnostics=1 the diagnostic step hooks count the steps
 ragFixQueued=true;InterlockedExchange(&fdFix,1);return true;
}
// v22 experiment (Lab.ini [Experiment] KeepCorpses=1, lab only): keep corpses in the physics world.
// v21: ragdoll parts leave the world in the game's physics sync 0x451380: for every physics object G
// whose activation counter [G+0x1c0] dropped to 0 while it is in the world (0x49f260), the game
// queues G for removeEntityBatch (+ removes its constraints). G = one ragdoll: [G+0x38] = body slots
// (0x60 each, hkpRigidBody* at +0x50), WORD [G+0x3e] = count. Here a ragdoll G (first body in
// collision layer 6) is left in the world; the matching add-side setup (0x49f150) is also skipped by
// the game later, because the object is still in the world. Bytes 0x4514fc: test al,al; je 0x4515b7.
static volatile LONG keepCorpses=0,keepN=0;static DWORD keepRes=0,kp504=0x451504,kp5b7=0x4515b7;
static DWORD __stdcall KeepCheck(DWORD g){
 if(!keepCorpses)return 0;
 __try{int n=*(WORD*)(g+0x3e);if(n<=0)return 0;DWORD rb=*(DWORD*)(*(DWORD*)(g+0x38)+0x50);if(!RgPtr(rb)||!*(DWORD*)(rb+8))return 0;
  DWORD info=*(DWORD*)(rb+0x2c);if((info&31)!=6)return 0;
  static DWORD seen[256];static int seenI=0;for(int i=0;i<256;i++)if(seen[i]==g)return 1;seen[seenI++&255]=g;   // count each ragdoll once
  if(InterlockedIncrement(&keepN)<=12){char b[200];sprintf_s(b,"RAG KEEP: ragdoll %08lx (%d bodies, first %08lx class %d owner %08lx) stays in the physics world",g,n,rb,RgClass(*(DWORD*)(rb+0xe0)),*(DWORD*)(rb+0xc));Log(b);}
  return 1;}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
__declspec(naked) static void KeepProbe(){__asm{
 test al,al
 jz keep_skip
 pushad
 push edi
 call KeepCheck
 test eax,eax
 popad
 jne keep_skip
 jmp dword ptr [kp504]
keep_skip:
 jmp dword ptr [kp5b7]
}}
static bool InstallKeepCorpses(){
 static const BYTE in[8]={0x84,0xc0,0x0f,0x84,0xb3,0x00,0x00,0x00};if(memcmp((void*)0x4514fc,in,8))return false;
 BYTE code[8];memset(code,0x90,8);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)KeepProbe-(0x4514fc+5);
 DWORD old;if(!VirtualProtect((void*)0x4514fc,8,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)0x4514fc,code,8);FlushInstructionCache(GetCurrentProcess(),(void*)0x4514fc,8);DWORD ig;VirtualProtect((void*)0x4514fc,8,old,&ig);return true;
}
// v24: who wakes a corpse up again. 0x4c84f0 = physics object (G) "activate physics": counter
// [G+0x1c0] goes up, bodies become dynamic again. Logged when G is a ragdoll (first body in layer 6)
// whose counter is 0 (already deactivated = a corpse), with the call chain, once per distinct chain.
static volatile LONG acN=0;struct AcChain{DWORD h;LONG n;};static AcChain acCh[24];static int acChN=0;
static void __stdcall ActivateNote(DWORD ebpv,DWORD g,DWORD ret){
 if(!rgOn)return;DWORD rb=0;int cnt=0;
 __try{if(*(WORD*)(g+0x3e)==0)return;rb=*(DWORD*)(*(DWORD*)(g+0x38)+0x50);if(!RgPtr(rb)||(*(DWORD*)(rb+0x2c)&31)!=6)return;cnt=*(int*)(g+0x1c0);}__except(EXCEPTION_EXECUTE_HANDLER){return;}
 if(cnt>0)return;InterlockedIncrement(&acN);
 DWORD ch[14];int n=0;ch[n++]=ret;DWORD f=ebpv;
 __try{while(n<14&&RgPtr(f)){DWORD r=*(DWORD*)(f+4);if(r<0x401000||r>=0x1e3a000)break;ch[n++]=r;DWORD nf=*(DWORD*)f;if(nf<=f)break;f=nf;}}__except(EXCEPTION_EXECUTE_HANDLER){}
 DWORD h=2166136261u;for(int i=0;i<n;i++)h=(h^ch[i])*16777619u;
 if(!fdLockReady)return;EnterCriticalSection(&fdLock);int j=0;for(;j<acChN;j++)if(acCh[j].h==h){acCh[j].n++;break;}
 bool fresh=j==acChN&&acChN<24;if(fresh){acCh[acChN].h=h;acCh[acChN].n=1;acChN++;}LeaveCriticalSection(&fdLock);
 if(fresh){char b[420];int k=0;__try{k=sprintf_s(b,"RAG WAKE chain #%d: ragdoll %08lx body %08lx class %d in world %s | callers:",j,g,rb,RgClass(*(DWORD*)(rb+0xe0)),*(DWORD*)(rb+8)?"yes":"no");}__except(EXCEPTION_EXECUTE_HANDLER){return;}
  for(int i=0;i<n;i++)k+=sprintf_s(b+k,sizeof(b)-k," %08lx",ch[i]);Log(b);}
}
static DWORD acBack=0x4c84f7;
__declspec(naked) static void ActivateProbe(){__asm{
 pushad
 push dword ptr [esp+32]
 push ecx
 push ebp
 call ActivateNote
 popad
 push ebp
 mov ebp,esp
 push ecx
 push esi
 mov esi,ecx
 jmp dword ptr [acBack]
}}
static bool InstallActivateProbe(){
 static const BYTE in[7]={0x55,0x8b,0xec,0x51,0x56,0x8b,0xf1};if(memcmp((void*)0x4c84f0,in,7))return false;
 BYTE code[7];memset(code,0x90,7);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)ActivateProbe-(0x4c84f0+5);
 DWORD old;if(!VirtualProtect((void*)0x4c84f0,7,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)0x4c84f0,code,7);FlushInstructionCache(GetCurrentProcess(),(void*)0x4c84f0,7);DWORD ig;VirtualProtect((void*)0x4c84f0,7,old,&ig);return true;
}
// v25: the game's own collision filter (vtable 0x1e71ef8, decision 0x7cc8b0(infoA, infoB)). Its
// filter info is NOT Havok's group-filter layout: layer = bits 0-5 (64 layers, 64-bit masks in the
// constant table 0x1e70180), sub id = bits 6-9 (same-group table 0x1e702f8), id = bits 10-15,
// system group = bits 16-25. Two bodies with DIFFERENT non-zero system groups never collide; the
// layer table is not even asked. Every ragdoll gets its own group (1, 2, 3 ...), so corpses of
// different NPCs pass through each other although the table allows layer 6 with layer 6.
// CorpseCollide: for two layer-6 bodies (ragdoll parts) of different groups the layer table decides.
// Patch 0x7cc8e1 (8 bytes: test ecx,ecx; je 0x7cc910; cmp eax,ecx; jne 0x7cc926).
static volatile LONG corpseCollide=0,ccHits=0;
static DWORD cc910=0x7cc910,cc8e9=0x7cc8e9,cc926=0x7cc926,cc92d=0x7cc92d;
// v49 (replaces the v46 rule "only when both lie still", which let falling bodies sink into each other and
// then pushed the already merged bodies apart = somersaults): two ragdoll parts of DIFFERENT dead NPCs
// collide from the moment of death, except pairs that were already overlapping when one of them became a
// ragdoll (Havok asks the filter again for all touching pairs in setMotionType): those were "born inside
// each other" (NPCs killed together, v45 explosions) and never collide - as in the original game. Living
// NPCs (keyframed parts of a group without a death time) are not touched.
static int __stdcall CollideDecide(DWORD a,DWORD b){
 if((a&0x3f)!=6||(b&0x3f)!=6)return 0;
 DWORD ga=(a>>16)&0x3ff,gb=(b>>16)&0x3ff;if(ga>gb){DWORD x=ga;ga=gb;gb=x;}
 DWORD now=GetTickCount(),ta=grpDeadMs[ga],tb=grpDeadMs[gb];
 bool da=(ta&&now-ta<6000)||(keptGroups[ga>>5]>>(ga&31)&1),db=(tb&&now-tb<6000)||(keptGroups[gb>>5]>>(gb&31)&1);
 if(!da||!db)return 0;
 DWORD key=ga<<16|gb;
 for(int i=0;i<256;i++){const RejPair& e=rejTab[i];if(e.key==key&&e.ta==ta&&e.tb==tb)return 0;}
 if((ta&&now-ta<100)||(tb&&now-tb<100)){
  RejPair& e=rejTab[InterlockedIncrement(&rejI)&255];e.key=0;e.ta=ta;e.tb=tb;e.key=key;InterlockedIncrement(&ccBorn);return 0;}
 InterlockedIncrement(&ccHits);return 1;
}
__declspec(naked) static void CorpseCollideProbe(){__asm{
 test ecx,ecx
 je cc_g0
 cmp eax,ecx
 je cc_same
 cmp corpseCollide,0
 je cc_no
 push edx
 push edx
 push esi
 call CollideDecide
 pop edx
 test eax,eax
 je cc_no
 jmp dword ptr [cc92d]
cc_g0:
 jmp dword ptr [cc910]
cc_same:
 jmp dword ptr [cc8e9]
cc_no:
 jmp dword ptr [cc926]
}}
static bool InstallCorpseCollide(){
 if(*(BYTE*)0x7cc8e1==0xe9&&*(DWORD*)0x7cc8e2==(DWORD)CorpseCollideProbe-(0x7cc8e1+5))return true;   // already in
 static const BYTE in[8]={0x85,0xc9,0x74,0x2b,0x3b,0xc1,0x75,0x3d};if(memcmp((void*)0x7cc8e1,in,8))return false;
 BYTE code[8];memset(code,0x90,8);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)CorpseCollideProbe-(0x7cc8e1+5);
 DWORD old;if(!VirtualProtect((void*)0x7cc8e1,8,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)0x7cc8e1,code,8);FlushInstructionCache(GetCurrentProcess(),(void*)0x7cc8e1,8);DWORD ig;VirtualProtect((void*)0x7cc8e1,8,old,&ig);return true;
}
// v25: how long the game keeps a corpse in physics. 0x4c87e0(G, target weight, duration, flag) fades
// the physics weight [G+0x1C4] to target over duration; at |weight| <= 0.0005 the game switches the
// physics off (0x4cae80) and the corpse leaves the world. Logged for ragdolls (first body layer 6).
static volatile LONG fdeN=0,keepPhysN=0;struct FdeChain{DWORD h;LONG n;float tmin,tmax,dmin,dmax;};static FdeChain fdeCh[24];static int fdeChN=0;
static bool KeptReleased(DWORD g);
static DWORD __stdcall FadeNote(DWORD ebpv,DWORD g,DWORD ret,float target,float dur){
 if(!rgOn&&!keepPhys)return 0;int cls=-1,cnt=0;
 __try{if(*(WORD*)(g+0x3e)==0)return 0;DWORD rb=*(DWORD*)(*(DWORD*)(g+0x38)+0x50);if(!RgPtr(rb)||(*(DWORD*)(rb+0x2c)&31)!=6)return 0;cls=RgClass(*(DWORD*)(rb+0xe0));cnt=*(int*)(g+0x1c0);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
 if(rgOn)InterlockedIncrement(&fdeN);
 DWORD ch[10];int n=0;ch[n++]=ret;DWORD f=ebpv;
 __try{while(n<10&&RgPtr(f)){DWORD r=*(DWORD*)(f+4);if(r<0x401000||r>=0x1e3a000)break;ch[n++]=r;DWORD nf=*(DWORD*)f;if(nf<=f)break;f=nf;}}__except(EXCEPTION_EXECUTE_HANDLER){}
 // v26 KeepPhysics: the slow fade-out of a lying ragdoll (target 0, >= 0.5 s, bodies still dynamic,
 // physics on) is not executed - the corpse stays a full ragdoll; Havok puts it to sleep when it is
 // still and wakes it when something hits it. Short fade-outs (pick-up: 0-0.3 s) are left alone.
 // v28: only the dead NPC's fade (1.0 s, called from 0x12a1a92 via 0x4c8930) - a living NPC that leaves
 // its ragdoll is never held.
 DWORD skip=keepPhys&&target<0.01f&&dur>=0.5f&&cls==5&&cnt>0&&n>1&&ch[1]==0x12a1a92;
 if(skip){LONG k=InterlockedIncrement(&keepPhysN);if(k<=12){char b[200];sprintf_s(b,"RAG KEEPPHYS: fade-out of ragdoll %08lx over %.2f s skipped - it stays in physics (#%ld)",g,dur,k);Log(b);}}
 if(skip&&KeptReleased(g))skip=0;   // v35: given back -> let the game fade it
 if(!rgOn)return skip;
 DWORD h=2166136261u;for(int i=0;i<n;i++)h=(h^ch[i])*16777619u;
 if(!fdLockReady)return skip;EnterCriticalSection(&fdLock);int j=0;for(;j<fdeChN;j++)if(fdeCh[j].h==h){FdeChain& c=fdeCh[j];c.n++;if(target<c.tmin)c.tmin=target;if(target>c.tmax)c.tmax=target;if(dur<c.dmin)c.dmin=dur;if(dur>c.dmax)c.dmax=dur;break;}
 bool fresh=j==fdeChN&&fdeChN<24;if(fresh){FdeChain& c=fdeCh[fdeChN++];c.h=h;c.n=1;c.tmin=c.tmax=target;c.dmin=c.dmax=dur;}LeaveCriticalSection(&fdLock);
 if(fresh){char b[400];int k=sprintf_s(b,"RAG FADE chain #%d: ragdoll %08lx weight %.3f -> %.3f over %.2f s | callers:",j,g,*(float*)(g+0x1c4),target,dur);
  for(int i=0;i<n;i++)k+=sprintf_s(b+k,sizeof(b)-k," %08lx",ch[i]);Log(b);}
 return skip;
}
static DWORD fdeBack=0x4c87e6,fdeSkip=0;
__declspec(naked) static void FadeProbe(){__asm{
 pushad
 push dword ptr [esp+40]
 push dword ptr [esp+40]
 push dword ptr [esp+40]
 push ecx
 push ebp
 call FadeNote
 test eax,eax
 popad
 jne fde_skip
 push ebp
 mov ebp,esp
 xorps xmm3,xmm3
 jmp dword ptr [fdeBack]
fde_skip:
 ret 0xc
}}
static bool InstallFadeProbe(){
 if(*(BYTE*)0x4c87e0==0xe9&&*(DWORD*)0x4c87e1==(DWORD)FadeProbe-(0x4c87e0+5))return true;   // already in (CorpsePhysics)
 static const BYTE in[6]={0x55,0x8b,0xec,0x0f,0x57,0xdb};if(memcmp((void*)0x4c87e0,in,6))return false;
 BYTE code[6];memset(code,0x90,6);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)FadeProbe-(0x4c87e0+5);
 DWORD old;if(!VirtualProtect((void*)0x4c87e0,6,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)0x4c87e0,code,6);FlushInstructionCache(GetCurrentProcess(),(void*)0x4c87e0,6);DWORD ig;VirtualProtect((void*)0x4c87e0,6,old,&ig);return true;
}
// v27: the second way a corpse loses its physics. The dead NPC's behaviour 0x1b42b50 (state machine
// 0x1b66180, case at 0x1b66201) on its first run stores 4 bone values and calls 0x4cb080(G): if the
// physics counter [G+0x1C0] > 0 -> 0x4cae80(1) = physics off AT ONCE (no fade) -> bodies kinematic ->
// removed from the world. Every corpse Edward carried and dropped went this way (v26 log: motion chain
// #4 from 0x1b66206), so the pile was pure animation and the bodies sank into each other.
// KeepPhysics also skips this call for ragdolls (first body layer 6, class 5, counter > 0).
// v29: the same behaviour is the corpse's "support sensor": it stored the height above ground of 4 bones
// ([beh+0x10C..0x118]) and every 4-16 frames compares them; a change > 0.1 m makes the dead NPC's AI
// "fall again" (state 0x1003) and the behaviour restarts. A corpse that keeps its physics moves a little,
// so the sensor kept firing (one body restarted 5 times in a row) -> twitches. A stored -1.0 means "do
// not check this bone" for the game, so for a kept corpse all 4 are set to -1: Havok holds the body up.
// v29: a kept corpse that never comes to rest (ship deck, rocking under it) is given back to the game:
// after CORPSE_MOVING_S of uninterrupted motion [beh+0x108] is cleared, the behaviour runs its first
// branch again on the game thread and this time 0x4cb080 is let through (frozen like the original game).
static const float CORPSE_MOVING_S=5.0f,CORPSE_STILL_MS=0.02f;
struct KeptCorpse{DWORD g,beh,owner,groupTag,groupHeld;float p[3];float moving;LONGLONG last,born,pushWin;LONG release,pushes;};
static int corpseLimit=20;   // v32: [Physics] CorpseLimit - at most this many corpses keep their physics (0 = no limit)
static KeptCorpse kept[64];static int keptN=0;static CRITICAL_SECTION keptLock;static bool keptLockReady=false;
#include "corpse_read_guard.h"
#include "corpse_group_ownership.h"
static void KeptGroup(KeptCorpse& c,bool on){
 if(on)corpse_groups::Acquire(c.groupTag,c.groupHeld,keptGroups);
 else corpse_groups::Release(c.groupTag,c.groupHeld,keptGroups);
}
static bool KeptReleased(DWORD g){if(!keptLockReady)return false;bool r=false;EnterCriticalSection(&keptLock);for(int j=0;j<keptN;j++)if(kept[j].g==g&&kept[j].release){r=true;break;}LeaveCriticalSection(&keptLock);return r;}
static volatile LONG keepOffN=0,keepRelN=0,keptBusy=0;static LONGLONG keptTick=0;
static bool KeptBody0(DWORD g,float* p){DWORD tag=0;return corpse_read::First(g,p,tag);}
static bool KeptBehOk(const KeptCorpse& c){   // the behaviour object is still this corpse's and still holds our -1 sentinel
 __try{if(*(DWORD*)c.beh!=c.owner||*(BYTE*)(c.beh+0x108)!=1)return false;for(int i=0;i<4;i++)if(*(DWORD*)(c.beh+0x10c+4*i)!=0xbf800000)return false;return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// called after every Havok step (any thread); does its work at most 10 times a second
static void CorpseTick(){
 if(!keepPhys||!keptLockReady||!keptN)return;LONGLONG now=QpcNow();if(now-keptTick<frequency/10)return;
 if(InterlockedCompareExchange(&keptBusy,1,0))return;keptTick=now;
 EnterCriticalSection(&keptLock);
 for(int i=0;i<keptN;i++){KeptCorpse& c=kept[i];float p[3];bool drop=false;
  if(c.release){if(now-c.last>10*frequency)drop=true;}   // v35: kept 10 s as 'given back' - the behaviour may run its first branch again
  else if(!KeptBody0(c.g,p))drop=true;
  else{float dt=(float)(now-c.last)/frequency;c.last=now;float dx=p[0]-c.p[0],dy=p[1]-c.p[1],dz=p[2]-c.p[2];memcpy(c.p,p,12);
   float v=dt>0?sqrtf(dx*dx+dy*dy+dz*dz)/dt:0;if(v<CORPSE_STILL_MS)c.moving=0;else c.moving+=dt;
   if(c.moving>=CORPSE_MOVING_S){if(KeptBehOk(c)){__try{*(BYTE*)(c.beh+0x108)=0;}__except(EXCEPTION_EXECUTE_HANDLER){}c.release=1;c.last=now;KeptGroup(c,false);
     LONG k=InterlockedIncrement(&keepRelN);if(k<=60){char b[200];sprintf_s(b,"Corpses: ragdoll %08lx never came to rest (%.1f s moving, last %.2f m/s, moving ground?) - given back to the game (#%ld)",c.g,c.moving,v,k);Log(b);}}
    else drop=true;}}
  if(drop){KeptGroup(kept[i],false);kept[i]=kept[--keptN];i--;}}
 // v32: limit. Over the limit, the corpse farthest from the newest one (= where the fight is, near
 // Edward) is given back to the game the same way (frozen as in the original game).
 for(int guard=0;corpseLimit>0&&guard<8;guard++){int act=0,newest=-1;
  for(int i=0;i<keptN;i++)if(!kept[i].release){act++;if(newest<0||kept[i].born>kept[newest].born)newest=i;}
  if(act<=corpseLimit||newest<0)break;
  int fi=-1;float fd=-1;for(int i=0;i<keptN;i++){if(kept[i].release||i==newest)continue;float dx=kept[i].p[0]-kept[newest].p[0],dy=kept[i].p[1]-kept[newest].p[1],dz=kept[i].p[2]-kept[newest].p[2],d=dx*dx+dy*dy+dz*dz;if(d>fd){fd=d;fi=i;}}
  if(fi<0)break;KeptCorpse& c=kept[fi];
  if(KeptBehOk(c)){__try{*(BYTE*)(c.beh+0x108)=0;}__except(EXCEPTION_EXECUTE_HANDLER){}c.release=1;c.last=now;KeptGroup(c,false);
   LONG k=InterlockedIncrement(&keepRelN);if(k<=60){char b[200];sprintf_s(b,"Corpses: more than %d corpses in physics - ragdoll %08lx (%.0f m from the newest) given back to the game (#%ld)",corpseLimit,c.g,sqrtf(fd),k);Log(b);}}
  else{KeptGroup(kept[fi],false);kept[fi]=kept[--keptN];}}
 LeaveCriticalSection(&keptLock);InterlockedExchange(&keptBusy,0);
}
// v30: launches of kept corpses. A corpse that keeps its physics lies at Edward's feet; a kinematic body
// (the sword being sheathed, a keyframed prop) sweeping through it is resolved by the solver with infinite
// mass -> the body is shot up (video 12.52.28.07_2: ~17 m/s, 3.5 s in the air). Velocities of kept corpse
// parts are read right before each Havok step and checked right after it: a speed that GREW inside the
// step by more than CORPSE_PUSH_MS is a solver push, it is limited to the speed before the step + 1 m/s.
// Speeds set by game code (explosions, hits) are set before the step and pass untouched; gravity adds
// only 0.33 m/s per step.
static const float CORPSE_PUSH_MS=3.0f,CORPSE_PUSH_RAD=12.0f,CORPSE_SPIN_MAX=30.0f;   // v35: corpse parts never spin faster than 30 rad/s
struct KeptPre{DWORD rb,g;float v,w;};static KeptPre keptPre[1024];static int keptPreN=0;static volatile LONG keptClampN=0;
static void CorpsePre(){
 keptPreN=0;if(!keepPhys||!keptLockReady)return;EnterCriticalSection(&keptLock);
 for(int i=0;i<keptN&&keptPreN<1024;i++){
  DWORD g=kept[i].g,bodies[40];unsigned n=0;
  if(!corpse_read::Group(g,bodies,n))continue;
  for(unsigned k=0;k<n&&keptPreN<1024;k++){
   float v=0,w=0;if(!corpse_read::Body(bodies[k],false,v,w))continue;
   KeptPre& p=keptPre[keptPreN++];p.rb=bodies[k];p.g=g;p.v=v;p.w=w;
  }
 }
 LeaveCriticalSection(&keptLock);
 LONGLONG now=QpcNow();
 for(int i=0;i<512&&keptPreN<1024;i++){
  FreshPart f=freshTab[i];if(!f.rb||now-f.t>4*frequency)continue;
  float v=0,w=0;if(!corpse_read::Body(f.rb,true,v,w))continue;
  bool dup=false;for(int k=0;k<keptPreN;k++)if(keptPre[k].rb==f.rb){dup=true;break;}if(dup)continue;
  KeptPre& p=keptPre[keptPreN++];p.rb=f.rb;p.g=0;p.v=v;p.w=w;
 }
}
// v33: a corpse pushed inside the step again and again is stuck in level geometry: the solver keeps
// trying to push it out and the limit above lets it out only slowly -> endless tumbling (2026-10-12 20:37:
// one corpse clamped 7 times in a row). CORPSE_STUCK pushes within 1 s -> the corpse is given back to the
// game at once (frozen as in the original game).
static const int CORPSE_STUCK=4;static volatile LONG keepStuckN=0;
static void CorpseStuck(DWORD g){
 LONGLONG now=QpcNow();EnterCriticalSection(&keptLock);
 for(int i=0;i<keptN;i++){KeptCorpse& c=kept[i];if(c.g!=g||c.release)continue;
  if(now-c.pushWin>frequency){c.pushWin=now;c.pushes=0;}
  if(++c.pushes>=CORPSE_STUCK&&KeptBehOk(c)){__try{*(BYTE*)(c.beh+0x108)=0;}__except(EXCEPTION_EXECUTE_HANDLER){}c.release=1;c.last=now;KeptGroup(c,false);
   // v38: stop it until the game freezes it - a body stuck in the level is otherwise shot out by the solver
   __try{int n=*(WORD*)(g+0x3e);DWORD cells=*(DWORD*)(g+0x38);if(n<=40&&RgPtr(cells))for(int k=0;k<n;k++){DWORD rb=*(DWORD*)(cells+k*0x60+0x50);
     if(RgPtr(rb)&&(*(DWORD*)(rb+0x2c)&31)==6&&RgClass(*(DWORD*)(rb+0xe0))==5){float* v=(float*)(rb+0xe0+0xd0);float* w=(float*)(rb+0xe0+0xe0);v[0]=v[1]=v[2]=0;w[0]=w[1]=w[2]=0;}}}__except(EXCEPTION_EXECUTE_HANDLER){}
   LONG k=InterlockedIncrement(&keepStuckN);if(k<=60){char b[200];sprintf_s(b,"Corpses: ragdoll %08lx pushed %ld times within 1 s - stuck in the level, given back to the game (#%ld)",g,c.pushes,k);Log(b);}}
  break;}
 LeaveCriticalSection(&keptLock);
}
static void CorpsePost(){
 DWORD pushed[8];int pn=0;
 for(int i=0;i<keptPreN;i++){KeptPre& p=keptPre[i];
  __try{{int pc=RgClass(*(DWORD*)(p.rb+0xe0));if(pc!=5&&pc!=4)continue;}float* v=(float*)(p.rb+0xe0+0xd0);float* w=(float*)(p.rb+0xe0+0xe0);float sv=RgLen(v),sw=RgLen(w);
   bool cv=RgFinite(sv)&&sv-p.v>CORPSE_PUSH_MS,cw=RgFinite(sw)&&sw-p.w>CORPSE_PUSH_RAD;
   if(cv){float k=(p.v+1.0f)/sv;v[0]*=k;v[1]*=k;v[2]*=k;}
   if(cw){float k=(p.w+3.0f)/sw;w[0]*=k;w[1]*=k;w[2]*=k;sw*=k;}
   if(RgFinite(sw)&&sw>CORPSE_SPIN_MAX){float k=CORPSE_SPIN_MAX/sw;w[0]*=k;w[1]*=k;w[2]*=k;}
   if(cv||cw){int j=0;for(;j<pn;j++)if(pushed[j]==p.g)break;if(p.g&&j==pn&&pn<8)pushed[pn++]=p.g;
    LONG c=InterlockedIncrement(&keptClampN);if(c<=40||c%50==0){char b[200];sprintf_s(b,"Corpses: %s part %08lx pushed inside a physics step %.1f -> %.1f m/s, spin %.1f -> %.1f rad/s - limited (#%ld)",p.g?"corpse":"falling ragdoll",p.rb,p.v,sv,p.w,sw,c);Log(b);}}
  }__except(EXCEPTION_EXECUTE_HANDLER){}}
 keptPreN=0;if(keptLockReady)for(int j=0;j<pn;j++)CorpseStuck(pushed[j]);
}
static DWORD __stdcall ImmOffNote(DWORD g,DWORD beh){
 if(!keepPhys)return 0;int cls=-1,cnt=0;
 __try{if(*(WORD*)(g+0x3e)==0)return 0;DWORD rb=*(DWORD*)(*(DWORD*)(g+0x38)+0x50);if(!RgPtr(rb)||(*(DWORD*)(rb+0x2c)&31)!=6)return 0;cls=RgClass(*(DWORD*)(rb+0xe0));cnt=*(int*)(g+0x1c0);}__except(EXCEPTION_EXECUTE_HANDLER){return 0;}
 if(cls!=5||cnt<=0)return 0;
 if(keptLockReady){EnterCriticalSection(&keptLock);int j=0;for(;j<keptN;j++)if(kept[j].g==g)break;
  if(j<keptN&&kept[j].release){float age=(float)(QpcNow()-kept[j].last)/frequency;LeaveCriticalSection(&keptLock);
   static volatile LONG tk=0;if(InterlockedIncrement(&tk)<=80){char b[160];sprintf_s(b,"Corpses: ragdoll %08lx taken back by the game %.2f s after it was given back",g,age);Log(b);}
   return 0;}   // given back: the game freezes it (v35: also on a repeated call)
  if(j==keptN&&keptN>=64){LeaveCriticalSection(&keptLock);return 0;}   // v36: table full -> frozen as in the game, never untracked
  if(j==keptN&&keptN<64){float position[3];DWORD groupTag=0;
   if(!corpse_read::First(g,position,groupTag)){LeaveCriticalSection(&keptLock);return 0;}
   KeptCorpse& c=kept[keptN++];memset(&c,0,sizeof(c));c.g=g;c.beh=beh;c.groupTag=groupTag;memcpy(c.p,position,12);c.last=c.born=QpcNow();KeptGroup(c,true);__try{c.owner=*(DWORD*)beh;}__except(EXCEPTION_EXECUTE_HANDLER){}}
  else if(j<keptN){kept[j].beh=beh;__try{kept[j].owner=*(DWORD*)beh;}__except(EXCEPTION_EXECUTE_HANDLER){}}
  LeaveCriticalSection(&keptLock);}
 __try{for(int i=0;i<4;i++)*(DWORD*)(beh+0x10c+4*i)=0xbf800000;}__except(EXCEPTION_EXECUTE_HANDLER){}   // support sensor off (-1.0)
 LONG k=InterlockedIncrement(&keepOffN);if(k<=12){char b[200];sprintf_s(b,"RAG KEEPPHYS: immediate physics-off of ragdoll %08lx (dead NPC behaviour 0x1b42b50) skipped - it stays in physics, support sensor off (#%ld)",g,k);Log(b);}
 return 1;
}
static DWORD immOffSkip=0,immOffGame=0x4cb080;
__declspec(naked) static void ImmOffProbe(){__asm{
 pushad
 push ebx
 push ecx
 call ImmOffNote
 test eax,eax
 popad
 jne imm_skip
 jmp dword ptr [immOffGame]
imm_skip:
 ret
}}
static bool InstallImmOffProbe(){
 BYTE* at=(BYTE*)0x1b42bc1;if(at[0]==0xe8&&(DWORD)(0x1b42bc6+*(int*)(at+1))==(DWORD)ImmOffProbe)return true;   // already in
 if(at[0]!=0xe8||(DWORD)(0x1b42bc6+*(int*)(at+1))!=0x4cb080)return false;
 BYTE code[5];code[0]=0xe8;*(DWORD*)(code+1)=(DWORD)ImmOffProbe-0x1b42bc6;
 DWORD old;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy(at,code,5);FlushInstructionCache(GetCurrentProcess(),at,5);DWORD ig;VirtualProtect(at,5,old,&ig);return true;
}
static void InstallRagLab(){
 InitializeCriticalSection(&rgLock);rgLockReady=true;if(!fdLockReady){InitializeCriticalSection(&fdLock);fdLockReady=true;}
 BYTE* tr=(BYTE*)VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!tr)return;
 static const BYTE setLin[]={0x55,0x8b,0xec,0x8b,0x45,0x08},addImp[]={0x55,0x8b,0xec,0x0f,0x28,0x81,0xc0,0x00,0x00,0x00};
 int ok=RgProbe(0xc41050,6,setLin,0,tr)+RgProbe(0xc41070,6,setLin,1,tr+64)+RgProbe(0xc41280,10,addImp,2,tr+128);
 // follow driver entry: push ebx; mov ebx,esp; sub esp,8
 static const BYTE fdIn[]={0x53,0x8b,0xdc,0x83,0xec,0x08};bool fd=false;
 if(!memcmp((void*)0x5fdec0,fdIn,6)){BYTE* t=tr+192;int k=0;t[k++]=0x60;
  t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x28;t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x28;t[k++]=0x51;   // push [esp+40] x2 (s, obj), push ecx
  t[k++]=0xe8;*(DWORD*)(t+k)=(DWORD)FollowNote-(DWORD)(t+k+4);k+=4;
  t[k++]=0x89;t[k++]=0x44;t[k++]=0x24;t[k++]=0x28;t[k++]=0x61;                                            // mov [esp+40],eax (s); popad
  memcpy(t+k,fdIn,6);k+=6;t[k++]=0xe9;*(DWORD*)(t+k)=0x5fdec6-(DWORD)(t+k+4);k+=4;
  fd=HookEntry(0x5fdec0,fdIn,t);}
 static const BYTE rmIn[]={0x55,0x8b,0xec,0x8b,0x45,0x0c};bool rm=false;
 if(!memcmp((void*)0xc5a8e0,rmIn,6)){BYTE* t=tr+320;int k=0;t[k++]=0x60;                                      // pushad
  t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x28;t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;t[k++]=0x55;   // push entity, push ret, push ebp
  t[k++]=0xe8;*(DWORD*)(t+k)=(DWORD)RemoveNote-(DWORD)(t+k+4);k+=4;t[k++]=0x61;                                  // call; popad
  memcpy(t+k,rmIn,6);k+=6;t[k++]=0xe9;*(DWORD*)(t+k)=0xc5a8e6-(DWORD)(t+k+4);k+=4;
  rm=HookEntry(0xc5a8e0,rmIn,t);}
 static const BYTE mtIn[]={0x55,0x8b,0xec,0x83,0xec,0x0c};bool mtp=false;
 if(*(BYTE*)0xc4e4c0==0xe9)mtp=true;   // v34: already hooked (CorpseCCD)
 else if(!memcmp((void*)0xc4e4c0,mtIn,6)){BYTE* t=tr+384;int k=0;t[k++]=0x60;                                       // pushad
  t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;t[k++]=0x51;t[k++]=0x55; // push type, push ret, push ecx(rb), push ebp
  t[k++]=0xe8;*(DWORD*)(t+k)=(DWORD)MotionNote-(DWORD)(t+k+4);k+=4;t[k++]=0x61;                                  // call; popad
  memcpy(t+k,mtIn,6);k+=6;t[k++]=0xe9;*(DWORD*)(t+k)=0xc4e4c6-(DWORD)(t+k+4);k+=4;
  mtp=HookEntry(0xc4e4c0,mtIn,t);}
 bool ft=ragFixQueued;
 if(!ft&&!memcmp((void*)0x5fdf4f,ftIn,11)){BYTE code[11];memset(code,0x90,11);code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)FollowTargetProbe-(0x5fdf4f+5);
  DWORD old;if(VirtualProtect((void*)0x5fdf4f,11,PAGE_EXECUTE_READWRITE,&old)){memcpy((void*)0x5fdf4f,code,11);FlushInstructionCache(GetCurrentProcess(),(void*)0x5fdf4f,11);DWORD ig;VirtualProtect((void*)0x5fdf4f,11,old,&ig);ft=true;}}
 char b[160];sprintf_s(b,"RAG: lab ready, %d of 3 motion setters probed, follow driver probe %s, target probe %s, removal probe %s",ok,fd?"on":"off",ft?"on":"off",rm?"on":"off");Log(mtp?"RAG: motion type probe on":"RAG: motion type probe off (code not recognised)");Log(InstallActivateProbe()?"RAG: corpse wake probe on":"RAG: corpse wake probe off (code not recognised)");Log(InstallFadeProbe()?"RAG: physics fade probe on":"RAG: physics fade probe off (code not recognised)");Log(InstallImmOffProbe()?"RAG: immediate physics-off probe on":"RAG: immediate physics-off probe off (code not recognised)");Log(b);
}
static void RgReport(int phase,const char* cond){
 EnterCriticalSection(&rgLock);
 LONG st=InterlockedExchange(&rgSteps,0);double bs=rgBodiesSum;rgBodiesSum=0;RgStat s[8];memcpy(s,rgStat,sizeof(s));memset(rgStat,0,sizeof(rgStat));
 LeaveCriticalSection(&rgLock);
 LONG ev=InterlockedExchange(&rgEvents,0);
 for(int g=0;g<2;g++){LONG n=InterlockedExchange(&wkN[g],0),bg=InterlockedExchange(&wkBig[g],0),mx=InterlockedExchange(&wkMax[g],0);LONGLONG su=InterlockedExchange64(&wkSum[g],0),ws=InterlockedExchange64(&wkWSum[g],0);
  char wb[200];sprintf_s(wb,"RAG phase %d WAKE %s: %ld bodies became active, speed at activation avg %.2f max %.2f m/s, >2 m/s %ld, spin avg %.1f rad/s",phase,g?"ragdoll parts (class 5-7)":"single bodies (class 4)",n,n?su/1000.0/n:0.0,mx/1000.0,bg,n?ws/1000.0/n:0.0);Log(wb);}
 {char wb[300];int k=sprintf_s(wb,"RAG phase %d worlds:",phase);for(int i=0;i<4;i++){RgW* w=rgWorlds[i];if(!w)continue;k+=sprintf_s(wb+k,sizeof(wb)-k," #%d %08lx steps %ld bodies/step %.0f;",i,w->world,w->steps,w->steps?w->bodies/w->steps:0.0);w->steps=0;w->bodies=0;}Log(wb);}
 char b[700];sprintf_s(b,"RAG phase %d [%s]: physics steps %ld, bodies/step %.0f, launch events %ld, read faults %ld",phase,cond,st,st?bs/st:0.0,ev,(LONG)rgFaults);Log(b);
 for(int c=0;c<8;c++){const RgStat& x=s[c];if(x.snaps<1)continue;
  sprintf_s(b,"RAG phase %d class %d: body-steps %.0f, avg speed %.3f m/s | between steps: velocity changed >0.5 in %.1f%% (avg change %.3f m/s), spin changed >3 in %.1f%%, moved by hand >5cm in %.1f%% | inside step: velocity jump >4 %.0f | direction reversals %.1f%% of %.0f",
   phase,c,x.snaps,x.speed/x.snaps,100*x.outDv/x.snaps,x.outDvSum/x.snaps,100*x.outW/x.snaps,100*x.outDp/x.snaps,x.inDv,x.revN?100*x.rev/x.revN:0.0,x.revN);Log(b);}
 {LONG n=InterlockedExchange(&fdN,0),fr=InterlockedExchange(&fdFrames,0);LONGLONG ss=InterlockedExchange64(&fdSSum,0),sd=InterlockedExchange64(&fdSDt,0);LONG mn=InterlockedExchange(&fdSMinB,0x7f7fffff),mx=InterlockedExchange(&fdSMaxB,0);
  sprintf_s(b,"RAG phase %d follow driver 0x5fdec0: %.1f calls/s in %ld frames, s avg %.2f (min %.2f max %.2f), s x frame time avg %.3f",phase,n/20.0,fr,n?ss/1000.0/n:0.0,n?*(float*)&mn:0.0f,n?*(float*)&mx:0.0f,n?sd/1000000.0/n:0.0);Log(b);}
 {LONG n=InterlockedExchange(&fvN,0),vb=InterlockedExchange(&fvBig,0),wb=InterlockedExchange(&fwBig,0),vm=InterlockedExchange(&fvMax,0),wm=InterlockedExchange(&fwMax,0),ch=InterlockedExchange(&fdChanged,0);
  LONGLONG vs=InterlockedExchange64(&fvSum,0),ws=InterlockedExchange64(&fwSum,0);LONG g[6];for(int i=0;i<6;i++)g[i]=InterlockedExchange(&fdGap[i],0);InterlockedExchange(&rgDriveN,0);InterlockedIncrement(&rgDriveGen);
  {char jb[600];int k=sprintf_s(jb,"RAG phase %d follow driver by job (calls / teleport-size jumps):",phase);for(int i=0;i<16;i++){if(!fdJobs[i].name)continue;LONG c=InterlockedExchange(&fdJobs[i].calls,0),t=InterlockedExchange(&fdJobs[i].tele,0);if(c||t)k+=sprintf_s(jb+k,sizeof(jb)-k," %s %ld / %ld;",fdJobs[i].name,c,t);}Log(jb);}
  {LONG st=InterlockedExchange(&ftStill,0),mv=InterlockedExchange(&ftMoved,0);LONG hh[7];for(int i=0;i<7;i++)hh[i]=InterlockedExchange(&ftHist[i],0);
   sprintf_s(b,"RAG phase %d follow TARGET: unchanged in %ld calls, moved in %ld, teleports skipped %ld | frames since its previous move: 1: %ld, 2: %ld, 3: %ld, 4: %ld, 5: %ld, 6+: %ld, first/long pause: %ld",phase,st,mv,InterlockedExchange(&ftTeleports,0),hh[0],hh[1],hh[2],hh[3],hh[4],hh[5],hh[6]);Log(b);}
  sprintf_s(b,"RAG phase %d follow OUTPUT: %ld sets, velocity avg %.2f max %.1f m/s, >8 m/s %ld | spin avg %.1f max %.1f rad/s, >20 rad/s %ld | updates since previous of same obj: same frame %ld, 1 frame %ld, 2 frames %ld, 3-5 %ld, 6+ %ld, first %ld | s lowered by fix %ld",
   phase,n,n?vs/1000.0/n:0.0,vm/1000.0,vb,n?ws/1000.0/n:0.0,wm/1000.0,wb,g[0],g[1],g[2],g[3],g[4],g[5],ch);Log(b);}
 {int k=sprintf_s(b,"RAG phase %d removals: %ld entities, %ld ragdoll parts (layer 6), ragdolls kept so far %ld; chains:",phase,InterlockedExchange(&rmAll,0),InterlockedExchange(&rmL6,0),keepN);
  for(int j=0;j<rmChN&&k<sizeof(b)-40;j++)k+=sprintf_s(b+k,sizeof(b)-k," #%d x%ld;",j,rmCh[j].n);Log(b);}
 {int k=sprintf_s(b,"RAG phase %d motion type changes of ragdoll parts: %ld; chains:",phase,InterlockedExchange(&mtN,0));
  for(int j=0;j<mtChN&&k<sizeof(b)-40;j++)k+=sprintf_s(b+k,sizeof(b)-k," #%d(->%d) x%ld;",j,mtCh[j].type,mtCh[j].n);Log(b);}
 {int k=sprintf_s(b,"RAG phase %d corpse wake-ups: %ld; chains:",phase,InterlockedExchange(&acN,0));
  for(int j=0;j<acChN&&k<sizeof(b)-40;j++)k+=sprintf_s(b+k,sizeof(b)-k," #%d x%ld;",j,acCh[j].n);Log(b);}
 {int k=sprintf_s(b,"RAG phase %d corpse-corpse filter passes %ld, fade-outs skipped so far %ld, immediate offs skipped so far %ld",phase,InterlockedExchange(&ccHits,0),keepPhysN,keepOffN);Log(b);}
 {int k=sprintf_s(b,"RAG phase %d physics fades of ragdolls: %ld; chains (target, duration s):",phase,InterlockedExchange(&fdeN,0));
  for(int j=0;j<fdeChN&&k<sizeof(b)-80;j++)k+=sprintf_s(b+k,sizeof(b)-k," #%d x%ld %.2f..%.2f %.2f..%.2f;",j,fdeCh[j].n,fdeCh[j].tmin,fdeCh[j].tmax,fdeCh[j].dmin,fdeCh[j].dmax);Log(b);}
 int n=sprintf_s(b,"RAG phase %d setters (kind/class caller calls/s max):",phase);
 for(int r=0;r<16;r++){int best=-1;for(int i=0;i<64;i++)if(rgCallers[i].ret&&rgCallers[i].n>0&&(best<0||rgCallers[i].n>rgCallers[best].n))best=i;if(best<0)break;
  RgCaller& c=rgCallers[best];n+=sprintf_s(b+n,sizeof(b)-n," %s/%ld %08lx %.1f %.2f;",c.kind==0?"lin":c.kind==1?"ang":"imp",c.cls,(DWORD)c.ret,c.n/20.0,c.maxMm/1000.0);c.n=-c.n;}
 for(int i=0;i<64;i++){if(rgCallers[i].n<0)rgCallers[i].n=0;rgCallers[i].n=0;rgCallers[i].maxMm=0;}
 Log(b);
}
static int __fastcall StepHook0(void* s,void*,float dt){DiagAdd(diagStep,dt);DWORD fr;__asm{mov eax,[ebp]} __asm{mov fr,eax} DiagCaller(fr);InterlockedIncrement(&hkSteps);HkTickNote();RgPre(s);CorpsePre();int r=origStep[0](s,dt);CorpsePost();RgPost(s);CorpseTick();return r;}
static int __fastcall StepHook1(void* s,void*,float dt){DiagAdd(diagStep,dt);DWORD fr;__asm{mov eax,[ebp]} __asm{mov fr,eax} DiagCaller(fr);InterlockedIncrement(&hkSteps);HkTickNote();RgPre(s);CorpsePre();int r=origStep[1](s,dt);CorpsePost();RgPost(s);CorpseTick();return r;}
static int __fastcall StepHook2(void* s,void*,float dt){DiagAdd(diagStep,dt);DWORD fr;__asm{mov eax,[ebp]} __asm{mov fr,eax} DiagCaller(fr);InterlockedIncrement(&hkSteps);HkTickNote();RgPre(s);CorpsePre();int r=origStep[2](s,dt);CorpsePost();RgPost(s);CorpseTick();return r;}
static int __fastcall BeginHook(void* s,void*,void* q,float dt){DiagAdd(diagBegin,dt);return origBegin(s,q,dt);}
// EXPERIMENT (F6): AnvilNext cloth (dresses, scarves, maybe sails) advances once per call and its
// damping/iterations are defined per frame, so at 141 FPS it runs 2.3x faster than at 60. With
// the gate on, each cloth object is updated at most ~60 times per second (time accumulator per
// object); other calls are skipped and the cloth keeps its last state.
static volatile LONG clothGate=0,clothSkipped=0;static CRITICAL_SECTION clothLock;static bool clothLockReady=false;
struct ClothSlot{void* obj;LONGLONG last,acc;};static ClothSlot clothSlots[512];
static bool ClothAllowed(void* obj){
 if(!clothGate||!clothLockReady||!frequency)return true;
 LONGLONG now=QpcNow(),step=frequency/60;bool run=true;
 EnterCriticalSection(&clothLock);
 UINT h=(UINT)(((DWORD)obj>>4)*2654435761u)%512;ClothSlot* slot=0;
 for(UINT i=0;i<16;i++){ClothSlot& c=clothSlots[(h+i)%512];if(c.obj==obj||!c.obj){slot=&c;break;}}
 if(slot){
  if(!slot->obj||now-slot->last>frequency/4){slot->obj=obj;slot->acc=step;}   // new or long idle: update now
  else slot->acc+=now-slot->last;
  slot->last=now;
  if(slot->acc>=step){slot->acc-=step;if(slot->acc>step)slot->acc=step;}else run=false;
 }
 LeaveCriticalSection(&clothLock);
 if(!run)InterlockedIncrement(&clothSkipped);
 return run;
}
static void __fastcall ClothHook(void* s,void*,void* ctx){InterlockedIncrement(&diagCloth);if(!ClothAllowed(s))return;LONGLONG a=QpcNow();origCloth(s,ctx);InterlockedExchangeAdd(&diagClothUs,(LONG)((QpcNow()-a)*1000000/(frequency?frequency:1)));}
static bool SwapSlot(DWORD slot,void* hook,void** orig,DWORD expected){
 if(*(DWORD*)slot!=expected)return false;DWORD old;if(!VirtualProtect((void*)slot,4,PAGE_READWRITE,&old))return false;
 *orig=(void*)*(DWORD*)slot;*(DWORD*)slot=(DWORD)hook;DWORD ig;VirtualProtect((void*)slot,4,old,&ig);return true;
}
static void InstallSailProbe();static void InstallCapProbe();
static void LogJobs();static void LogVelocity();static void LogClothProbe();static void LogSoftProbe();static void LogClothAnchor();static void LogLod();static void LogLodBlend();static void LogCorpseFilter();static void LogCloth30();static void LogSailProbe();static void LogCapProbe();static void LogDampFix();static void LogClothDump();
static DWORD WINAPI DiagThread(LPVOID){
 for(;;){Sleep(2000);
  EnterCriticalSection(&diagLock);DiagStat a=diagStep,b=diagBegin;LONG c=diagCloth,cs=InterlockedExchange(&clothSkipped,0);LONG fr=diagFrames,sp=diagSpikes,wu=diagWorstUs;diagFrames=diagSpikes=diagWorstUs=0;LONG smax=diagStepUsMax,ssum=diagStepUsSum,cus=diagClothUs;diagStepUsMax=diagStepUsSum=diagClothUs=0;
  diagStep.calls=diagBegin.calls=0;diagStep.minDt=diagBegin.minDt=1e9f;diagStep.maxDt=diagBegin.maxDt=diagStep.sumDt=diagBegin.sumDt=0;diagCloth=0;LeaveCriticalSection(&diagLock);
  char buf[512];sprintf_s(buf,"DIAG 2s: havokStep %ld (dt %.4f..%.4f avg %.4f) havokMtBegin %ld (dt %.4f..%.4f avg %.4f) clothUpdates %ld (skipped %ld, gate %s) | frames %ld, >25ms %ld, worst %.1f ms | havok total %.1f ms, max step %.1f ms | cloth total %.1f ms",
   a.calls,a.calls?a.minDt:0,a.maxDt,a.calls?a.sumDt/a.calls:0,b.calls,b.calls?b.minDt:0,b.maxDt,b.calls?b.sumDt/b.calls:0,c,cs,clothGate?"60Hz":"off",fr,sp,wu/1000.0,ssum/1000.0,smax/1000.0,cus/1000.0);
  Log(buf);LogApexStats();LogSpriteStats();LogParticleMetrics();LogCookStats();LogFxStats();
  {static int tick=0;if(++tick%5==0){LogJobs();LogVelocity();LogClothProbe();LogSoftProbe();LogClothAnchor();LogLod();LogLodBlend();LogCorpseFilter();LogCloth30();LogSailProbe();LogCapProbe();LogDampFix();}if(tick%2==0)LogClothDump();}
 }
}

// ---- Spike sampler: what the frame thread is doing during long frames ----------------------
// When the current frame has lasted >20 ms, the main thread is briefly suspended every 4 ms and
// its call chain (EIP + saved return addresses inside the game EXE) is recorded. Every 10 s the
// most frequent chains are written to runtime.log. Only with [Debug] Diagnostics=1.
struct Chain{DWORD a[6];DWORD s[5];LONG n;};static Chain chains[256];static int chainCount=0;
static bool InGame(DWORD a){return a>=0x401000&&a<0x1e3a000;}
static Chain listChains[256];static int listCount=0;
static void RecordInto(Chain* arr,int& cnt,const Chain& c){
 for(int i=0;i<cnt;i++)if(!memcmp(arr[i].a,c.a,sizeof(c.a))&&!memcmp(arr[i].s,c.s,sizeof(c.s))){arr[i].n++;return;}
 if(cnt<256){arr[cnt]=c;arr[cnt].n=1;cnt++;}
}
static void RecordChain(const Chain& c){RecordInto(chains,chainCount,c);}
// A stack value is a plausible return address when it points into executable image memory
// right after a call instruction.
static bool LooksLikeReturn(DWORD v){
 if(v<0x10000)return false;
 MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void*)v,&m,sizeof(m))||m.Type!=MEM_IMAGE||m.State!=MEM_COMMIT)return false;
 if(!(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
 if(v-6<(DWORD)m.BaseAddress)return false;
 const BYTE* b=(const BYTE*)v;
 if(b[-5]==0xe8)return true;                                  // call rel32
 if(b[-6]==0xff&&(b[-5]&0x38)==0x10)return true;              // call [disp32] / [reg+disp32]
 if(b[-3]==0xff&&(b[-2]&0x38)==0x10)return true;              // call [reg+disp8]
 if(b[-2]==0xff&&(b[-1]&0x38)==0x10)return true;              // call reg / [reg]
 return false;
}
static void Where(DWORD a,char* out,size_t n){
 if(!a){strcpy_s(out,n,"-");return;}
 if(InGame(a)){sprintf_s(out,n,"%08lx",a);return;}
 HMODULE h=0;char path[MAX_PATH];
 if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)a,&h)&&GetModuleFileNameA(h,path,MAX_PATH)){
  const char* name=strrchr(path,'\\');name=name?name+1:path;sprintf_s(out,n,"%s+%lx",name,a-(DWORD)h);
 } else sprintf_s(out,n,"%08lx",a);
}
static DWORD WINAPI SpikeSampler(LPVOID){
 HANDLE th=0;DWORD lastDump=GetTickCount();LONGLONG sampledFrame=0;int perFrame=0;static DWORD stack[512];
 for(;;){
  Sleep(4);
  if(!mainThreadId||!frequency)continue;
  if(!th){th=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,mainThreadId);if(!th)continue;}
  LONGLONG start=frameStartQpc;LARGE_INTEGER now;QueryPerformanceCounter(&now);
  if(start!=sampledFrame){sampledFrame=start;perFrame=0;}
  bool listing=inList!=0;
  if(listing||((now.QuadPart-start)*1000/frequency>20 && perFrame<20)){
   if(!listing)perFrame++;
   if(SuspendThread(th)!=(DWORD)-1){
    CONTEXT c;c.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;Chain ch;memset(&ch,0,sizeof(ch));int words=0;DWORD esp=0;
    if(GetThreadContext(th,&c)){
     ch.a[0]=c.Eip;int k=1;DWORD f=c.Ebp;
     for(int i=0;i<24&&k<6;i++){
      if(f<mainStackLo||f+8>mainStackHi||(f&3))break;
      DWORD ret=((DWORD*)f)[1];if(InGame(ret))ch.a[k++]=ret;
      DWORD nf=((DWORD*)f)[0];if(nf<=f)break;f=nf;
     }
     esp=c.Esp;
     if((listing||!InGame(c.Eip))&&esp>=mainStackLo&&esp<mainStackHi){
      words=(int)((mainStackHi-esp)/4);if(words>512)words=512;memcpy(stack,(void*)esp,words*4);
     }
    }
    ResumeThread(th);
    // Outside the game EXE the frame-pointer walk skips frames, so look for return addresses
    // on the copied stack: the first ones lead from the system call back to its caller.
    for(int i=0,k=0;i<words&&k<5;i++){
     DWORD v=stack[i];if(!LooksLikeReturn(v))continue;
     if(InGame(v)){ch.s[k++]=v;break;}
     ch.s[k++]=v;
    }
    if(listing){
     ch.a[1]=ch.a[2]=ch.a[3]=ch.a[4]=ch.a[5]=0;ch.s[3]=ch.s[4]=0;
     HMODULE h=0;if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)ch.a[0],&h)&&h)ch.a[0]=(DWORD)h+((ch.a[0]-(DWORD)h)&~0xffu);
     RecordInto(listChains,listCount,ch);
    }else RecordChain(ch);
   }
  }
  if(GetTickCount()-lastDump>10000){
   lastDump=GetTickCount();
   for(int r=0;r<8;r++){
    int best=-1;for(int i=0;i<chainCount;i++)if(chains[i].n>0&&(best<0||chains[i].n>chains[best].n))best=i;
    if(best<0)break;
    char b[700];char w[11][64];
    for(int i=0;i<6;i++)Where(chains[best].a[i],w[i],64);
    for(int i=0;i<5;i++)Where(chains[best].s[i],w[6+i],64);
    sprintf_s(b,"SPIKE x%ld: eip %s <- %s <- %s <- %s <- %s <- %s | stack %s, %s, %s, %s, %s",chains[best].n,
     w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10]);Log(b);
    chains[best].n=-chains[best].n;
   }
   chainCount=0;Log("SPIKE ----");
   int total=0;for(int i=0;i<listCount;i++)total+=listChains[i].n;
   for(int r=0;r<12;r++){
    int best=-1;for(int i=0;i<listCount;i++)if(listChains[i].n>0&&(best<0||listChains[i].n>listChains[best].n))best=i;
    if(best<0)break;
    char b[600];char w[6][64];Where(listChains[best].a[0],w[0],64);for(int i=0;i<5;i++)Where(listChains[best].s[i],w[1+i],64);
    sprintf_s(b,"LISTPROF x%ld/%d: %s | %s, %s, %s, %s, %s",listChains[best].n,total,w[0],w[1],w[2],w[3],w[4],w[5]);Log(b);
    listChains[best].n=-listChains[best].n;
   }
   listCount=0;
  }
 }
}
// ---- Crash notes: first serious exceptions go to runtime.log with where they happened -----
static volatile LONG crashNotes=0;
static LONG CALLBACK CrashNote(EXCEPTION_POINTERS* e){
 DWORD code=e->ExceptionRecord->ExceptionCode;
 if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_STACK_OVERFLOW&&code!=EXCEPTION_INT_DIVIDE_BY_ZERO&&code!=0xc0000409)return EXCEPTION_CONTINUE_SEARCH;
 {static volatile LONG ownNotes=0;HMODULE me=0;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)e->ExceptionRecord->ExceptionAddress,&me);
  static HMODULE self=0;if(!self)GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)&crashNotes,&self);
  if(me&&me==self){if(InterlockedIncrement(&ownNotes)>2)return EXCEPTION_CONTINUE_SEARCH;}
  else if(InterlockedIncrement(&crashNotes)>12)return EXCEPTION_CONTINUE_SEARCH;}
 CONTEXT* c=e->ContextRecord;char w[6][64];Where(c->Eip,w[0],64);
 int k=1;DWORD* sp=(DWORD*)c->Esp;
 for(int i=0;i<256&&k<6;i++){DWORD v;__try{v=sp[i];}__except(1){break;}if(LooksLikeReturn(v))Where(v,w[k++],64);}
 for(;k<6;k++)strcpy_s(w[k],"-");
 char b[600];sprintf_s(b,"EXCEPTION %08lx at %s (thread %lu%s) access %08lx | eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx | stack %s, %s, %s, %s, %s",
  code,w[0],GetCurrentThreadId(),GetCurrentThreadId()==mainThreadId?" main":"",e->ExceptionRecord->NumberParameters>1?(DWORD)e->ExceptionRecord->ExceptionInformation[1]:0,
  c->Eax,c->Ebx,c->Ecx,c->Edx,c->Esi,c->Edi,w[1],w[2],w[3],w[4],w[5]);
 Log(b);return EXCEPTION_CONTINUE_SEARCH;
}

// ---- Engine jobs: who runs per frame, and an experimental per-job rate gate (F6) -------------
// AnvilNext updates components through named jobs (e.g. "Physic::LiteRagdollComponent::Update").
// The batch executor at 0x57e070 calls job->[+0xd4] once per component (3 call sites). We point
// those calls at JobDispatch: it counts calls per job (log: JOB lines every 10 s) and, for jobs
// listed in [Experiment] ThrottleJobs while F6 is on, runs them only ThrottleHz times per second
// (other frames skip the component update), to test which systems are tied to the frame rate.
static volatile LONG jobGate=0,jobSkipped=0,jobListGen=0;static int jobHz=60;static char jobList[2048];
struct JobSlot{volatile LONG job;DWORD name;volatile DWORD observedFn;volatile LONG calls,execs;volatile LONGLONG lastFrame;volatile LONG run;LONGLONG acc,lastQpc;volatile LONG throttled;volatile DWORD comp;volatile LONG gen;volatile LONGLONG tUs,tMax,tMainUs;};
static JobSlot jobSlots[1024];
static const char* JobName(DWORD job){
 DWORD n=*(DWORD*)(job+0xb8);if(n<0x1e3a000||n>=0x2743000)n=*(DWORD*)(job+0xb0);
 return (n>=0x1e3a000&&n<0x2743000)?(const char*)n:"?";
}
static bool JobListed(const char* name){
 if(!name)return false;
 size_t n=strlen(name);if(!n||!jobList[0])return false;
 for(const char* p=jobList;*p;){const char* e=strchr(p,',');size_t len=e?(size_t)(e-p):strlen(p);
  while(len&&p[0]==' '){p++;len--;}while(len&&p[len-1]==' ')len--;
  if(len==n&&!_strnicmp(p,name,n))return true;if(!e)break;p=e+1;}
 return false;
}
static JobSlot* JobFind(DWORD job){
 UINT h=(job>>4)*2654435761u%1024;
 for(UINT i=0;i<32;i++){JobSlot& s=jobSlots[(h+i)%1024];
  if((DWORD)s.job==job)return &s;
  if(!s.job&&InterlockedCompareExchange(&s.job,(LONG)job,0)==0){const char* nm=JobName(job);s.run=1;s.name=(DWORD)nm;s.throttled=JobListed(nm);return &s;}
  if((DWORD)s.job==job)return &s;
 }
 return 0;
}
static DWORD __stdcall JobGate(DWORD job,DWORD comp){
 DWORD fn=*(DWORD*)(job+0xd4);
 JobSlot* s=JobFind(job);if(!s)return fn;tlJob=(const char*)s->name;
 // Snapshot the callable while the game owns this live job. The logger must never
 // dereference a retained job/component pointer during teardown.
 s->observedFn=fn;
 InterlockedIncrement(&s->calls);if(!s->comp)s->comp=comp;
 // The slot's name is filled in by the thread that created it, right after claiming it; another
 // thread may get here first and see 0. Only re-read the list once the name is there.
 {LONG g=jobListGen;const char* nm=(const char*)s->name;if(s->gen!=g&&nm){s->throttled=JobListed(nm);s->gen=g;}}
 LONGLONG f=frameStartQpc,prev=s->lastFrame;
 if(prev!=f&&InterlockedCompareExchange64(&s->lastFrame,f,prev)==prev){
  InterlockedIncrement(&s->execs);
  if(jobGate&&s->throttled&&frequency){
   LONGLONG step=frequency/jobHz;
   if(!s->lastQpc||f-s->lastQpc>frequency/4)s->acc=step;else s->acc+=f-s->lastQpc;
   s->lastQpc=f;
   if(s->acc>=step){s->acc-=step;if(s->acc>step)s->acc=step;s->run=1;}else s->run=0;
  }else{s->run=1;s->lastQpc=f;s->acc=0;}
 }
 if(!s->run){InterlockedIncrement(&jobSkipped);return 0;}
 return fn;
}
// Replaces "mov eax,[esi+0xd4]" before "call eax": ecx = component, esi = job.
static void JobAcc(DWORD job,LONGLONG a,LONGLONG b){JobSlot* s=JobFind(job);if(!s)return;LONGLONG us=Us(b-a);InterlockedAdd64(&s->tUs,us);if(us>s->tMax)s->tMax=us;if(GetCurrentThreadId()==mainThreadId)InterlockedAdd64(&s->tMainUs,us);}
static DWORD __stdcall JobTimed0(DWORD fn,DWORD comp,DWORD job){LARGE_INTEGER a,b;QueryPerformanceCounter(&a);DWORD r;__asm{mov ecx,comp
 call fn
 mov r,eax}
 QueryPerformanceCounter(&b);JobAcc(job,a.QuadPart,b.QuadPart);return r;}
static DWORD __stdcall JobTimed1(DWORD fn,DWORD comp,DWORD arg,DWORD job){LARGE_INTEGER a,b;QueryPerformanceCounter(&a);DWORD r;__asm{push arg
 mov ecx,comp
 call fn
 mov r,eax}
 QueryPerformanceCounter(&b);JobAcc(job,a.QuadPart,b.QuadPart);return r;}
__declspec(naked) static void JobDispatch(){__asm{
 pushad
 push ecx
 push esi
 call JobGate
 mov dword ptr [esp+28],eax
 popad
 test eax,eax
 jz skip
 push esi
 push ecx
 push eax
 call JobTimed0
 ret
skip:
 ret
}}
// Second component-job class (executor 0x57dd70): "mov edx,[esi+0xd4]; push arg; call edx" вЂ” the
// update function takes one stack argument, so a skipped call returns with "ret 4".
__declspec(naked) static void JobDispatch1(){__asm{
 pushad
 push ecx
 push esi
 call JobGate
 mov dword ptr [esp+20],eax
 popad
 test edx,edx
 jz skip1
 push esi
 push dword ptr [esp+8]
 push ecx
 push edx
 call JobTimed1
 ret 4
skip1:
 ret 4
}}
static void LogJobs(){
 struct Row{const char* name;LONG calls,execs;bool th;DWORD fn;};static Row rows[1024];int n=0;
 for(int i=0;i<1024;i++){JobSlot& s=jobSlots[i];if(!s.job||!s.name)continue;LONG c=InterlockedExchange(&s.calls,0),e=InterlockedExchange(&s.execs,0);if(!c)continue;rows[n].name=(const char*)s.name;rows[n].calls=c;rows[n].execs=e;rows[n].th=s.throttled!=0;
  rows[n].fn=s.observedFn;n++;}
 {struct T{const char* nm;LONGLONG us,mx,mu;LONG c;};static T tt[1024];int m=0;
  for(int i=0;i<1024;i++){JobSlot& s=jobSlots[i];if(!s.job||!s.name)continue;LONGLONG u=InterlockedExchange64(&s.tUs,0),mu=InterlockedExchange64(&s.tMainUs,0);LONGLONG mx=s.tMax;s.tMax=0;if(!u)continue;tt[m].nm=(const char*)s.name;tt[m].us=u;tt[m].mx=mx;tt[m].mu=mu;m++;}
  char bb[220];Log("JOBTIME 10s: per job ms per second (all threads), of it on the main thread, longest single run ms");
  for(int r=0;r<25;r++){int best=-1;for(int i=0;i<m;i++)if(tt[i].us>0&&(best<0||tt[i].us>tt[best].us))best=i;if(best<0)break;
   sprintf_s(bb,"JOBTIME %s %.2f main %.2f max %.2f",tt[best].nm,tt[best].us/10000.0,tt[best].mu/10000.0,tt[best].mx/1000.0);Log(bb);tt[best].us=0;}}
 char b[220];sprintf_s(b,"JOB 10s: gate %s (%d Hz), skipped %ld component updates; per job: runs/s, component updates/s; fn is captured dispatch address",jobGate?"ON":"off",jobHz,InterlockedExchange(&jobSkipped,0));Log(b);
 for(int r=0;r<80;r++){int best=-1;for(int i=0;i<n;i++)if(rows[i].calls>0&&(best<0||rows[i].calls>rows[best].calls))best=i;if(best<0)break;
  sprintf_s(b,"JOB %s%s %.1f %.1f fn=%08lx",rows[best].th?"[gated] ":"",rows[best].name,rows[best].execs/10.0,rows[best].calls/10.0,rows[best].fn);Log(b);rows[best].calls=0;}
}
// ---- Probe: who sets rigid-body velocities, and how often ------------------------------------
// The game sets a Havok body's velocity with motion->vtbl[0x40/4] (+0x44 for angular). Code that
// keyframes bodies (ragdoll driving, carried props) and runs every rendered frame instead of every
// physics step would show up here at ~141 calls/s per body instead of ~30. Each known call site
// ("mov r,[r+0E0h]; mov r,[r+40h]") is routed through a trampoline that counts it.
static const DWORD velSites[]={0x4a9dea,0x50ce7e,0x5582ba,0x5b3ee0,0x5b4023,0x5bb733,0x6e4f1f,0x7b37fb,0x7b3839,0x7babb1,0x1c5c6ae,0x1c5c726,0x1cab525};
static const int velCount=sizeof(velSites)/sizeof(velSites[0]);static volatile LONG velCalls[16];static bool velActive=false;
static bool InstallVelocityProbe(){
 BYTE* tramp=(BYTE*)VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!tramp)return false;
 int ok=0;
 for(int i=0;i<velCount;i++){
  BYTE* site=(BYTE*)velSites[i]-6;
  if(site[0]!=0x8b||site[2]!=0xe0||site[3]||site[4]||site[5]||site[6]!=0x8b||site[8]!=0x40)continue;
  BYTE* t=tramp+i*32;
  t[0]=0xf0;t[1]=0xff;t[2]=0x05;*(DWORD*)(t+3)=(DWORD)&velCalls[i];      // lock inc [counter]
  memcpy(t+7,site,9);                                                    // the two original movs
  t[16]=0xe9;*(DWORD*)(t+17)=(DWORD)(site+9)-(DWORD)(t+21);               // jmp back
  BYTE code[9];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)t-(DWORD)(site+5);code[5]=code[6]=code[7]=code[8]=0x90;
  DWORD old;if(!VirtualProtect(site,9,PAGE_EXECUTE_READWRITE,&old))continue;
  memcpy(site,code,9);FlushInstructionCache(GetCurrentProcess(),site,9);DWORD ig;VirtualProtect(site,9,old,&ig);ok++;
 }
 char b[96];sprintf_s(b,"DIAG: velocity probe on %d of %d call sites.",ok,velCount);Log(b);
 velActive=ok>0;return ok>0;
}
static void LogVelocity(){
 if(!velActive)return;
 char b[600];int n=sprintf_s(b,"VEL 10s calls/s:");
 for(int i=0;i<velCount;i++){LONG c=InterlockedExchange(&velCalls[i],0);if(c)n+=sprintf_s(b+n,sizeof(b)-n," %06lx=%.1f",velSites[i],c/10.0);}
 Log(b);
}

// ---- Probe: ClothLODInstance step (dresses, skirts; vtable 0x1e4e760) ---------------------------
// 0x53bb90 = ClothLODInstance::Step(float dt, int count): runs count substeps of the Verlet cloth
// (0x541b00, damping scaled by dt*30). Logged every 10 s: calls/s, dt range, count histogram and
// the callers, to see whether cloth is stepped once per rendered frame or in fixed 30 Hz steps.
static volatile LONG clStepCalls=0,clSubCalls=0,clN[6];static volatile LONGLONG clDtSum=0,clSubDtSum=0;
static volatile LONG clDtMin=0x7fffffff,clDtMax=0;struct ClCaller{volatile LONG addr,n;};static ClCaller clCallers[8];
static bool clothProbeActive=false;
// F7 experiment: the cloth is plain Verlet stepped once per frame with that frame's time. With an
// uneven frame time the gravity term a*dt^2 changes from frame to frame while the velocity term
// (x - x_prev) does not, so hanging cloth "breathes" with the frame-time jitter. With smoothing
// on, every cloth step of a frame gets the same smoothed frame time instead of the raw one.
static volatile LONG clothSmooth=0,clSmoothed=0;static LONGLONG clLastFrame=0;static float clEma=0,clLastDt=0;
static volatile LONG clFrames=0;static volatile LONGLONG clJitterUs=0;
// Mode 1 (F7 once): cloth frozen вЂ” Step is called with count 0, so no Verlet substeps run.
// If a standing NPC's dress still twitches, the twitch is not made by the cloth simulation.
// (Tested and removed: time smoothing; stepping each cloth at ~70 Hz.)
// Per-instance cadence: in which frames does each cloth instance get its Step? 1 = every frame.
// Larger gaps mean the game itself skips cloth updates for that instance (budget / LOD) вЂ” the
// cloth then stands still for a frame while the body moves on, which looks like a jerk.
static volatile LONG clGap[6],clNew=0;
struct ClInst{volatile LONG self;float acc;LONG lastFrame;float* lastD;float* lastA;float* entryP;float* exitP;LONG exitOk;};static ClInst clInst[512];static volatile LONG clSkipped=0;
static ClInst* ClFind(DWORD self){UINT h=(self>>4)*2654435761u%512;
 for(UINT i=0;i<32;i++){ClInst& c=clInst[(h+i)%512];if((DWORD)c.self==self)return &c;
  if(!c.self&&InterlockedCompareExchange(&c.self,(LONG)self,0)==0){c.acc=0;c.lastFrame=0;c.lastD=0;c.lastA=0;c.entryP=0;c.exitP=0;c.exitOk=0;return &c;}if((DWORD)c.self==self)return &c;}
 return 0;}
// Returns the new dt bits in eax and the new substep count in edx (cloth may step on several threads).
// Mode 2: the cloth damps velocity by k*dt*30 per step (time-correct per second), but its
// constraints pull a fixed fraction per step, so at 141 steps/s the cloth is a stiffer spring
// with the same damping per second = less damped, rings constantly. Mode 2 gives every step the
// damping a step gets at 60 FPS: the two "dt*30.0003" multiplies (0x541b5d, 0x57cfed) read
// clDampScale instead of the constant; outside mode 2 it stays 30.0003.
static float clDampScale=30.0003f;
static unsigned __int64 __stdcall ClothStepNote(DWORD ret,DWORD dtBits,DWORD n,DWORD self){
 DWORD clNewCount=n;
 if(labCollect)LabMeasure(self,*(float*)&dtBits);
 if(0){float dt0=*(float*)&dtBits;
  if(labCond==4&&dt0>0.001f&&dt0<0.25f)clDampScale=30.0003f*(1.0f/60.0f)/dt0;
  else if(labCond==5&&n>0&&dt0>0.0f&&dt0<0.25f){ClInst* c=ClFind(self);if(c){c->acc+=dt0;if(c->acc>=0.012f){float o=c->acc;if(o>0.05f)o=0.05f;c->acc=0;dtBits=*(DWORD*)&o;}else clNewCount=0;}}
  else if(labCond==6&&n>0&&dt0>0.0f&&dt0<0.25f){float o=dt0*0.5f;dtBits=*(DWORD*)&o;clNewCount=n*2;}}
 {ClInst* c=ClFind(self);if(c){LONG f=frameIndex;if(c->lastFrame){LONG g=f-c->lastFrame;if(g>=0&&g<100000)InterlockedIncrement(&clGap[g>5?5:g]);}else InterlockedIncrement(&clNew);c->lastFrame=f;}}
 InterlockedIncrement(&clStepCalls);float dt=*(float*)&dtBits;LONG us=(LONG)(dt*1000000.0f);
 LONGLONG f=frameStartQpc;
 if(f!=clLastFrame){clLastFrame=f;
  if(dt>0.0f&&dt<0.25f){
   if(clLastDt>0.0f){float d=dt-clLastDt;if(d<0)d=-d;InterlockedExchangeAdd64(&clJitterUs,(LONGLONG)(d*1000000.0f));InterlockedIncrement(&clFrames);}
   clLastDt=dt;clEma=clEma<=0.0f?dt:clEma*0.85f+dt*0.15f;}}


 InterlockedExchangeAdd64(&clDtSum,us);if(us<clDtMin)clDtMin=us;if(us>clDtMax)clDtMax=us;
 InterlockedIncrement(&clN[n>5?5:n]);
 for(int i=0;i<8;i++){if((DWORD)clCallers[i].addr==ret){InterlockedIncrement(&clCallers[i].n);break;}
  if(!clCallers[i].addr&&InterlockedCompareExchange(&clCallers[i].addr,(LONG)ret,0)==0){InterlockedIncrement(&clCallers[i].n);break;}}
 return ((unsigned __int64)clNewCount<<32)|dtBits;
}
static void __stdcall ClothSubNote(DWORD dtBits){InterlockedIncrement(&clSubCalls);InterlockedExchangeAdd64(&clSubDtSum,(LONG)(*(float*)&dtBits*1000000.0f));}
static DWORD clStepBack=0x53bb96,clSubBack=0x541b06;
__declspec(naked) static void ClothStepProbe(){__asm{
 pushad
 push ecx
 push dword ptr [esp+44]
 push dword ptr [esp+44]
 push dword ptr [esp+44]
 call ClothStepNote
 mov dword ptr [esp+36],eax
 mov dword ptr [esp+40],edx
 popad
 push ebp
 mov ebp,esp
 push esi
 mov esi,ecx
 jmp dword ptr [clStepBack]
}}
__declspec(naked) static void ClothSubProbe(){__asm{
 pushad
 push dword ptr [esp+36]
 call ClothSubNote
 popad
 push ebx
 mov ebx,esp
 sub esp,8
 jmp dword ptr [clSubBack]
}}
static bool HookEntry(DWORD va,const BYTE* expect,void* to){
 if(memcmp((void*)va,expect,6))return false;
 BYTE code[6];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)to-(va+5);code[5]=0x90;
 DWORD old;if(!VirtualProtect((void*)va,6,PAGE_EXECUTE_READWRITE,&old))return false;
 memcpy((void*)va,code,6);FlushInstructionCache(GetCurrentProcess(),(void*)va,6);DWORD ig;VirtualProtect((void*)va,6,old,&ig);return true;
}

// ---- Probe: SoftBodyLODInstance::Step (0x569f10) вЂ” base of ClothLODInstance; also used by other
// soft bodies (possibly sails). Each substep removes a "reference motion" vector ctx+0x130 from
// the Verlet velocity (ctx = this+0x54). With cloth frozen the dress drifted metres away from a
// standing NPC, so cloth lives in a frame that keeps moving and +0x130 compensates it. If that
// vector is refreshed at a different rate than the cloth steps (e.g. 30 Hz physics), most steps
// see 0 and some see a jump -> the whole silhouette jerks. Logged per class (vtable) every 10 s:
// steps, steps with a non-zero offset, average and largest offset.
struct SbSlot{volatile LONG vt,steps,nz;volatile LONGLONG sumUm;volatile LONG maxUm;};static SbSlot sbSlots[8];static bool sbActive=false;
// Cadence: for every instance, how many steps pass between two non-zero offsets (1 = every step,
// ~4.7 = 30 Hz at 141 FPS, 2 = 30 Hz at 60 FPS). Histogram 1/2/3/4/5/6+ over all instances.
// Wind: the pre-substep 0x573110 adds per particle wind ctx+0x90 plus a fresh random turbulence
// (LCG) every substep. Logged: average |wind| (m/s units of the game), share of steps where the
// wind vector changed since this instance's previous step, average change.
// F7 mode 1 = wind off for NPC cloth (ctx+0x90 zeroed before each cloth step).
static volatile LONG sbWSteps=0,sbWChanged=0;static volatile LONGLONG sbWSum=0,sbWDelta=0;
// Anchors: before stepping, ClothLODInstance::Step (0x53bb90 -> 0x53b2a0) rebuilds the cloth's
// attachment matrices from the NPC's bones (0x75a3d0) into this+0x84 (count this+0x8a, 64 bytes
// each). If those bones are sometimes read from the previous frame and sometimes from the
// current one (a race between threads), the anchor goes back and forth -> the whole dress jerks.
// Logged: share of steps in which the first anchor reversed its direction of motion.
static volatile LONG anSteps=0,anMoving=0,anReversed=0;static volatile LONGLONG anMoveUm=0;
struct SbInst{volatile LONG self;LONG since;float w[3];float ap[3],ad[3];LONG aok;};static SbInst sbInst[1024];static volatile LONG sbGap[6];
static SbInst* SbFind(DWORD self){UINT h=(self>>4)*2654435761u%1024;
 for(UINT i=0;i<32;i++){SbInst& c=sbInst[(h+i)%1024];if((DWORD)c.self==self)return &c;
  if(!c.self&&InterlockedCompareExchange(&c.self,(LONG)self,0)==0){c.since=-1;return &c;}if((DWORD)c.self==self)return &c;}
 return 0;}
static void __stdcall SoftStepNote(DWORD self,DWORD dtBits,DWORD n){
 DWORD vt=0,ctx=0;float o[4]={0,0,0,0};
 __try{vt=*(DWORD*)self;ctx=*(DWORD*)(self+0x54);if(ctx)memcpy(o,(void*)(ctx+0x130),16);}__except(1){return;}
 if(!n)return;
 float len=sqrtf(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]);LONG um=(LONG)(len*1000000.0f);if(!(len<1000.0f))um=999999999;
 float w[4]={0,0,0,0};__try{if(ctx)memcpy(w,(void*)(ctx+0x90),16);}__except(1){}
 {SbInst* c=SbFind(self);if(c){if(c->since>=0)c->since++;if(um>0){if(c->since>0)InterlockedIncrement(&sbGap[c->since>5?5:c->since-1]);c->since=0;}
  float dx=w[0]-c->w[0],dy=w[1]-c->w[1],dz=w[2]-c->w[2];float d=sqrtf(dx*dx+dy*dy+dz*dz);
  InterlockedIncrement(&sbWSteps);InterlockedExchangeAdd64(&sbWSum,(LONGLONG)(sqrtf(w[0]*w[0]+w[1]*w[1]+w[2]*w[2])*1000.0f));
  if(d>0.0f&&d<1000.0f){InterlockedIncrement(&sbWChanged);InterlockedExchangeAdd64(&sbWDelta,(LONGLONG)(d*1000.0f));}
  c->w[0]=w[0];c->w[1]=w[1];c->w[2]=w[2];
  if(vt==0x1e4e760){float a[4];bool ok=false;
   __try{DWORD arr=*(DWORD*)(self+0x84);WORD cnt=*(WORD*)(self+0x8a);if(arr&&cnt){memcpy(a,(void*)(arr+0x30),16);ok=true;}}__except(1){}
   if(ok){float d[3]={a[0]-c->ap[0],a[1]-c->ap[1],a[2]-c->ap[2]};float m=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(c->aok&&m<0.5f){InterlockedIncrement(&anSteps);
     if(m>0.0001f){InterlockedIncrement(&anMoving);InterlockedExchangeAdd64(&anMoveUm,(LONGLONG)(m*1000000.0f));
      float pm=sqrtf(c->ad[0]*c->ad[0]+c->ad[1]*c->ad[1]+c->ad[2]*c->ad[2]);
      if(pm>0.0001f&&d[0]*c->ad[0]+d[1]*c->ad[1]+d[2]*c->ad[2]<0.0f)InterlockedIncrement(&anReversed);}}
    for(int k=0;k<3;k++){c->ad[k]=d[k];c->ap[k]=a[k];}c->aok=1;}}}}
 for(int i=0;i<8;i++){SbSlot& sl=sbSlots[i];
  if((DWORD)sl.vt!=vt&&!(sl.vt==0&&InterlockedCompareExchange(&sl.vt,(LONG)vt,0)==0))continue;
  InterlockedIncrement(&sl.steps);if(um>0){InterlockedIncrement(&sl.nz);InterlockedExchangeAdd64(&sl.sumUm,um);if(um>sl.maxUm)sl.maxUm=um;}
  return;}
}
static DWORD sbBack=0x569f16;
__declspec(naked) static void SoftStepProbe(){__asm{
 pushad
 push dword ptr [esp+40]
 push dword ptr [esp+40]
 push ecx
 call SoftStepNote
 popad
 push ebp
 mov ebp,esp
 push esi
 mov esi,ecx
 jmp dword ptr [sbBack]
}}
static void LogSoftProbe(){
 if(!sbActive)return;
 char b[1100];int n=sprintf_s(b,"SOFT 10s per class: steps/s, steps with offset %%, avg offset mm (when non-zero), max mm:");
 for(int i=0;i<8;i++){SbSlot& sl=sbSlots[i];if(!sl.vt)continue;LONG st=InterlockedExchange(&sl.steps,0),nz=InterlockedExchange(&sl.nz,0);LONGLONG su=InterlockedExchange64(&sl.sumUm,0);LONG mx=InterlockedExchange(&sl.maxUm,0);
  if(!st)continue;n+=sprintf_s(b+n,sizeof(b)-n," | vt %08lx: %.1f/s, %.0f%%, %.3f, %.3f",(DWORD)sl.vt,st/10.0,100.0*nz/st,nz?su/1000.0/nz:0.0,mx/1000.0);}
 {LONG ws=InterlockedExchange(&sbWSteps,0),wc=InterlockedExchange(&sbWChanged,0);LONGLONG wsum=InterlockedExchange64(&sbWSum,0),wd=InterlockedExchange64(&sbWDelta,0);
  n+=sprintf_s(b+n,sizeof(b)-n," | wind avg %.3f, changed in %.0f%% of steps, avg change %.4f",ws?wsum/1000.0/ws:0.0,ws?100.0*wc/ws:0.0,wc?wd/1000.0/wc:0.0);}
 {LONG st=InterlockedExchange(&anSteps,0),mv=InterlockedExchange(&anMoving,0),rv=InterlockedExchange(&anReversed,0);LONGLONG mu=InterlockedExchange64(&anMoveUm,0);
  n+=sprintf_s(b+n,sizeof(b)-n," | cloth anchor: moving in %.0f%% of steps, avg move %.3f mm, direction reversed in %.0f%% of moving steps",st?100.0*mv/st:0.0,mv?mu/1000.0/mv:0.0,mv?100.0*rv/mv:0.0);}
 n+=sprintf_s(b+n,sizeof(b)-n," | gaps between non-zero offsets 1/2/3/4/5/6+ steps:");
 for(int i=0;i<6;i++)n+=sprintf_s(b+n,sizeof(b)-n,"%s%ld",i?"/":" ",InterlockedExchange(&sbGap[i],0));
 Log(b);
}
// F7 mode 1 = wind off: the pre-substep 0x573110 skips its wind+turbulence block when a flag in
// the cloth settings is clear ("je 0x57327b" at 0x573157); then every particle only gets gravity.
// Mode 1 turns that jump into an unconditional one, other modes restore the original bytes.
static void SetClothWind(bool off){
 static const BYTE orig[6]={0x0f,0x84,0x1e,0x01,0x00,0x00},nowind[6]={0xe9,0x1f,0x01,0x00,0x00,0x90};
 BYTE* at=(BYTE*)0x573157;if(memcmp(at,orig,6)&&memcmp(at,nowind,6))return;
 DWORD old;if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&old))return;
 memcpy(at,off?nowind:orig,6);FlushInstructionCache(GetCurrentProcess(),at,6);DWORD ig;VirtualProtect(at,6,old,&ig);
}
// F7 mode 2 = cloth ignores body capsules: ClothLODInstance::Collide (0x548d90, vtable slot +0x60,
// no stack arguments) pushes cloth particles out of the LiteRagdoll capsules (legs, hips). The
// video shows the skirt split open with the legs poking through at 141 FPS but closed at 60 FPS.
// Mode 2 makes that function return at once (first byte "push ebx" 0x53 -> "ret" 0xC3).
static void SetClothCollide(bool off){
 BYTE* at=(BYTE*)0x548d90;if(*at!=0x53&&*at!=0xc3)return;
 DWORD old;if(!VirtualProtect(at,1,PAGE_EXECUTE_READWRITE,&old))return;
 *at=off?0xc3:0x53;FlushInstructionCache(GetCurrentProcess(),at,1);DWORD ig;VirtualProtect(at,1,old,&ig);
}
// ---- FOUND: cloth "follow" vector scaled by 1/30 s instead of the frame time ----------------------
// The cloth component update (0x6c4c10) stores in ctx+0x80 how far the character moved since the
// last frame (previous minus current position), then multiplies it by (1/30) / ctx+0xbc, where
// ctx+0xbc is the elapsed time; i.e. it turns the movement into "movement per 1/30 s". Once per
// frame 0x56e290 shifts every cloth particle by ctx+0x80. At 30 FPS that is exactly one frame of
// movement; at 60 FPS twice as much, at 141 FPS 4.7 times вЂ” the cloth is kicked every frame and
// the simulation pulls it back: skirts and sails move ~2x too fast, skirts open up.
// Fix: skip that scaling (jbe 0x6c56bc at 0x6c5670 -> jmp), so the shift is the real movement of
// this frame, which is what the game does at its designed 30 FPS.
static void SetFollowFix(bool on){
 BYTE* at=(BYTE*)0x6c5670;if(at[1]!=0x4a||(at[0]!=0x76&&at[0]!=0xeb))return;
 DWORD old;if(!VirtualProtect(at,1,PAGE_EXECUTE_READWRITE,&old))return;
 *at=on?0xeb:0x76;FlushInstructionCache(GetCurrentProcess(),at,1);DWORD ig;VirtualProtect(at,1,old,&ig);
}
static bool clDampPatched=false;
static void PatchClothDamping(){
 const DWORD at[2]={0x541b5d,0x57cfed};BYTE want[8]={0xf3,0x0f,0x59,0x05,0x58,0xe7,0xe4,0x01};
 for(int i=0;i<2;i++)if(memcmp((void*)at[i],want,8))return;
 for(int i=0;i<2;i++){DWORD old;if(!VirtualProtect((void*)(at[i]+4),4,PAGE_EXECUTE_READWRITE,&old))return;
  *(DWORD*)(at[i]+4)=(DWORD)&clDampScale;FlushInstructionCache(GetCurrentProcess(),(void*)at[i],8);DWORD ig;VirtualProtect((void*)(at[i]+4),4,old,&ig);}
 clDampPatched=true;
}
static void InstallClothProbe(){
 PatchClothDamping();
 static const BYTE stepBytes[6]={0x55,0x8b,0xec,0x56,0x8b,0xf1},subBytes[6]={0x53,0x8b,0xdc,0x83,0xec,0x08};
 bool a=HookEntry(0x53bb90,stepBytes,(void*)ClothStepProbe),b=HookEntry(0x541b00,subBytes,(void*)ClothSubProbe);
 sbActive=HookEntry(0x569f10,stepBytes,(void*)SoftStepProbe);
 InstallSailProbe();InstallCapProbe();
 {static const BYTE pb[9]={0x55,0x8b,0xec,0x51,0xa1,0x10,0x02,0xdd,0x04};BYTE* at=(BYTE*)0x52d610;
  if(!memcmp(at,pb,9)){BYTE code[9];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)ClothPostProbe-(0x52d610+5);code[5]=code[6]=code[7]=code[8]=0x90;
   DWORD old;if(VirtualProtect(at,9,PAGE_EXECUTE_READWRITE,&old)){memcpy(at,code,9);FlushInstructionCache(GetCurrentProcess(),at,9);DWORD ig;VirtualProtect(at,9,old,&ig);Log("DIAG: cloth post-step probe on.");}}}Log(sbActive?"DIAG: soft-body step probe on.":"DIAG: soft-body step probe OFF.");
 clothProbeActive=a||b;
 char m[128];sprintf_s(m,"DIAG: cloth step probe %s, substep probe %s, damping hook %s.",a?"on":"OFF",b?"on":"OFF",clDampPatched?"on":"OFF");Log(m);
}
static void LogClothProbe(){
 if(!clothProbeActive)return;
 LONG c=InterlockedExchange(&clStepCalls,0),sc=InterlockedExchange(&clSubCalls,0);
 LONGLONG ds=InterlockedExchange64(&clDtSum,0),sds=InterlockedExchange64(&clSubDtSum,0);
 LONG mn=InterlockedExchange(&clDtMin,0x7fffffff),mx=InterlockedExchange(&clDtMax,0);
 char b[900];int n=sprintf_s(b,"CLOTH 10s: Step %.1f/s dt avg %.2f ms (min %.2f max %.2f), substeps %.1f/s dt avg %.2f ms, count 0/1/2/3/4/5+:",
  c/10.0,c?ds/1000.0/c:0.0,c?mn/1000.0:0.0,mx/1000.0,sc/10.0,sc?sds/1000.0/sc:0.0);
 for(int i=0;i<6;i++)n+=sprintf_s(b+n,sizeof(b)-n,"%s%ld",i?"/":" ",InterlockedExchange(&clN[i],0));
 {LONG fr=InterlockedExchange(&clFrames,0);LONGLONG j=InterlockedExchange64(&clJitterUs,0);
  n+=sprintf_s(b+n,sizeof(b)-n," | frame-time jitter avg %.2f ms | F7 mode %s (%ld changed steps, %ld skipped)",fr?j/1000.0/fr:0.0,clothSmooth==1?"1 follow fix ON":"0 off",InterlockedExchange(&clSmoothed,0),InterlockedExchange(&clSkipped,0));}
 n+=sprintf_s(b+n,sizeof(b)-n," | per-instance frames between steps 0/1/2/3/4/5+:");
 for(int i=0;i<6;i++)n+=sprintf_s(b+n,sizeof(b)-n,"%s%ld",i?"/":" ",InterlockedExchange(&clGap[i],0));
 n+=sprintf_s(b+n,sizeof(b)-n," (new %ld)",InterlockedExchange(&clNew,0));
 n+=sprintf_s(b+n,sizeof(b)-n," callers:");
 for(int i=0;i<8;i++)if(clCallers[i].addr){LONG k=InterlockedExchange(&clCallers[i].n,0);if(k)n+=sprintf_s(b+n,sizeof(b)-n," %08lx=%.1f",(DWORD)clCallers[i].addr,k/10.0);}
 Log(b);
}

// ---- Automatic lab ([Experiment] AutoLab=1) -----------------------------------------------------
// The player stands near NPCs in skirts and does nothing. Every 15 s the lab switches between full
// FPS and 60 FPS and logs, for all NPC cloth (ClothLODInstance) stepped during the phase (first 3 s
// after a switch skipped): average particle speed (m/s, free particles), average horizontal
// distance of free particles from the cloth's first anchor (skirt "opening"), and the average
// horizontal offset of the cloth centroid from that anchor (skirt pushed to one side).
static volatile LONG labSamples=0;static volatile LONGLONG labSpeed=0,labRadius=0,labOffset=0,labParticles=0,labStepUm=0,labMoving=0,labReversed=0;
// Anchor speeds: the cloth's attachment matrices (two arrays: this+0x84 count +0x8a, this+0x8c count
// +0x92; 64 bytes each, translation at +0x30) are built from the NPC's bones every step. If the
// skirt bones themselves move twice as fast at full FPS, it shows here; if they move right, the
// cloth's own use of them is at fault.
static volatile LONGLONG labASum[2],labACnt[2],labAMax[2];
// Where does the per-frame particle motion happen? Positions are copied at Step entry and at the
// post-step call (0x52d610, after all substeps of the frame). "inside" = entry -> post (the
// simulation), "outside" = previous post -> this entry (anything else in the game moving them).
static volatile LONGLONG labInUm=0,labInN=0,labOutUm=0,labOutN=0,labTvUm=0,labTvN=0,labTvNz=0,labBcUs=0;
// Who writes the particles between Steps? A hardware write breakpoint (DR0) is put on the x of one
// free particle of one skirt; every write is caught by WatchHandler, which counts the address of the
// instruction after the write. Logged per phase: the top writers. Writers inside the cloth step
// (0x541b00..0x5423xx, 0x548d90..) are expected; anything else is the per-frame mover.
static volatile DWORD watchCand=0,watchAddr=0;struct WHit{volatile LONG eip,n;};static WHit wHits[64];static volatile LONG wTotal=0;
static LONGLONG labPhaseStart=0;static int labPhase=-1;
static void LabMeasure(DWORD self,float dt){
 if(!labCollect||!(dt>0.0005f&&dt<0.1f))return;
 __try{
  WORD cnt=*(WORD*)(self+0x12);const float* pos=*(const float**)(self+0xc);const float* prv=*(const float**)(self+0x14);
  const BYTE* flags=*(const BYTE**)(self+0x34);DWORD arr=*(DWORD*)(self+0x84);WORD na=*(WORD*)(self+0x8a);
  if(!cnt||cnt>4096||!pos||!prv||!flags||!arr||!na)return;
  const float* an=(const float*)(arr+0x30);
  double sp=0,rad=0,cx=0,cy=0,stepm=0;int n=0,mov=0,rev=0;
  ClInst* ci=ClFind(self);float* ld=0;if(ci){if(!ci->lastD){ci->lastD=(float*)calloc(4096*3,sizeof(float));}ld=ci->lastD;}
  for(int i=0;i<cnt;i++){if(flags[i]&1)continue;const float* p=pos+i*4;const float* q=prv+i*4;
   double dx=p[0]-q[0],dy=p[1]-q[1],dz=p[2]-q[2];double dm=sqrt(dx*dx+dy*dy+dz*dz);sp+=dm/dt;stepm+=dm;
   if(ld){float* L=ld+i*3;if(dm>0.0005){mov++;if(dx*L[0]+dy*L[1]+dz*L[2]<0)rev++;}L[0]=(float)dx;L[1]=(float)dy;L[2]=(float)dz;}
   double hx=p[0]-an[0],hy=p[1]-an[1];rad+=sqrt(hx*hx+hy*hy);cx+=hx;cy+=hy;n++;}
  if(!n)return;
  cx/=n;cy/=n;
  // v7: the particle writer outside the step is 0x56e290 (adds ctx+0x80 to every particle, pos and
  // prev, once per frame). Now watch who writes ctx+0x80, and measure its size.
  {DWORD ctx=*(DWORD*)(self+0x54);if(ctx){const float* tv=(const float*)(ctx+0x80);double m=sqrt((double)tv[0]*tv[0]+(double)tv[1]*tv[1]+(double)tv[2]*tv[2]);
    if(m<100){InterlockedExchangeAdd64(&labTvUm,(LONGLONG)(m*1000000.0));InterlockedIncrement64(&labTvN);if(m>1e-6)InterlockedIncrement64(&labTvNz);
     float bc=*(const float*)(ctx+0xbc);if(bc>0&&bc<1)InterlockedExchangeAdd64(&labBcUs,(LONGLONG)(bc*1000000.0));}
    if(!watchCand)watchCand=ctx+0x80;}}
  if(ci&&cnt<=4096){if(!ci->entryP)ci->entryP=(float*)calloc(4096*4,sizeof(float));if(!ci->exitP)ci->exitP=(float*)calloc(4096*4,sizeof(float));
   if(ci->entryP&&ci->exitP){
    if(ci->exitOk==1){double o=0;int on=0;for(int i=0;i<cnt;i++){if(flags[i]&1)continue;const float* p=pos+i*4;const float* e=ci->exitP+i*4;double ex=p[0]-e[0],ey=p[1]-e[1],ez=p[2]-e[2];o+=sqrt(ex*ex+ey*ey+ez*ez);on++;}
     if(on){InterlockedExchangeAdd64(&labOutUm,(LONGLONG)(o/on*1000000.0));InterlockedIncrement64(&labOutN);}}
    memcpy(ci->entryP,pos,cnt*16);ci->exitOk=2;}}
  if(ci){if(!ci->lastA)ci->lastA=(float*)calloc(2*256*4,sizeof(float));float* LA=ci->lastA;
   if(LA)for(int k=0;k<2;k++){DWORD arr2=*(DWORD*)(self+(k?0x8c:0x84));WORD c2=*(WORD*)(self+(k?0x92:0x8a));if(!arr2||!c2||c2>256)continue;
    double ssum=0,smax=0;int cn=0;
    for(int j=0;j<c2;j++){const float* t=(const float*)(arr2+j*64+0x30);float* L=LA+(k*256+j)*4;
     if(L[3]!=0){double ex=t[0]-L[0],ey=t[1]-L[1],ez=t[2]-L[2];double v=sqrt(ex*ex+ey*ey+ez*ez)/dt;if(v<50){ssum+=v;if(v>smax)smax=v;cn++;}}
     L[0]=t[0];L[1]=t[1];L[2]=t[2];L[3]=1;}
    if(cn){InterlockedExchangeAdd64(&labASum[k],(LONGLONG)(ssum/cn*1000000.0));InterlockedExchangeAdd64(&labAMax[k],(LONGLONG)(smax*1000000.0));InterlockedIncrement64(&labACnt[k]);}}}
  InterlockedIncrement(&labSamples);InterlockedExchangeAdd64(&labParticles,n);
  InterlockedExchangeAdd64(&labSpeed,(LONGLONG)(sp/n*1000000.0));InterlockedExchangeAdd64(&labRadius,(LONGLONG)(rad/n*1000000.0));
  InterlockedExchangeAdd64(&labOffset,(LONGLONG)(sqrt(cx*cx+cy*cy)*1000000.0));
  InterlockedExchangeAdd64(&labStepUm,(LONGLONG)(stepm/n*1000000.0));InterlockedExchangeAdd64(&labMoving,mov);InterlockedExchangeAdd64(&labReversed,rev);
 }__except(1){}
}

static void __stdcall ClothPostNote(DWORD self){
 if(!labCollect)return;
 __try{
  if(*(DWORD*)self!=0x1e4e760)return;
  ClInst* ci=ClFind(self);if(!ci||ci->exitOk!=2&&ci->exitOk!=1)return;
  WORD cnt=*(WORD*)(self+0x12);const float* pos=*(const float**)(self+0xc);const BYTE* flags=*(const BYTE**)(self+0x34);
  if(!cnt||cnt>4096||!pos||!flags||!ci->entryP||!ci->exitP)return;
  if(ci->exitOk==2){double o=0;int on=0;for(int i=0;i<cnt;i++){if(flags[i]&1)continue;const float* p=pos+i*4;const float* e=ci->entryP+i*4;double ex=p[0]-e[0],ey=p[1]-e[1],ez=p[2]-e[2];o+=sqrt(ex*ex+ey*ey+ez*ez);on++;}
   if(on){InterlockedExchangeAdd64(&labInUm,(LONGLONG)(o/on*1000000.0));InterlockedIncrement64(&labInN);}}
  memcpy(ci->exitP,pos,cnt*16);ci->exitOk=1;
 }__except(1){}
}
static DWORD clPostBack=0x52d619;
__declspec(naked) static void ClothPostProbe(){__asm{
 pushad
 push ecx
 call ClothPostNote
 popad
 push ebp
 mov ebp,esp
 push ecx
 mov eax,dword ptr ds:[0x4dd0210]
 jmp dword ptr [clPostBack]
}}

static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e){
 if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP||!(e->ContextRecord->Dr6&0xf))return EXCEPTION_CONTINUE_SEARCH;
 if(e->ContextRecord->Dr6&1){DWORD eip=e->ContextRecord->Eip;InterlockedIncrement(&wTotal);
  for(int i=0;i<64;i++){if((DWORD)wHits[i].eip==eip){InterlockedIncrement(&wHits[i].n);break;}
   if(!wHits[i].eip&&InterlockedCompareExchange(&wHits[i].eip,(LONG)eip,0)==0){InterlockedIncrement(&wHits[i].n);break;}}}
 e->ContextRecord->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;
}
static void SetWatch(DWORD addr){
 static bool vehOn=false;if(!vehOn){AddVectoredExceptionHandler(1,WatchHandler);vehOn=true;}
 DWORD self=GetCurrentThreadId();int n=0;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap==INVALID_HANDLE_VALUE)return;
 THREADENTRY32 te;te.dwSize=sizeof(te);
 if(Thread32First(snap,&te))do{
  if(te.th32OwnerProcessID!=gamePID||te.th32ThreadID==self)continue;
  HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,FALSE,te.th32ThreadID);if(!t)continue;
  if(SuspendThread(t)!=(DWORD)-1){CONTEXT c;c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
   if(GetThreadContext(t,&c)){c.Dr0=addr;c.Dr6=0;c.Dr7=addr?((c.Dr7&~0x000F0003u)|1u|(1u<<16)|(3u<<18)):(c.Dr7&~0x000F0003u);if(SetThreadContext(t,&c))n++;}
   ResumeThread(t);}
  CloseHandle(t);
 }while(Thread32Next(snap,&te));
 CloseHandle(snap);
 watchAddr=addr;
 char b[96];sprintf_s(b,addr?"LAB: write watch on %08lx in %d threads":"LAB: write watch off (%d threads)",addr,n);Log(b);
}
static void LogWatch(int phase){
 char b[700];int n=sprintf_s(b,"LAB phase %d writers of ctx+0x80 (%ld writes): ",phase,InterlockedExchange(&wTotal,0));
 for(int r=0;r<12;r++){int best=-1;for(int i=0;i<64;i++)if(wHits[i].n>0&&(best<0||wHits[i].n>wHits[best].n))best=i;if(best<0)break;
  n+=sprintf_s(b+n,sizeof(b)-n," %08lx=%ld",(DWORD)wHits[best].eip,(LONG)wHits[best].n);wHits[best].n=0;}
 for(int i=0;i<64;i++){wHits[i].eip=0;wHits[i].n=0;}
 Log(b);
}
static void LabShot(){
 INPUT in[4]={};for(int i=0;i<4;i++)in[i].type=INPUT_KEYBOARD;
 in[0].ki.wVk=VK_MENU;in[1].ki.wVk=VK_F1;in[2].ki.wVk=VK_F1;in[2].ki.dwFlags=KEYEVENTF_KEYUP;in[3].ki.wVk=VK_MENU;in[3].ki.dwFlags=KEYEVENTF_KEYUP;
 SendInput(4,in,sizeof(INPUT));
}
// LAB v9 (AutoLab=2, ragdolls): 20 s at full FPS, 20 s at 60 FPS, repeating while the player fights.
// A screenshot (Alt+F1) is taken right after a launch event, at most one per 4 s.
static void RagLabTick(){
 LARGE_INTEGER q;QueryPerformanceCounter(&q);if(!frequency)return;
 static int phase=-1;static LONGLONG start=0,lastShot=0;static const char* names[2]={"full FPS, fix ON","60 FPS, fix ON"};
 if(phase<0){phase=0;start=q.QuadPart;safeMode=0;InterlockedExchange(&fdFix,1);InterlockedExchange(&tpGuard,0);InterlockedExchange(&rgOn,1);Log("LAB v27 (ragdolls): started, follow fix on; 20 s full FPS / 20 s 60 FPS; CorpseCollide + KeepPhysics per Lab.ini, fades, wake-ups, motion types, removals");return;}
 if(InterlockedExchange(&rgShotAsk,0)&&q.QuadPart-lastShot>4*frequency){lastShot=q.QuadPart;LabShot();Log("RAG: screenshot (Alt+F1) after a launch event");}
 if((double)(q.QuadPart-start)/frequency>=20.0){RgReport(phase,names[phase&1]);phase++;start=q.QuadPart;int c=phase&1;safeMode=c;char m[120];sprintf_s(m,"RAG: now %s",names[c]);Log(m);}
}
static void LabTick(){
 if(labOn==2){RagLabTick();return;}
 LARGE_INTEGER q;QueryPerformanceCounter(&q);if(!frequency)return;
 // v3: which engine job moves the skirt targets? Each condition runs one job family at 60 Hz
 // (others every frame) at full FPS. A job whose update is tied to the frame count halves the
 // skirt speed when gated; jobs with correct time steps do not change it.
 // v4: job gating showed no job whose 60 Hz gate halves the skirt speed (and gating ActionBones
 // crashed the game), so the loop is back to full FPS / 60 FPS, now with anchor speeds.
 static const char* gateJobs[]={""};
 static const char* condNames[4]={"full FPS, game as is","60 FPS, game as is","full FPS, cloth follow FIX","60 FPS, cloth follow FIX"};
 const int nCond=4;
 if(labPhase<0){labPhase=0;labPhaseStart=q.QuadPart;labCond=0;safeMode=0;Log("LAB v8: started, 15 s per condition: full/60 FPS, without and with the cloth follow fix");return;}
 double t=(double)(q.QuadPart-labPhaseStart)/frequency;
 // At 9 s into each phase press Alt+F1 (NVIDIA App screenshot) so each phase gets a picture.
 {static int shotPhase=-1;if(t>=9.0&&shotPhase!=labPhase){shotPhase=labPhase;
  INPUT in[4]={};for(int i=0;i<4;i++)in[i].type=INPUT_KEYBOARD;
  in[0].ki.wVk=VK_MENU;in[1].ki.wVk=VK_F1;in[2].ki.wVk=VK_F1;in[2].ki.dwFlags=KEYEVENTF_KEYUP;in[3].ki.wVk=VK_MENU;in[3].ki.dwFlags=KEYEVENTF_KEYUP;
  SendInput(4,in,sizeof(INPUT));char m[96];sprintf_s(m,"LAB: screenshot (Alt+F1) phase %d condition %d",labPhase,labCond);Log(m);}}
 if(t>=3.0&&!labCollect){InterlockedExchange(&labSamples,0);InterlockedExchange64(&labSpeed,0);InterlockedExchange64(&labRadius,0);InterlockedExchange64(&labOffset,0);InterlockedExchange64(&labParticles,0);InterlockedExchange64(&labStepUm,0);InterlockedExchange64(&labMoving,0);InterlockedExchange64(&labReversed,0);for(int k=0;k<2;k++){InterlockedExchange64(&labASum[k],0);InterlockedExchange64(&labACnt[k],0);InterlockedExchange64(&labAMax[k],0);}InterlockedExchange64(&labInUm,0);InterlockedExchange64(&labInN,0);InterlockedExchange64(&labOutUm,0);InterlockedExchange64(&labOutN,0);InterlockedExchange64(&labTvUm,0);InterlockedExchange64(&labTvN,0);InterlockedExchange64(&labTvNz,0);InterlockedExchange64(&labBcUs,0);InterlockedExchange(&labCollect,1);}
 {static int watchPhase=-1;if(t>=4.0&&watchPhase!=labPhase&&watchCand){watchPhase=labPhase;SetWatch(watchCand);}}
 if(t>=15.0){
  InterlockedExchange(&labCollect,0);
  if(watchAddr){SetWatch(0);LogWatch(labPhase);}watchCand=0;
  LONG ns=labSamples;char b[400];
  sprintf_s(b,"LAB phase %d [%d %s]: cloth steps %ld, particles/step %.0f, speed %.4f m/s, move per step %.2f mm, reversed %.0f%%, radius %.4f m, offset %.4f m",
   labPhase,labCond,condNames[labCond],ns,ns?(double)labParticles/ns:0.0,ns?labSpeed/1000000.0/ns:0.0,ns?labStepUm/1000.0/ns:0.0,labMoving?100.0*labReversed/labMoving:0.0,ns?labRadius/1000000.0/ns:0.0,ns?labOffset/1000000.0/ns:0.0);
  Log(b);
  sprintf_s(b,"LAB phase %d anchors: set A avg %.4f m/s (fastest %.4f), set B avg %.4f m/s (fastest %.4f)",labPhase,
   labACnt[0]?labASum[0]/1000000.0/labACnt[0]:0.0,labACnt[0]?labAMax[0]/1000000.0/labACnt[0]:0.0,labACnt[1]?labASum[1]/1000000.0/labACnt[1]:0.0,labACnt[1]?labAMax[1]/1000000.0/labACnt[1]:0.0);
  Log(b);
  sprintf_s(b,"LAB phase %d per frame: moved inside Step (simulation) %.3f mm, moved between Steps (elsewhere) %.3f mm; ctx+0x80 avg %.3f mm, non-zero in %.0f%% of steps, ctx+0xbc avg %.2f ms",labPhase,
   labInN?labInUm/1000.0/labInN:0.0,labOutN?labOutUm/1000.0/labOutN:0.0,labTvN?labTvUm/1000.0/labTvN:0.0,labTvN?100.0*labTvNz/labTvN:0.0,labTvN?labBcUs/1000.0/labTvN:0.0);
  Log(b);
  labPhase++;labPhaseStart=q.QuadPart;labCond=labPhase%nCond;safeMode=(labCond&1);SetFollowFix(labCond>=2);
  InterlockedExchange(&jobGate,0);
  InterlockedIncrement(&jobListGen);
 }
}
static bool InstallJobHooks(){
 static const BYTE site[]={0x8b,0x86,0xd4,0x00,0x00,0x00};
 BYTE code[6];code[0]=0xb8;*(DWORD*)(code+1)=(DWORD)JobDispatch;code[5]=0x90;
 const DWORD at[3]={0x57e0d6,0x57e208,0x57e26c};
 for(int i=0;i<3;i++)if(memcmp((void*)at[i],site,6))return false;
 for(int i=0;i<3;i++){DWORD old;if(!VirtualProtect((void*)at[i],6,PAGE_EXECUTE_READWRITE,&old))return false;memcpy((void*)at[i],code,6);FlushInstructionCache(GetCurrentProcess(),(void*)at[i],6);DWORD ig;VirtualProtect((void*)at[i],6,old,&ig);}
 static const BYTE site1[]={0x8b,0x96,0xd4,0x00,0x00,0x00};
 BYTE code1[6];code1[0]=0xba;*(DWORD*)(code1+1)=(DWORD)JobDispatch1;code1[5]=0x90;
 const DWORD at1[3]={0x57dde9,0x57df3d,0x57dfa5};bool ok1=true;
 for(int i=0;i<3;i++)if(memcmp((void*)at1[i],site1,6))ok1=false;
 if(ok1)for(int i=0;i<3;i++){DWORD old;if(!VirtualProtect((void*)at1[i],6,PAGE_EXECUTE_READWRITE,&old))break;memcpy((void*)at1[i],code1,6);FlushInstructionCache(GetCurrentProcess(),(void*)at1[i],6);DWORD ig;VirtualProtect((void*)at1[i],6,old,&ig);}
 Log(ok1?"DIAG: job profiler covers both component executors.":"DIAG: second job executor not recognised.");
 return true;
}

// ---- F7: mark the log and sample every game thread for 7 s ---------------------------------
// For finding the code that runs during a specific moment (e.g. the hat cutscene): each thread
// is briefly suspended every 4 ms and the innermost game-EXE address (EIP or a return address
// on its stack) is counted. Compare a burst during the event with one at a calm moment.
static volatile LONG burstRequest=0;static LONG markCount=0;static bool previousF6=false,previousF7=false;
struct BurstHit{DWORD addr;LONG n;};static BurstHit burstHits[4096];
static void BurstAdd(DWORD a){
 a&=~0x3Fu;UINT h=(a>>6)*2654435761u%4096;
 for(UINT i=0;i<64;i++){BurstHit& b=burstHits[(h+i)%4096];if(b.addr==a){b.n++;return;}if(!b.addr){b.addr=a;b.n=1;return;}}
}
static int CopyStack(DWORD esp,DWORD* out,int max){
 // Stay inside the committed stack region: reading past it faults, and the fault would be
 // logged as an EXCEPTION and use up the crash-note budget.
 MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void*)esp,&m,sizeof(m))||m.State!=MEM_COMMIT)return 0;
 DWORD room=((DWORD)m.BaseAddress+(DWORD)m.RegionSize-esp)/4;if((DWORD)max>room)max=(int)room;
 int got=0;__try{const DWORD* sp=(const DWORD*)esp;for(;got<max;got++)out[got]=sp[got];}__except(1){}
 return got;
}
static DWORD WINAPI BurstThread(LPVOID){
 static DWORD words[48];
 for(;;){
  Sleep(50);if(!InterlockedExchange(&burstRequest,0))continue;
  memset(burstHits,0,sizeof(burstHits));
  HANDLE threads[96];int n=0;DWORD self=GetCurrentThreadId();
  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
  if(snap!=INVALID_HANDLE_VALUE){THREADENTRY32 te;te.dwSize=sizeof(te);
   if(Thread32First(snap,&te))do{if(te.th32OwnerProcessID==gamePID&&te.th32ThreadID!=self&&n<96){HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,te.th32ThreadID);if(t)threads[n++]=t;}}while(Thread32Next(snap,&te));
   CloseHandle(snap);}
  DWORD start=GetTickCount();LONG samples=0;
  while(GetTickCount()-start<7000){
   for(int i=0;i<n;i++){
    if(SuspendThread(threads[i])==(DWORD)-1)continue;
    CONTEXT c;c.ContextFlags=CONTEXT_CONTROL;int got=0;
    if(GetThreadContext(threads[i],&c)){words[0]=c.Eip;got=1+CopyStack(c.Esp,words+1,47);}
    ResumeThread(threads[i]);
    if(!got)continue;
    DWORD hit=0;if(InGame(words[0]))hit=words[0];
    for(int k=1;!hit&&k<got;k++)if(InGame(words[k])&&LooksLikeReturn(words[k]))hit=words[k];
    if(hit){BurstAdd(hit);samples++;}
   }
   Sleep(4);
  }
  for(int i=0;i<n;i++)CloseHandle(threads[i]);
  char b[160];sprintf_s(b,"BURST %ld: %d threads, %ld samples in game code (address & ~3F : count)",markCount,n,samples);Log(b);
  for(int r=0;r<60;r++){
   int best=-1;for(int i=0;i<4096;i++)if(burstHits[i].n>0&&(best<0||burstHits[i].n>burstHits[best].n))best=i;
   if(best<0)break;
   sprintf_s(b,"BURST %ld %08lx %ld",markCount,burstHits[best].addr,burstHits[best].n);Log(b);burstHits[best].n=0;
  }
 }
}
// ---- GPUSYNC (Claude, experiment): bounded wait for the frame's GPU event query ----------------------
// 0x930710 (called once per frame) waits in 0x971650 until ID3D11DeviceContext::GetData on the event query
// at 0x4dd23c0 succeeds, spinning on SwitchToThread (call 0x8162d0 at 0x971690), and only then reads the
// visibility (occlusion) query results; a result that is not ready simply keeps the previous one (0x9307c7).
// With GPU PhysX the GPU finishes the frame later and the main thread spins. Here the spin gives up after
// gsLimitUs and continues as if the event had signalled. F3 (with Diagnostics) toggles game / limited.
static volatile LONG gsOn=1;static LONG gsLimitUs=500;static volatile LONG gsWaits=0,gsTimeouts=0;static volatile LONGLONG gsUs=0;
static LONGLONG gsStart=0;static LONG gsFrame=-1;static DWORD gsBack=0x971695,gsExit=0x9716ad;static DWORD gsYield=0;
static int __stdcall GsYield(){
 LARGE_INTEGER q;QueryPerformanceCounter(&q);
 if(gsFrame!=frameIndex||!gsStart){gsFrame=frameIndex;gsStart=q.QuadPart;InterlockedIncrement(&gsWaits);}
 ((void(WINAPI*)())gsYield)();
 QueryPerformanceCounter(&q);LONGLONG us=Us(q.QuadPart-gsStart);
 if(gsOn&&us>gsLimitUs){InterlockedIncrement(&gsTimeouts);gsStart=0;gsUs+=us;return 1;}
 return 0;}
__declspec(naked) static void GsHook(){__asm{
 push eax
 push ecx
 push edx
 call GsYield
 test eax,eax
 pop edx
 pop ecx
 pop eax
 jnz giveup
 jmp dword ptr [gsBack]
giveup:
 jmp dword ptr [gsExit]
}}
static void InstallGpuSync(){
 LONG v=(LONG)CfgIntW(L"Experiment",L"GpuSyncLimitUs",500,configPath);if(v<0){Log("GPUSYNC: off (game wait).");return;}gsLimitUs=v;
 gsYield=*(DWORD*)0x1e3a280;
 static const BYTE b[5]={0xe8,0x3b,0x4c,0xea,0xff};
 bool ok=Jump(0x971690,b,5,GsHook);
 char t[120];sprintf_s(t,"GPUSYNC: frame GPU-query wait limited to %ld us: %s (F3 toggles game/limited)",gsLimitUs,ok?"on":"code not recognised");Log(t);}
static void LogGpuSync(){LONG w=InterlockedExchange(&gsWaits,0),t=InterlockedExchange(&gsTimeouts,0);if(!w&&!t)return;char b[160];sprintf_s(b,"GPUSYNC 2s: %s, frames that spun %ld, gave up %ld",gsOn?"limited":"game wait",w,t);Log(b);}
// ---- MAINPROF (Claude, diagnostic): where the main thread spends its time, sampled every ~1 ms ----
struct MpMod{HMODULE m;char name[40];DWORD lo,hi;};static MpMod mpMods[64];static int mpModN=0;
static void MpModules(){HMODULE mods[512];DWORD need=0;if(!EnumProcessModules(GetCurrentProcess(),mods,sizeof mods,&need))return;int n=need/sizeof(HMODULE);mpModN=0;
 for(int i=0;i<n&&mpModN<64;i++){MODULEINFO mi;if(!GetModuleInformation(GetCurrentProcess(),mods[i],&mi,sizeof mi))continue;MpMod& e=mpMods[mpModN++];e.m=mods[i];e.lo=(DWORD)mi.lpBaseOfDll;e.hi=e.lo+mi.SizeOfImage;char f[MAX_PATH];GetModuleFileNameA(mods[i],f,MAX_PATH);char* l=strrchr(f,92);strncpy_s(e.name,l?l+1:f,_TRUNCATE);}}
static int MpFind(DWORD a){for(int i=0;i<mpModN;i++)if(a>=mpMods[i].lo&&a<mpMods[i].hi)return i;return -1;}
struct MpHit{DWORD k;LONG n;};
static void MpAdd(MpHit* t,int cap,DWORD k){UINT h=(k*2654435761u)%cap;for(int i=0;i<64;i++){MpHit& b=t[(h+i)%cap];if(b.k==k){b.n++;return;}if(!b.n){b.k=k;b.n=1;return;}}}
static MpHit mpMod[128],mpExe[2048],mpLeaf[2048];
static DWORD WINAPI MainProf(LPVOID){
 while(!mainThreadId)Sleep(500);Sleep(5000);
 HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,mainThreadId);if(!t){Log("MAINPROF: cannot open main thread");return 0;}
 timeBeginPeriod(1);MpModules();Log("MAINPROF: sampling the main thread every ~1 ms");
 static DWORD words[64];DWORD last=GetTickCount();LONG total=0;int round=0;
 for(;;){
  Sleep(1);
  if(SuspendThread(t)!=(DWORD)-1){CONTEXT c;c.ContextFlags=CONTEXT_CONTROL;int got=0;if(GetThreadContext(t,&c)){words[0]=c.Eip;got=1+CopyStack(c.Esp,words+1,63);}ResumeThread(t);
   if(got){total++;int mi=MpFind(words[0]);MpAdd(mpMod,128,mi<0?0xffff:(DWORD)mi);
    if(mi>=0&&mpMods[mi].m!=GetModuleHandleW(0))MpAdd(mpLeaf,2048,((DWORD)mi<<24)|((words[0]-mpMods[mi].lo)&0xfffff0));if(mi<0){MEMORY_BASIC_INFORMATION mb;if(VirtualQuery((void*)words[0],&mb,sizeof mb))MpAdd(mpExe,2048,0x80000000u|(((DWORD)mb.AllocationBase)>>4));}
    DWORD hit=0;if(InGame(words[0]))hit=words[0];for(int k=1;!hit&&k<got;k++)if(InGame(words[k])&&LooksLikeReturn(words[k]))hit=words[k];if(hit)MpAdd(mpExe,2048,hit&~0xFu);}}
  if(GetTickCount()-last>=2000){last=GetTickCount();if(++round%10==0)MpModules();
   char b[300];sprintf_s(b,"MAINPROF 2s: %ld samples",total);Log(b);
   for(int r=0;r<8;r++){int best=-1;for(int i=0;i<128;i++)if(mpMod[i].n&&(best<0||mpMod[i].n>mpMod[best].n))best=i;if(best<0)break;
    DWORD k=mpMod[best].k;sprintf_s(b,"MAINPROF  module %s %.1f%%",k==0xffff?"(none)":mpMods[k].name,100.0*mpMod[best].n/max(total,1L));Log(b);mpMod[best].n=0;}
   for(int r=0;r<10;r++){int best=-1;for(int i=0;i<2048;i++)if(mpLeaf[i].n&&(best<0||mpLeaf[i].n>mpLeaf[best].n))best=i;if(best<0)break;
    DWORD k=mpLeaf[best].k;sprintf_s(b,"MAINPROF  dll %s+%lx %.1f%%",mpMods[k>>24].name,k&0xffffff,100.0*mpLeaf[best].n/max(total,1L));Log(b);mpLeaf[best].n=0;}
   for(int r=0;r<12;r++){int best=-1;for(int i=0;i<2048;i++)if(mpExe[i].n&&(best<0||mpExe[i].n>mpExe[best].n))best=i;if(best<0)break;
    sprintf_s(b,"MAINPROF  exe %08lx %.1f%%",mpExe[best].k,100.0*mpExe[best].n/max(total,1L));Log(b);mpExe[best].n=0;}
   memset(mpMod,0,sizeof mpMod);memset(mpExe,0,sizeof mpExe);memset(mpLeaf,0,sizeof mpLeaf);total=0;}
 }}
static void PollDiagKeys(){
 {static bool p3=false;bool k3=(GetAsyncKeyState(VK_F3)&0x8000)!=0;if(k3&&!p3){LONG m=!gsOn;InterlockedExchange(&gsOn,m);Log(m?"KEY F3: GPU-query wait limited":"KEY F3: GPU-query wait = game");}p3=k3;}
 bool k6=(GetAsyncKeyState(VK_F6)&0x8000)!=0,k7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
 if(k6&&!previousF6){LONG on=!smokeReuse;InterlockedExchange(&smokeReuse,on);Log(on?"KEY F6: smoke buffer reuse ON":"KEY F6: smoke buffer reuse OFF (game: rewritten every frame)");}
 if(k7&&!previousF7){LONG m=!(dampFix||skinFix||iterFix);InterlockedExchange(&dampFix,m);InterlockedExchange(&skinFix,m);InterlockedExchange(&iterFix,m);Log(m?"KEY F7: cloth 30 FPS formulas ON":"KEY F7: cloth formulas as game");}
 previousF6=k6;previousF7=k7;
 {static bool p12=false;bool k12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;if(k12&&!p12){LONG m=!lbMode;InterlockedExchange(&lbMode,m);Log(m?"KEY F12: LOD crossfade OFF (hard switch)":"KEY F12: LOD crossfade = game");}p12=k12;}
 {static bool p5=false;bool k5=(GetAsyncKeyState(VK_F5)&0x8000)!=0;if(k5&&!p5){LONG on=!gfxOn;InterlockedExchange(&gfxOn,on);if(gfxObj)GfxApply(gfxObj);Log(on?"KEY F5: LOD = patch":"KEY F5: LOD = game");}p5=k5;}
 {static bool p11=false;bool k11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;if(k11&&!p11){LONG m=(gwOn+1)%3;InterlockedExchange(&gwOn,m);Log(m==1?"KEY F11: cloth on physics tick, smooth":m==2?"KEY F11: cloth on physics tick, latest step":"KEY F11: cloth as game");}p11=k11;}
}

// ---- WAITPROBE (Claude, diagnostic): which thread wakes the main thread, and how long the main thread waits ----
typedef DWORD (WINAPI *WfsoFn)(HANDLE,DWORD);typedef BOOL (WINAPI *SetEvFn)(HANDLE);
static WfsoFn wpWaitO;static SetEvFn wpSetO;static CRITICAL_SECTION wpLock;static bool wpReady=false;
struct WpSig{HANDLE h;DWORD tid;void* st[8];LONGLONG q;};static WpSig wpSig[256];
struct WpAgg{DWORD key[8];DWORD tid;LONG n;LONGLONG us,maxUs;};static WpAgg wpBySig[32];      // main waits at 0x8175cb, by waker stack
struct WpSite{DWORD ret;LONG n;LONGLONG us,maxUs;};static WpSite wpSites[32];                  // all main-thread waits, by caller
static LONGLONG wpFrameUs=0;static LONG wpFrames=0;
static BOOL WINAPI WpSetEvent(HANDLE h){
 if(wpReady){WpSig s;s.h=h;s.tid=GetCurrentThreadId();memset(s.st,0,sizeof s.st);RtlCaptureStackBackTrace(1,8,s.st,0);LARGE_INTEGER q;QueryPerformanceCounter(&q);s.q=q.QuadPart;
  EnterCriticalSection(&wpLock);wpSig[((DWORD)h>>2)&255]=s;LeaveCriticalSection(&wpLock);}
 return wpSetO(h);}
static DWORD WINAPI WpWait(HANDLE h,DWORD ms){
 if(!wpReady||GetCurrentThreadId()!=mainThreadId)return wpWaitO(h,ms);
 DWORD ret=(DWORD)_ReturnAddress();
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);DWORD r=wpWaitO(h,ms);QueryPerformanceCounter(&b);LONGLONG us=Us(b.QuadPart-a.QuadPart);
 EnterCriticalSection(&wpLock);
 {int i=0;for(;i<32;i++)if(wpSites[i].ret==ret||!wpSites[i].ret)break;if(i<32){WpSite& s=wpSites[i];s.ret=ret;s.n++;s.us+=us;if(us>s.maxUs)s.maxUs=us;}}
 if(us>200){WpSig& g=wpSig[((DWORD)h>>2)&255];if(g.h==h){DWORD k[8];for(int j=0;j<8;j++)k[j]=(DWORD)g.st[j];
   int i=0;for(;i<32;i++)if(!wpBySig[i].n||!memcmp(wpBySig[i].key,k,sizeof k))break;
   if(i<32){WpAgg& s=wpBySig[i];memcpy(s.key,k,sizeof k);s.tid=g.tid;s.n++;s.us+=us;if(us>s.maxUs)s.maxUs=us;}}}
 LeaveCriticalSection(&wpLock);return r;}
static void WpName(DWORD a,char* o,int cap){HMODULE m=0;if(a&&GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)a,&m)&&m&&m!=GetModuleHandleW(0)){char n[MAX_PATH];GetModuleFileNameA(m,n,MAX_PATH);char* l=strrchr(n,'\\');sprintf_s(o,cap,"%s+%lx",l?l+1:n,a-(DWORD)m);}else sprintf_s(o,cap,"%08lx",a);}
static DWORD WINAPI WpThread(LPVOID){
 for(;;){Sleep(2000);LogGpuSync();if(!wpReady)continue;
  WpSite sites[32];WpAgg sig[32];EnterCriticalSection(&wpLock);memcpy(sites,wpSites,sizeof sites);memcpy(sig,wpBySig,sizeof sig);memset(wpSites,0,sizeof wpSites);memset(wpBySig,0,sizeof wpBySig);LeaveCriticalSection(&wpLock);
  LONGLONG tot=0;for(int i=0;i<32;i++)tot+=sites[i].us;if(!tot)continue;
  char b[600];sprintf_s(b,"WAITPROBE 2s: main thread waited %.1f ms in WaitForSingleObject",tot/1000.0);Log(b);
  for(int k=0;k<5;k++){int best=-1;for(int i=0;i<32;i++)if(sites[i].n&&(best<0||sites[i].us>sites[best].us))best=i;if(best<0)break;
   char n[64];WpName(sites[best].ret,n,64);sprintf_s(b,"WAITPROBE  site %s: %ld waits, %.1f ms, max %.1f ms",n,sites[best].n,sites[best].us/1000.0,sites[best].maxUs/1000.0);Log(b);sites[best].n=0;}
  for(int k=0;k<5;k++){int best=-1;for(int i=0;i<32;i++)if(sig[i].n&&(best<0||sig[i].us>sig[best].us))best=i;if(best<0)break;
   char st[400];st[0]=0;for(int j=0;j<8;j++){char n[64];WpName(sig[best].key[j],n,64);strcat_s(st,n);if(j<7)strcat_s(st," <- ");}
   sprintf_s(b,"WAITPROBE  woken by tid %lu: %ld waits >0.2ms, %.1f ms, max %.1f ms | %s",sig[best].tid,sig[best].n,sig[best].us/1000.0,sig[best].maxUs/1000.0,st);Log(b);sig[best].n=0;}
 }}
static void InstallWaitProbe(){InitializeCriticalSection(&wpLock);
 bool a=HookImport("KERNEL32.dll","WaitForSingleObject",(void*)WpWait,(void**)&wpWaitO),c=HookImport("KERNEL32.dll","SetEvent",(void*)WpSetEvent,(void**)&wpSetO);
 if(a&&c){wpReady=true;CloseHandle(CreateThread(0,0,WpThread,0,0,0));}
 char b[100];sprintf_s(b,"WAITPROBE: hooks wait %d setevent %d",a,c);Log(b);}
static void StartDiagnostics(const Sites* s){
 if(s->stamp!=0x64a813d4){Log("DIAG: only available for the Steam 2023 build.");return;}
 InstallWaitProbe();
 InstallGpuSync();
 if(CfgIntW(L"Debug",L"FakeGpuVendor",0,configPath))FakeVendor();
 if(CfgIntW(L"Debug",L"CameraLog",1,configPath)){   // 0.3: per-frame camera log, previous session kept as camera.prev.csv
  wcscpy_s(camPath,logPath);wchar_t* sl=wcsrchr(camPath,L'\\');if(sl){wcscpy_s(sl+1,MAX_PATH-(sl+1-camPath),L"camera.csv");
   wchar_t prev[MAX_PATH];wcscpy_s(prev,camPath);wchar_t* dot=wcsrchr(prev,L'.');wcscpy_s(dot,MAX_PATH-(dot-prev),L".prev.csv");MoveFileExW(camPath,prev,MOVEFILE_REPLACE_EXISTING);
   Log("CAMPROBE: per-frame camera -> AC4Fix\\camera.csv (PageUp = mark)");}else camPath[0]=0;}
 AddVectoredExceptionHandler(1,CrashNote);
 InitializeCriticalSection(&diagLock);diagEnabled=1;bool ok=true;
 ok=SwapSlot(0x25db8dc+0x10,(void*)StepHook0,(void**)&origStep[0],0xc4b560)&&ok;
 ok=SwapSlot(0x25dba5c+0x10,(void*)StepHook1,(void**)&origStep[1],0xc4b560)&&ok;
 ok=SwapSlot(0x25dbb94+0x10,(void*)StepHook2,(void**)&origStep[2],0xc4b560)&&ok;
 ok=SwapSlot(0x25db8dc+0x0c,(void*)WholeHook0,(void**)&origWhole[0],0xc4a5a0)&&ok;
 ok=SwapSlot(0x25dba5c+0x0c,(void*)WholeHook1,(void**)&origWhole[1],0xc4a5a0)&&ok;
 ok=SwapSlot(0x25dbb94+0x0c,(void*)WholeHook2,(void**)&origWhole[2],0xc4a5a0)&&ok;
 ok=SwapSlot(0x25db8dc+0x1c,(void*)BeginHook,(void**)&origBegin,0xc430f0)&&ok;
 ok=SwapSlot(0x1e61868+0x80,(void*)ClothHook,(void**)&origCloth,0x6c4c10)&&ok;
 Log(ok?"DIAG: Havok and cloth counters active.":"DIAG: some counters could not be installed.");
 InitializeCriticalSection(&clothLock);clothLockReady=true;
 GetPrivateProfileStringA("Experiment","ThrottleJobs","Physic::LiteRagdollComponent::Update",jobList,sizeof(jobList),configPathA);
 jobHz=CfgIntW(L"Experiment",L"ThrottleHz",60,configPath);if(jobHz<10||jobHz>240)jobHz=60;
 // AC4Fix\Lab.ini (development only) survives reinstalls; Settings.ini is rewritten by the installer.
 {wchar_t lab[MAX_PATH];wcscpy_s(lab,configPath);wchar_t* sl=wcsrchr(lab,L'\\');if(sl)wcscpy_s(sl+1,MAX_PATH-(sl+1-lab),L"Lab.ini");
  int al=CfgIntW(L"Experiment",L"AutoLab",GetPrivateProfileIntW(L"Experiment",L"AutoLab",0,lab),configPath);if(al==1||al==2)InterlockedExchange(&labOn,al);if(al==2)InstallRagLab();
  if(al==2&&GetPrivateProfileIntW(L"Experiment",L"CorpseCollide",0,lab)==1){bool k=InstallCorpseCollide();if(k)InterlockedExchange(&corpseCollide,1);Log(k?"RAG COLLIDE: experiment on - ragdoll parts (layer 6) of different NPCs collide (layer table decides)":"RAG COLLIDE: filter code not recognised; experiment off");}
  if(al==2&&GetPrivateProfileIntW(L"Experiment",L"KeepPhysics",0,lab)==1){InterlockedExchange(&keepPhys,1);Log("RAG KEEPPHYS: experiment on - lying corpses keep their ragdoll physics (slow fade-out skipped)");}
  if(al==2&&GetPrivateProfileIntW(L"Experiment",L"KeepCorpses",0,lab)==1){bool k=InstallKeepCorpses();if(k)InterlockedExchange(&keepCorpses,1);Log(k?"RAG KEEP: experiment on - corpses stay in the physics world":"RAG KEEP: physics sync code not recognised; experiment off");}}
 Log(InstallJobHooks()?"DIAG: job profiler active (F6 = job gate experiment).":"DIAG: job executor not recognised; job profiler off.");
 InstallVelocityProbe();
 InstallClothProbe();
 HANDLE th=CreateThread(0,0,DiagThread,0,0,0);if(th)CloseHandle(th);
 th=CreateThread(0,0,BurstThread,0,0,0);if(th)CloseHandle(th);
 th=CreateThread(0,0,SpikeSampler,0,0,0);if(th)CloseHandle(th);
 th=CreateThread(0,0,MainProf,0,0,0);if(th)CloseHandle(th);
}
struct Change {BYTE* dst;BYTE data[16];BYTE old[16];unsigned size;};
static Change changes[64];static unsigned count=0;
static bool Queue(DWORD va,const BYTE* expected,const BYTE* replacement,unsigned size){
 if(size>16 || count>=sizeof(changes)/sizeof(changes[0]) || memcmp((void*)va,expected,size))return false;
 Change& c=changes[count++];c.dst=(BYTE*)va;c.size=size;memcpy(c.old,expected,size);memcpy(c.data,replacement,size);return true;
}
static bool Jump(DWORD va,const BYTE* expected,unsigned size,void* fn){
 BYTE data[16];memset(data,0x90,sizeof(data));data[0]=0xe9;*(DWORD*)(data+1)=(DWORD)fn-va-5;return Queue(va,expected,data,size);
}
// ---- v54: cloth damping made frame-rate independent (the real "30 FPS formula") --------------------
// Both cloth integrators (ClothLODInstance 0x541b00: dresses, coats, sails; SoftBodyLODInstance 0x57cfb0:
// flags, ropes) damp the Verlet velocity per axis as  v *= 1 - k*w  with  k = coefficient * dt*30.
// That linear scaling is exact only at 30 FPS. The sail coefficients are large: e.g. k = 1 at 30 FPS
// stops the motion against the wind every step (the sail stays filled and calm); at 141 FPS the same
// coefficient becomes 0.21 per step, i.e. only 1-(0.79^4.7) = 67% per 1/30 s - the sail sags back and
// flutters ("not filled, trembling"), dress hems swing too long. The exact conversion keeps the
// per-second effect of 30 FPS at any frame rate:  k' = 1 - (1 - min(k30,1))^(dt*30).  At 30 FPS k' == k.
// [Physics] ClothDampFix=1; F7 (Diagnostics) toggles it.
static volatile LONG dampCalls=0;
static void __stdcall DampConv(DWORD a,DWORD b,float d30){
 if(!dampFix||!(d30>0.0001f&&d30<4.0f))return;InterlockedIncrement(&dampCalls);
 DWORD at[2]={a,b};
 for(int i=0;i<2;i++){float* p=(float*)at[i];float K=*p;if(!(K>0.0f))continue;float k=K/d30;
  *p=k>=1.0f?1.0f:1.0f-powf(1.0f-k,d30);}
}
static void __stdcall DampCloth(DWORD ebpv){DampConv(ebpv-0x4c,ebpv-0x48,*(float*)(ebpv-4));}
static void __stdcall DampSoft(DWORD ebpv,DWORD ebxv){DampConv(ebpv-0x58,ebpv-0x54,*(float*)(ebxv+8)*30.0003f);}
static DWORD dcBack=0x541bcd,dsBack=0x57d07a;
__declspec(naked) static void DampClothHook(){__asm{
 movss dword ptr [ebp-48h],xmm0
 sub esp,128
 movups [esp],xmm0
 movups [esp+16],xmm1
 movups [esp+32],xmm2
 movups [esp+48],xmm3
 movups [esp+64],xmm4
 movups [esp+80],xmm5
 movups [esp+96],xmm6
 movups [esp+112],xmm7
 pushfd
 pushad
 push ebp
 call DampCloth
 popad
 popfd
 movups xmm0,[esp]
 movups xmm1,[esp+16]
 movups xmm2,[esp+32]
 movups xmm3,[esp+48]
 movups xmm4,[esp+64]
 movups xmm5,[esp+80]
 movups xmm6,[esp+96]
 movups xmm7,[esp+112]
 add esp,128
 jmp dword ptr [dcBack]
}}
__declspec(naked) static void DampSoftHook(){__asm{
 movss dword ptr [ebp-54h],xmm6
 sub esp,128
 movups [esp],xmm0
 movups [esp+16],xmm1
 movups [esp+32],xmm2
 movups [esp+64],xmm4
 movups [esp+80],xmm5
 movups [esp+112],xmm7
 pushfd
 pushad
 push ebx
 push ebp
 call DampSoft
 popad
 popfd
 movups xmm0,[esp]
 movups xmm1,[esp+16]
 movups xmm2,[esp+32]
 movups xmm4,[esp+64]
 movups xmm5,[esp+80]
 movups xmm7,[esp+112]
 add esp,128
 movss xmm3,dword ptr [ebp-58h]
 movss xmm6,dword ptr [ebp-54h]
 jmp dword ptr [dsBack]
}}
// ---- v56: pull-to-animation made frame-rate independent (ROOT CAUSE of flat, fluttering sails) ----------
// At the end of every step the cloth solver (0x53af60, ClothLODInstance: sails, dresses, coats) moves every free
// particle toward its animated position S by the fraction b = max(g=[inst+0x80], w_i=[[inst+4]+0x78][i]):
// P += b*(S-P). The fraction is per STEP, tuned for one step per 1/30 s. Measured (v55, ClothDump): sails have
// w ~0.11 on average and bulge 53-110 cm away from the animated pose at 30 FPS but only 10-21 cm at 141 FPS -
// at 141 FPS the pull runs 4.7 times as often and flattens the sail. Exact conversion for a step of dt:
// b' = 1 - (1-b)^(dt*30), identical at 30 FPS. dt comes from the step wrapper (thread-local, the solver has no dt).
// [Physics] ClothSkinFix=1; F7 (Diagnostics) toggles this together with the damping formula.
static volatile LONG skinCalls=0;static __declspec(thread) float tlsE30=1.0f;
static __declspec(thread) float skLutE=-1.0f;static __declspec(thread) float skLut[257];
static void __stdcall SkinConv(float* p){
 float x=*p;if(!(x>0.0f&&x<1.0f))return;float e=tlsE30;if(!(e>0.0001f&&e<4.0f)||fabsf(e-1.0f)<0.0005f)return;
 if(skLutE!=e){for(int i=0;i<=256;i++)skLut[i]=1.0f-powf(1.0f-i/256.0f,e);skLut[256]=1.0f;skLutE=e;}
 float f=x*256.0f;int i=(int)f;if(i>255)i=255;float t=f-i;*p=skLut[i]+(skLut[i+1]-skLut[i])*t;
}
static DWORD skBack=0x53b26e;
__declspec(naked) static void SkinPullHook(){__asm{
 movaps xmm3,xmm2
 maxps xmm3,xmm0
 cmp skinFix,0
 je sk_done
 sub esp,32
 movups [esp],xmm2
 movups [esp+16],xmm3
 pushad
 lea eax,[esp+48]
 push eax
 call SkinConv
 popad
 movups xmm2,[esp]
 movss xmm3,dword ptr [esp+16]
 shufps xmm3,xmm3,0
 add esp,32
 lock inc skinCalls
sk_done:
 movaps xmm0,xmmword ptr [edi+edx]
 jmp dword ptr [skBack]
}}
// ---- v61: our own thread solver for ClothLODInstance (sails, dresses, coats) -----------------------------
// Root cause (v53-v60): the game's solver 0x53af60 is plain PBD - every projection removes a thread's whole
// stretch, so how stretched (= how filled) a sail can be depends on the step length (wind stretches it by
// F*dt^2 per step) and the number of projections per step. Tuning it (v54-v59) or hiding it (gateway/smoothing,
// v51-v60) cannot make it right at every FPS. The standard cure is XPBD (Macklin, Mueller, Chentanez 2016): a
// thread gets a compliance alpha (softness in physical units) and a damping, the projection uses
//   dLambda = (-C - alpha~*lambda - gamma*vrel) / ((1+gamma)*W + alpha~),  alpha~ = alpha/dt^2, gamma = beta/dt,
// and the result no longer depends on dt or on the iteration count. Here the whole vtable slot +0x5c of
// ClothLODInstance is replaced by a faithful port of 0x53af60 (same order: body capsules, iterations, hard 1.5x
// stretch limit, 0x56df70, final pull to animation) in which only the thread projection is XPBD.
// alpha = xpC * (1/30 s)^2 (xpC = softness the 30 FPS game effectively has, ~0.5 from the v58 calibration),
// beta = xpD * (1/30 s) (damping of thread stretching speed, against the "jelly"). [Physics] ClothSolver=1.
typedef void(__thiscall*SolveFn)(void*);static SolveFn origSolve,origSolveSoft;
static volatile LONG xpCalls=0,xpEval=0,xpLimit=0;static __declspec(thread) LONG tlsStepSerial=0;
struct XpInst{DWORD self;LONG serial;int cap;float* lam;LONGLONG last;};static XpInst xpTab[1024];static CRITICAL_SECTION xpLock;static bool xpReady=false;
static float* XpLambdas(void* self,int count){
 LONGLONG now=QpcNow();UINT h=(((DWORD)self>>4)*2654435761u)>>22;XpInst* e=0;XpInst* fr=0;
 EnterCriticalSection(&xpLock);
 for(int i=0;i<16;i++){XpInst& c=xpTab[(h+i)&1023];if(c.self==(DWORD)self){e=&c;break;}if(!fr&&(!c.self||now-c.last>2*frequency))fr=&c;}
 if(!e&&fr){if(fr->lam)HeapFree(GetProcessHeap(),0,fr->lam);memset(fr,0,sizeof(*fr));fr->self=(DWORD)self;fr->serial=-1;e=fr;}
 LeaveCriticalSection(&xpLock);if(!e)return 0;
 e->last=now;
 if(e->cap<count){if(e->lam)HeapFree(GetProcessHeap(),0,e->lam);e->lam=(float*)HeapAlloc(GetProcessHeap(),0,count*sizeof(float));e->cap=e->lam?count:0;if(!e->lam)return 0;}
 memset(e->lam,0,count*sizeof(float));   // one solver call = one step: lambdas start at 0
 return e->lam;
}
static inline void VCall60(void* self){typedef void(__thiscall*F)(void*);F f=*(F*)(*(BYTE**)self+0x60);f(self);}
// v67: the same solver for SoftBodyLODInstance (flags, ropes; game solver 0x577660): identical structure but its
// own flag bytes (0x2ac179c/0x2ac179b/0x276db94/0x2ac179a), no hard stretch limit inside the iterations and no
// pull toward animation (flags have no animated pose).
static void XpSolveCore(void* self,int soft);
static void __fastcall XpSolve(void* self,void*){if(!xpOn){origSolve(self);return;}XpSolveCore(self,0);}
static void __fastcall XpSolveSoft(void* self,void*){if(!xpOn||!xpSoftOn){origSolveSoft(self);return;}XpSolveCore(self,1);}
static void XpSolveCore(void* self,int soft){
 SolveFn orig=soft?origSolveSoft:origSolve;
 BYTE* s=(BYTE*)self;DWORD ctx=*(DWORD*)(s+0x54);BYTE* de=ctx?*(BYTE**)(ctx+0xc4):0;if(!de){orig(self);return;}
 volatile BYTE* g62e=(BYTE*)(soft?0x2ac179c:0x2ac162e);volatile BYTE* g62d=(BYTE*)(soft?0x2ac179b:0x2ac162d);volatile BYTE* g6ba2=(BYTE*)(soft?0x276db94:0x2766ba2);volatile BYTE* g62c=(BYTE*)(soft?0x2ac179a:0x2ac162c);
 if(de[0x3e]&&*(int*)(de+0x68)==2){*g62e=0;*g62d=1;*g6ba2=0;*g62c=0;}else if(*g62e)VCall60(self);
 float maxS2=*(float*)(de+0x2c);maxS2*=maxS2;int iters=(*(WORD*)(de+0x3c)>>11)&7;
 int nc=*(WORD*)(s+0x52);BYTE* recs=*(BYTE**)(s+0x4c);float* P=*(float**)(s+0xc);const float* Pp=*(const float**)(s+0x14);const BYTE* pin=*(const BYTE**)(s+0x34);
 if(!P||!Pp||!pin||(nc&&!recs)){orig(self);return;}
 float dt=tlsE30/30.0003f;if(!(dt>0.0005f&&dt<0.2f))dt=1.0f/30;const float h30=1.0f/30;
 float at=xpC*(h30*h30)/(dt*dt);float gm=xpD*h30/dt;   // alpha~ and gamma for this step
 float* lam=XpLambdas(self,nc>0?nc:1);if(!lam){orig(self);return;}
 InterlockedIncrement(&xpCalls);LONG nEval=0,nLim=0;
 for(int it=0;it<iters;it++){
  if(*g6ba2)VCall60(self);
  for(int j=0;j<nc;j++){
   const BYTE* rc=recs+j*12;int a=*(const WORD*)rc,b=*(const WORD*)(rc+2);int pa=pin[a]&1,pb=pin[b]&1;if(pa&&pb)continue;
   float* A=P+a*4;float* B=P+b*4;float dx=B[0]-A[0],dy=B[1]-A[1],dz=B[2]-A[2];float L2=dx*dx+dy*dy+dz*dz;float r2=*(const float*)(rc+4);
   if(!(L2>1e-12f)||!(r2>0))continue;float L=sqrtf(L2),r=sqrtf(r2);
   nEval++;
   if(!soft&&L2>r2*maxS2){   // hard stretch limit exactly as the game (cloth only)
    nLim++;
    float m=sqrtf(r2*maxS2/L2);
    if(!pa&&!pb){float cx=(A[0]+B[0])*0.5f,cy=(A[1]+B[1])*0.5f,cz=(A[2]+B[2])*0.5f;A[0]=cx-m*dx*0.5f;A[1]=cy-m*dy*0.5f;A[2]=cz-m*dz*0.5f;B[0]=cx+m*dx*0.5f;B[1]=cy+m*dy*0.5f;B[2]=cz+m*dz*0.5f;}
    else if(pa){B[0]=A[0]+m*dx;B[1]=A[1]+m*dy;B[2]=A[2]+m*dz;}else{A[0]=B[0]-m*dx;A[1]=B[1]-m*dy;A[2]=B[2]-m*dz;}
    lam[j]=0;continue;}
   float C=L-r;
   if(C<0){float slack=(*(const WORD*)(rc+8))*1.5259022e-5f;float rc2=r*(1.0f-slack);C=L<rc2?L-rc2:0.0f;}   // allowed compression
   float nx=dx/L,ny=dy/L,nz=dz/L;float wa=pa?0.0f:1.0f,wb=pb?0.0f:1.0f,W=wa+wb;
   const float* Ap=Pp+a*4;const float* Bp=Pp+b*4;
   float vrel=((B[0]-Bp[0])-(A[0]-Ap[0]))*nx+((B[1]-Bp[1])-(A[1]-Ap[1]))*ny+((B[2]-Bp[2])-(A[2]-Ap[2]))*nz;
   if(C==0.0f&&vrel>=0.0f&&lam[j]==0.0f)continue;
   float dl=(-C-at*lam[j]-gm*vrel)/((1.0f+gm)*W+at);lam[j]+=dl;
   A[0]-=wa*nx*dl;A[1]-=wa*ny*dl;A[2]-=wa*nz*dl;B[0]+=wb*nx*dl;B[1]+=wb*ny*dl;B[2]+=wb*nz*dl;
  }
  if(*g62c)VCall60(self);
 }
 InterlockedExchangeAdd(&xpEval,nEval);InterlockedExchangeAdd(&xpLimit,nLim);
 // v65: ripple damping. At 30 FPS a 33 ms step cannot carry waves faster than ~15 Hz - they die out; at 141 FPS
 // fine ripples running across the sail survive (the flutter). Neighbours joined by a thread are slowed relative
 // to each other in ALL directions (the thread damping above only acts along the thread, ripples are across it):
 // v_a,v_b -> toward their common velocity by k = 1-exp(-xpK*dt) per step (xpK per second, FPS independent).
 // Motion of the whole sail (filling, slow sway) moves neighbours together and is hardly touched.
 {float kk=soft?xpKSoft:xpK;if(kk>0.0f){float k=1.0f-expf(-kk*dt);
  for(int j=0;j<nc;j++){const BYTE* rc=recs+j*12;int a=*(const WORD*)rc,b=*(const WORD*)(rc+2);int pa=pin[a]&1,pb=pin[b]&1;if(pa&&pb)continue;
   float* A=P+a*4;float* B=P+b*4;const float* Ap=Pp+a*4;const float* Bp=Pp+b*4;
   for(int c=0;c<3;c++){float dv=(B[c]-Bp[c])-(A[c]-Ap[c]);
    if(!pa&&!pb){A[c]+=0.5f*k*dv;B[c]-=0.5f*k*dv;}else if(pa)B[c]-=k*dv;else A[c]+=k*dv;}}}}
 {typedef void(__thiscall*F)(void*);((F)0x56df70)(self);}
 if(*g62d)VCall60(self);
 // final pull toward the animated pose, per step as the game (converted per dt when ClothSkinFix is on)
 if(!soft){float g=*(float*)(s+0x80);DWORD Ld=*(DWORD*)(s+4);const float* w=Ld?*(const float**)(Ld+0x78):0;const float* Sa=*(const float**)(s+0x9c);int n=*(WORD*)(s+0x12);
  if(w&&Sa)for(int i=0;i<n;i++){if(pin[i]&1)continue;float bb=w[i]>g?w[i]:g;SkinConv(&bb);float* p=P+i*4;const float* q=Sa+i*4;p[0]+=bb*(q[0]-p[0]);p[1]+=bb*(q[1]-p[1]);p[2]+=bb*(q[2]-p[2]);p[3]+=bb*(q[3]-p[3]);}}
}
static bool InstallXpbd(){InitializeCriticalSection(&xpLock);xpReady=true;bool a=SwapSlot(0x1e4e760+0x5c,(void*)XpSolve,(void**)&origSolve,0x53af60);bool b=SwapSlot(0x1e50ce0+0x5c,(void*)XpSolveSoft,(void**)&origSolveSoft,0x577660);if(!b)xpSoftOn=0;return a;}
static void XpSet(float c,float d){xpC=c;xpD=d;}
static bool InstallSkinFix(){static const BYTE b[10]={0x0f,0x28,0xda,0x0f,0x5f,0xd8,0x0f,0x28,0x04,0x17};return Jump(0x53b264,b,10,SkinPullHook);}
// ---- v57: constraint iterations per SECOND as at 30 FPS --------------------------------------------------
// The solvers (0x53af60 cloth/sails/dresses, 0x577660 flags) run N projections of all threads per STEP
// (N = bits 11-13 of [settings+0x3c]: sails 5, dresses/flags 3). At 30 FPS that is 30*N per second; at 141 FPS
// 141*N: the cloth is held almost exactly at its rest lengths and cannot stretch. A filled sail IS a stretched
// sail (up to the 1.5x stretch limit), so at high FPS sails stay flat (v56 data: |P-S| 37 cm vs 93 cm at 30 FPS
// even with the pull fixed). Here N per step = round(N * dt*30), at least 1: the same projections per second.
static volatile LONG iterIn=0,iterOut=0;
static int __stdcall IterConv(int n){
 InterlockedExchangeAdd(&iterIn,n);
 if(iterFix&&n>0){float e=tlsE30;if(e>0.0001f&&e<4.0f&&fabsf(e-1.0f)>0.0005f){int m=(int)(n*e+0.5f);if(m<1)m=1;if(m>n*2)m=n*2;n=m;}}
 InterlockedExchangeAdd(&iterOut,n);return n;
}
static DWORD itBack1=0x53b000,itBack2=0x5776f0;
__declspec(naked) static void IterClothHook(){__asm{
 sub esp,128
 movups [esp],xmm0
 movups [esp+16],xmm1
 movups [esp+32],xmm2
 movups [esp+48],xmm3
 movups [esp+64],xmm4
 movups [esp+80],xmm5
 movups [esp+96],xmm6
 movups [esp+112],xmm7
 push ecx
 push edx
 push eax
 call IterConv
 pop edx
 pop ecx
 movups xmm0,[esp]
 movups xmm1,[esp+16]
 movups xmm2,[esp+32]
 movups xmm3,[esp+48]
 movups xmm4,[esp+64]
 movups xmm5,[esp+80]
 movups xmm6,[esp+96]
 movups xmm7,[esp+112]
 add esp,128
 mov dword ptr [ebp-10h],eax
 jmp dword ptr [itBack1]
}}
__declspec(naked) static void IterSoftHook(){__asm{
 sub esp,128
 movups [esp],xmm0
 movups [esp+16],xmm1
 movups [esp+32],xmm2
 movups [esp+48],xmm3
 movups [esp+64],xmm4
 movups [esp+80],xmm5
 movups [esp+112],xmm7
 push ecx
 push edx
 push eax
 call IterConv
 pop edx
 pop ecx
 movups xmm0,[esp]
 movups xmm1,[esp+16]
 movups xmm2,[esp+32]
 movups xmm3,[esp+48]
 movups xmm4,[esp+64]
 movups xmm5,[esp+80]
 movups xmm7,[esp+112]
 add esp,128
 xorps xmm6,xmm6
 mov dword ptr [ebp-8],eax
 jmp dword ptr [itBack2]
}}
static bool InstallIterFix(){
 static const BYTE c[10]={0x89,0x45,0xf0,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00},sb[6]={0x0f,0x57,0xf6,0x89,0x45,0xf8};
 bool a=Jump(0x53aff6,c,10,IterClothHook),b=Jump(0x5776ea,sb,6,IterSoftHook);return a&&b;
}
static bool InstallDampFix(){
 static const BYTE c[5]={0xf3,0x0f,0x11,0x45,0xb8},s5[5]={0xf3,0x0f,0x11,0x75,0xac};
 bool a=Jump(0x541bc8,c,5,DampClothHook),b=Jump(0x57d075,s5,5,DampSoftHook);
 return a&&b;
}
static void LogDampFix(){LONG c=InterlockedExchange(&dampCalls,0),k=InterlockedExchange(&skinCalls,0);char b[360];sprintf_s(b,"CLOTH 30FPS FORMULAS 10s: damping %s (%ld steps), pull to animation %s (%ld particle pulls), iterations %s (%ld -> %ld), own solver %s (%ld steps, hard 1.5x limit hit in %.2f%% of thread solves)",dampFix?"on":"game",c,skinFix?"on":"game",k,iterFix?"on":"game",InterlockedExchange(&iterIn,0),InterlockedExchange(&iterOut,0),xpOn?"on":"off",InterlockedExchange(&xpCalls,0),[](){LONG e=InterlockedExchange(&xpEval,0),l=InterlockedExchange(&xpLimit,0);return e?100.0*l/e:0.0;}());Log(b);}

// ---- Havok physics rate ---------------------------------------------------------------------
// The game steps Havok asynchronously with a fixed step read from a global that the physics
// world init sets to 1/30 s (movss xmm0,[1/30]; ...; movss [step],xmm0). Nothing interpolates
// between steps, so bodies, ragdolls and everything attached to them move in 30 Hz jumps even
// at 141 FPS. Pointing that load at our own constant makes Havok step at 1/HavokRate instead.
static float havokStep=1.0f/60;
static bool QueueHavokRate(int rate){
 static const BYTE head[]={0xf3,0x0f,0x10,0x05};static const BYTE mid[]={0x33,0xc0,0xf3,0x0f,0x11,0x05};static const BYTE tail[]={0x89,0x87,0xa8,0,0,0};
 BYTE* image=(BYTE*)GetModuleHandleW(0);IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
 IMAGE_SECTION_HEADER* sec=IMAGE_FIRST_SECTION(nt);BYTE* hit=0;int hits=0;
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)if(sec[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){
  BYTE* p=image+sec[i].VirtualAddress;DWORD n=sec[i].Misc.VirtualSize;
  for(DWORD k=0;k+24<=n;k++)if(p[k]==0xf3&&!memcmp(p+k,head,4)&&!memcmp(p+k+8,mid,6)&&!memcmp(p+k+18,tail,6)){hit=p+k;hits++;}
 }
 if(hits!=1){Log("Physics: Havok step not found; physics rate unchanged.");return true;}
 float* src=*(float**)(hit+4);if(fabs(*src-1.0f/30)>1e-6f){Log("Physics: unexpected Havok step; physics rate unchanged.");return true;}
 havokStep=1.0f/rate;BYTE now[4];memcpy(now,hit+4,4);DWORD ours=(DWORD)&havokStep;
 if(!Queue((DWORD)(hit+4),now,(BYTE*)&ours,4))return false;
 char b[64];sprintf_s(b,"Physics: Havok steps at %d Hz (game default 30).",rate);Log(b);return true;
}
static bool Apply(){
 unsigned i=0;
 for(;i<count;i++){
  DWORD old;Change& c=changes[i];if(!VirtualProtect(c.dst,c.size,PAGE_EXECUTE_READWRITE,&old))break;
  memcpy(c.dst,c.data,c.size);FlushInstructionCache(GetCurrentProcess(),c.dst,c.size);DWORD ignored;VirtualProtect(c.dst,c.size,old,&ignored);
 }
 if(i==count)return true;
 while(i){Change& c=changes[--i];DWORD old;if(VirtualProtect(c.dst,c.size,PAGE_EXECUTE_READWRITE,&old)){memcpy(c.dst,c.old,c.size);FlushInstructionCache(GetCurrentProcess(),c.dst,c.size);DWORD ignored;VirtualProtect(c.dst,c.size,old,&ignored);}}
 return false;
}

// v39: cloth anchor smoothing ([Physics] ClothAnchorSmooth=1, F11 toggles with Diagnostics).
// ClothLODInstance::Step (0x53bb90) first rebuilds the cloth's attachment matrices from bones (0x53b2a0:
// this+0x84 count WORD +0x8a, this+0x8c count +0x92, 64 bytes each), then steps the cloth. Log 21:3x
// (sailing): at 141 FPS the anchors changed in only 12-16% of cloth steps (~20 times a second) and 16-22%
// of the changes went BACK (at 60 FPS 18-29% / 3-11%): the cloth saw "still, still, jump (, jump back)"
// - the residual shiver of sails and dresses at high FPS. Here, right after the rebuild, each anchor is
// led linearly from where it was shown to its new pose over the interval between two pose changes, and a
// change back towards the previous pose (stale bones read by a parallel thread) is ignored. A jump over
// 1 m, a pause over 0.25 s or a new anchor count snaps (no smoothing).
static float CaDist2(const float* a,const float* b){float dx=a[12]-b[12],dy=a[13]-b[13],dz=a[14]-b[14];return dx*dx+dy*dy+dz*dz;}
static void __stdcall ClothAnchorNote(DWORD self){
 if(!caOn||!caTab)return;
 LONGLONG now=QpcNow();CaInst* c=0;
 EnterCriticalSection(&caLock);
 {UINT h=(self>>4)*2654435761u%CA_N;int fr=-1;for(int i=0;i<16;i++){CaInst& e=caTab[(h+i)%CA_N];if(e.self==self){c=&e;break;}if(fr<0&&(!e.self||now-e.last>2*frequency))fr=(h+i)%CA_N;}
  if(!c&&fr>=0){c=&caTab[fr];c->self=self;c->last=0;}}
 LeaveCriticalSection(&caLock);
 if(!c)return;
 __try{
  DWORD a1=*(DWORD*)(self+0x84),a2=*(DWORD*)(self+0x8c);WORD n1=*(WORD*)(self+0x8a),n2=*(WORD*)(self+0x92);
  if(n1>32)n1=32;if(n2>32)n2=32;if(!a1)n1=0;if(!a2)n2=0;int n=n1+n2;if(!n)return;
  float* src[64];for(int i=0;i<n1;i++)src[i]=(float*)(a1+64*i);for(int i=0;i<n2;i++)src[n1+i]=(float*)(a2+64*i);
  bool snap=!c->last||now-c->last>frequency/4||c->n1!=n1||c->n2!=n2;
  if(!snap)for(int i=0;i<n;i++)if(CaDist2(src[i],c->B[i])>1.0f){snap=true;break;}
  c->last=now;
  if(snap){for(int i=0;i<n;i++){memcpy(c->A[i],src[i],64);memcpy(c->B[i],src[i],64);}c->n1=n1;c->n2=n2;c->tB=now;c->period=1.0f/30;InterlockedIncrement(&caSnaps);return;}
  InterlockedIncrement(&caSteps);
  float u=c->period>0?(float)((double)(now-c->tB)/frequency)/c->period:1.0f;if(u>1)u=1;if(u<0)u=0;
  // did the pose change since the last rebuild?
  float maxd=0,back=0;for(int i=0;i<n;i++){float d=CaDist2(src[i],c->B[i]);if(d>maxd)maxd=d;}
  if(maxd>1e-10f){
   // back towards A (previous pose) and closer to it than to B -> stale bones, ignore
   bool isBack=true;for(int i=0;i<n&&isBack;i++){float dA=CaDist2(src[i],c->A[i]),dB=CaDist2(src[i],c->B[i]);if(!(dA<dB*0.25f&&CaDist2(c->A[i],c->B[i])>1e-10f))isBack=false;}
   if(isBack){InterlockedIncrement(&caBack);}
   else{float p=(float)((double)(now-c->tB)/frequency);if(p<0.004f)p=0.004f;if(p>0.1f)p=0.1f;
    for(int i=0;i<n;i++){for(int k=0;k<16;k++)c->A[i][k]=c->A[i][k]+(c->B[i][k]-c->A[i][k])*u;memcpy(c->B[i],src[i],64);}
    c->period=p;c->tB=now;u=0;InterlockedIncrement(&caChanged);InterlockedExchangeAdd64(&caPeriodUs,(LONGLONG)(p*1e6f));}
  }
  for(int i=0;i<n;i++)for(int k=0;k<16;k++)src[i][k]=c->A[i][k]+(c->B[i][k]-c->A[i][k])*u;
 }__except(EXCEPTION_EXECUTE_HANDLER){}
}
static DWORD caOrig=0x53b2a0;
__declspec(naked) static void ClothAnchorWrap(){__asm{
 push ecx
 push dword ptr [esp+12]
 push dword ptr [esp+12]
 call dword ptr [caOrig]
 call ClothAnchorNote
 ret 8
}}
static bool InstallClothAnchorSmooth(){
 BYTE* at=(BYTE*)0x53bbae;if(at[0]!=0xe8||(DWORD)(0x53bbb3+*(int*)(at+1))!=0x53b2a0)return false;
 caTab=(CaInst*)VirtualAlloc(0,sizeof(CaInst)*CA_N,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!caTab)return false;
 InitializeCriticalSection(&caLock);
 BYTE code[5];code[0]=0xe8;*(DWORD*)(code+1)=(DWORD)ClothAnchorWrap-0x53bbb3;
 DWORD old;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;memcpy(at,code,5);FlushInstructionCache(GetCurrentProcess(),at,5);DWORD ig;VirtualProtect(at,5,old,&ig);
 InterlockedExchange(&caOn,1);return true;
}
static void LogClothAnchor(){
 if(!caTab)return;LONG st=InterlockedExchange(&caSteps,0),ch=InterlockedExchange(&caChanged,0),bk=InterlockedExchange(&caBack,0),sn=InterlockedExchange(&caSnaps,0);LONGLONG pu=InterlockedExchange64(&caPeriodUs,0);
 char b[220];sprintf_s(b,"CLOTH ANCHORS 10s (smoothing %s): steps %ld, pose changes %ld (%.0f%%), avg interval %.1f ms, back-jumps ignored %ld, snaps %ld",caOn?"ON":"off",st,ch,st?100.0*ch/st:0.0,ch?pu/1000.0/ch:0.0,bk,sn);Log(b);
}

// v40: LOD distance ([Graphics] LODScale, F5 toggles with Diagnostics). Every LODSelector picks its level in
// 0x867450 (first of up to 5 LODDescriptor thresholds above the given distance). The caller 0x869560 takes
// the camera distance to the object and multiplies it by the renderer's global LOD factor [ctx+0x348]
// (0x86967f) before that. Our factor is applied on top: 0.5 = every model keeps its detailed level twice as
// far ("square people" further away). Objects flagged to ignore the global factor are left alone.
static volatile LONG lodCalls[2];static DWORD lodBack2=0x86b728;
__declspec(naked) static void LodHook(){__asm{
 mulss xmm2,dword ptr [edx+348h]
 mov lodCtx,edx
 lock inc lodCalls
 mulss xmm2,dword ptr [lodScale]
 jmp dword ptr [lodBack]
}}
// v43: the second LOD path (0x86b037, its own copy of the threshold loop at 0x86b759) loads the global
// factor at 0x86b720 (movss xmm0,[edi+348h]) and multiplies the distance by it.
__declspec(naked) static void LodHook2(){__asm{
 movss xmm0,dword ptr [edi+348h]
 mov lodCtx,edi
 lock inc lodCalls+4
 mulss xmm0,dword ptr [lodScale]
 jmp dword ptr [lodBack2]
}}
static void LogLod(){
 static float last=-1;float g=0;DWORD c=lodCtx;if(!c)return;__try{g=*(float*)(c+0x348);}__except(EXCEPTION_EXECUTE_HANDLER){return;}
 LONG a=InterlockedExchange(&lodCalls[0],0),b2=InterlockedExchange(&lodCalls[1],0);
 {char b[200];sprintf_s(b,"LOD 10s: game distance factor %.3f, patch factor %.2f | selections path A (0x869560) %ld, path B (0x86b037) %ld",g,lodScale,a,b2);Log(b);}
}

// v41 experiment (F11 with Diagnostics, [Physics] ClothWindMode): every cloth substep the pre-substep
// 0x573110 (vtable +0x54 of ClothLODInstance 0x1e4e760 and SoftBodyLODInstance 0x1e50ce0) adds to each
// particle the wind plus a fresh random turbulence from the global LCG ([0x4dd992c]+4). At 141 FPS the
// cloth gets 141 different gusts a second, at 60 FPS 60. Modes (v49b): 0 = game; 1/2 = turbulence re-rolled at most
// 30 (consoles, the speed the cloth was tuned for) / 60 times a second per cloth (between rolls the same LCG seed is replayed -> same gusts; the global
// sequence is restored afterwards so nothing else is affected); 3 = cloth wind and turbulence off.
typedef void(__thiscall*PreSubFn)(void*);static PreSubFn origPreSub[2];
struct WindHold{DWORD self;DWORD seed;LONGLONG t;};static WindHold windHold[512];
static void PreSubCommon(int k,void* self){
 if(windMode!=1&&windMode!=2){origPreSub[k](self);return;}
 LONGLONG hold=frequency/(windMode==1?30:60);
 DWORD g=*(DWORD*)0x4dd992c;DWORD* st=(DWORD*)(g+4);LONGLONG now=QpcNow();
 UINT h=((DWORD)self>>4)*2654435761u%512;WindHold* w=0;
 for(int i=0;i<8;i++){WindHold& e=windHold[(h+i)%512];if(e.self==(DWORD)self||!e.self||now-e.t>frequency){w=&e;break;}}
 if(!w){origPreSub[k](self);return;}
 if(w->self!=(DWORD)self||now-w->t>=hold){w->self=(DWORD)self;w->t=now;w->seed=*st;origPreSub[k](self);return;}
 DWORD cur=*st;*st=w->seed;origPreSub[k](self);*st=cur;
}
static void __fastcall PreSub0(void* self,void*){PreSubCommon(0,self);}
static void __fastcall PreSub1(void* self,void*){PreSubCommon(1,self);}
static void SetWindMode(int m){
 if(m==3)SetClothWind(true);else SetClothWind(false);
 InterlockedExchange(&windMode,m);
}
// ---- v51: cloth "30 FPS gateway" ------------------------------------------------------------------
// The cloth (ClothLODInstance, dresses/sails/coats) and soft bodies (SoftBodyLODInstance, flags/ropes)
// were tuned at 30 FPS: one Verlet step of 1/30 s per frame. Their inputs - bone anchors, the LiteRagdoll
// body capsules they collide with, wind - are sampled once per step. At 141 FPS the cloth steps 141
// times a second against inputs that partly change only ~30 times a second (Havok/animation rate):
// the free edges (hems, sail edges) tremble. Instead of chasing each input, the gateway makes the cloth
// run EXACTLY as at 30 FPS: the per-instance update (vtable +0x48, called from 0x569ff0 with the frame
// time) is accumulated and the original step runs with dt = 1/30 s whenever 1/30 s has passed. Between
// steps the particles shown are interpolated between the last two steps (particles live in a space
// relative to the owner [owner+0x120]; the renderer reads [inst+0xc] directly), so the motion stays
// smooth at any frame rate. The per-frame "follow" shift (0x56e290, same vector for every particle)
// that happens between our calls is detected on particle 0 and carried into both stored states.
// [Physics] Cloth30=1; F11 (Diagnostics) toggles gateway / game.
typedef void(__thiscall*ClStepFn)(void*,float,int);static ClStepFn origClStep[2];
struct GwInst{DWORD self;float* P;WORD n;BYTE valid;LONG tick;float acc;float RA[12],RB[12];float* SA;float* SB;BYTE skinMode;LONGLONG last;float* A;float* B;float* D;};
static GwInst gwTab[1024];static volatile LONG gwSteps=0,gwFrames=0,gwInits=0,gwFull=0;static CRITICAL_SECTION gwLock;static bool gwLockReady=false;
static GwInst* GwFind(DWORD self,LONGLONG now){
 UINT h=((self>>4)*2654435761u)>>22;GwInst* freeE=0;
 for(int i=0;i<16;i++){GwInst& e=gwTab[(h+i)&1023];if(e.self==self)return &e;
  if(!freeE&&(!e.self||now-e.last>2*frequency))freeE=&e;}
 if(!freeE){InterlockedIncrement(&gwFull);return 0;}
 if(freeE->A){HeapFree(GetProcessHeap(),0,freeE->A);}
 memset(freeE,0,sizeof(*freeE));freeE->self=self;freeE->last=now;return freeE;
}
// ---- v55 engine teardown: per-instance cloth data from the running game (Diagnostics only) ------------
// For every cloth instance stepped in a 10 s window: class, particles, blend-to-animation g=[inst+0x80] and the
// per-particle weights w=[[inst+4]+0x78][i] (the solver pulls every free particle to its animated position S by
// max(g,w) EVERY STEP), the cloth settings [ctx+0xc4] (+0x14 warm-up, +0x18/+0x1c/+0x60/+0x64 damping, +0x2c max
// stretch, +0x30, +0x34/+0x38 wind, iterations bits 11-13 of +0x3c), wind ctx+0x90, ctx+0x160, how often the
// animated pose S really changes between steps, and how far the simulated cloth hangs from it (|P-S|).
struct CdEnt{DWORD self;int cls;WORD n;LONG steps,sChg;double dist;LONG distN;float s0[3];float g,wMax,wAvg;LONG wNz;float d[12];float wind;float c160;float dtSum;double m1,m2;LONG mN;
 // v62: burstiness of the inputs inside 1/30 s windows (share of the motion done in the biggest step: ~21% smooth at 141 FPS, ~100% = 30 Hz jumps)
 float lastS[3],lastW[3];LONGLONG winT;float sSum,sMax,wiSum,wiMax;double sTot,sMaxTot,wiTot,wiMaxTot;
 // jitter of the cloth relative to its animation sampled every 1/30 s (same sampling at any FPS): 2nd difference RMS
 float oA[8][3],oB[8][3];LONGLONG oT;int oN;double jit;LONG jitN;
 // v63: step-to-step zigzag of the same 8 particles (absolute positions): 2nd difference per step (mm) and how
 // often its sign flips between consecutive steps (oscillation at the step rate; smooth motion flips rarely)
 float zA[8][3],zB[8][3],zD[8][3];int zN;double zig;LONG zigN,flip,flipN;};
static CdEnt cdTab[256];static CRITICAL_SECTION cdLock;static bool cdReady=false;static volatile LONG cdOn=0;
static void ClothDumpNote(int cls,void* self,float dt){
 if(!cdOn||!cdReady)return;
 __try{
  BYTE* s=(BYTE*)self;WORD n=*(WORD*)(s+0x12);if(!n||n>8192)return;DWORD ctx=*(DWORD*)(s+0x54);if(!ctx)return;DWORD de=*(DWORD*)(ctx+0xc4);
  EnterCriticalSection(&cdLock);
  UINT h=(((DWORD)self>>4)*2654435761u)>>24;CdEnt* e=0;for(int i=0;i<8;i++){CdEnt& c=cdTab[(h+i)&255];if(c.self==(DWORD)self){e=&c;break;}if(!c.self){e=&c;break;}}
  if(!e){LeaveCriticalSection(&cdLock);return;}
  if(e->self!=(DWORD)self){memset(e,0,sizeof(*e));e->self=(DWORD)self;e->cls=cls;e->n=n;
   if(de){const DWORD offs[12]={0x14,0x18,0x1c,0x2c,0x30,0x34,0x38,0x60,0x64,0x68,0x3c,0x3e};for(int i=0;i<12;i++)e->d[i]=(i>=10)?(float)(i==10?((*(WORD*)(de+0x3c)>>11)&7):*(BYTE*)(de+0x3e)):*(float*)(de+offs[i]);}
   if(cls==0){e->g=*(float*)(s+0x80);DWORD L=*(DWORD*)(s+4);const float* w=L?*(const float**)(L+0x78):0;
    if(w){double sum=0;for(int i=0;i<n;i++){float v=w[i];if(v>e->wMax)e->wMax=v;sum+=v;if(v>0.0001f)e->wNz++;}e->wAvg=(float)(sum/n);}}}
  e->steps++;e->dtSum+=dt;
  {LONGLONG nowq=QpcNow();const float* wv=(const float*)(ctx+0x90);
   if(cls==0){const float* S0=*(const float**)(s+0x9c);if(S0){int k=n/2;const float* q=S0+k*4;
    if(e->steps>1){float dx=q[0]-e->lastS[0],dy=q[1]-e->lastS[1],dz=q[2]-e->lastS[2];float m=sqrtf(dx*dx+dy*dy+dz*dz);e->sSum+=m;if(m>e->sMax)e->sMax=m;
     float ex=wv[0]-e->lastW[0],ey=wv[1]-e->lastW[1],ez=wv[2]-e->lastW[2];float mw=sqrtf(ex*ex+ey*ey+ez*ez);e->wiSum+=mw;if(mw>e->wiMax)e->wiMax=mw;}
    memcpy(e->lastS,q,12);memcpy(e->lastW,wv,12);
    if(!e->winT)e->winT=nowq;if(nowq-e->winT>=frequency/30){e->sTot+=e->sSum;e->sMaxTot+=e->sMax;e->wiTot+=e->wiSum;e->wiMaxTot+=e->wiMax;e->sSum=e->sMax=e->wiSum=e->wiMax=0;e->winT=nowq;}
    // cloth offset from animation, 8 particles, sampled every 1/30 s
    const float* P0=*(const float**)(s+0xc);const BYTE* pn=*(const BYTE**)(s+0x34);
    if(P0){int st2=n/9>0?n/9:1;float o[8][3];for(int i=0;i<8;i++){int id=(i+1)*st2;if(id>=n)id=n-1;for(int c=0;c<3;c++)o[i][c]=P0[id*4+c];}
     if(e->zN>=2){double a=0;for(int i=0;i<8;i++)for(int c=0;c<3;c++){float d2=o[i][c]-2*e->zB[i][c]+e->zA[i][c];a+=d2*d2;
       if(e->zN>=3){if((d2>0)!=(e->zD[i][c]>0)&&fabsf(d2)>1e-4f)e->flip++;e->flipN++;}e->zD[i][c]=d2;}e->zig+=sqrt(a/8);e->zigN++;}
     memcpy(e->zA,e->zB,sizeof(o));memcpy(e->zB,o,sizeof(o));e->zN++;}
    if(P0&&pn&&(!e->oT||nowq-e->oT>=frequency/30)){float o[8][3];int st=n/9>0?n/9:1;for(int i=0;i<8;i++){int id=(i+1)*st;if(id>=n)id=n-1;for(int c=0;c<3;c++)o[i][c]=P0[id*4+c]-S0[id*4+c];}
     if(e->oN>=2){double a=0;for(int i=0;i<8;i++)for(int c=0;c<3;c++){double d2=o[i][c]-2*e->oB[i][c]+e->oA[i][c];a+=d2*d2;}e->jit+=sqrt(a/8);e->jitN++;}
     memcpy(e->oA,e->oB,sizeof(o));memcpy(e->oB,o,sizeof(o));e->oN++;e->oT=nowq;}}}}{const float* wv=(const float*)(ctx+0x90);e->wind=sqrtf(wv[0]*wv[0]+wv[1]*wv[1]+wv[2]*wv[2]);e->c160=*(float*)(ctx+0x160);}
  if(cls==0){const float* S=*(const float**)(s+0x9c);const float* P=*(const float**)(s+0xc);const BYTE* pin=*(const BYTE**)(s+0x34);
   if(S&&P&&pin){int k=n/2;if(memcmp(e->s0,S+k*4,12)){memcpy(e->s0,S+k*4,12);e->sChg++;}
    int stride=n>64?n/64:1;double sd=0;int sn=0;for(int i=0;i<n;i+=stride){if(pin[i]&1)continue;float dx=P[i*4]-S[i*4],dy=P[i*4+1]-S[i*4+1],dz=P[i*4+2]-S[i*4+2];float q=sqrtf(dx*dx+dy*dy+dz*dz);e->dist+=q;e->distN++;sd+=q;sn++;}
    if(sn){double mm=sd/sn;e->m1+=mm;e->m2+=mm*mm;e->mN++;}}}
  LeaveCriticalSection(&cdLock);
 }__except(EXCEPTION_EXECUTE_HANDLER){}
}
static void LogClothDump(){
 if(!cdOn||!cdReady)return;CdEnt t[256];EnterCriticalSection(&cdLock);memcpy(t,cdTab,sizeof(t));memset(cdTab,0,sizeof(cdTab));LeaveCriticalSection(&cdLock);
 // biggest instances first, at most 14 lines
 bool used[256]={0};
 for(int r=0;r<14;r++){int b=-1;for(int i=0;i<256;i++)if(t[i].self&&!used[i]&&t[i].steps>5&&(b<0||t[i].n>t[b].n))b=i;if(b<0)break;used[b]=true;CdEnt& e=t[b];
  char m[520];sprintf_s(m,"CLOTHDUMP %s %08lx n=%u steps=%ld (avg dt %.1f ms) g=%.3f w max %.3f avg %.3f nonzero %ld | anim pose changed in %.0f%% of steps | |P-S| avg %.1f cm wobble %.1f cm jitter30 %.2f cm zigzag %.2f mm/step flips %.0f%% | anim burst %.0f%% wind burst %.0f%% | wind %.2f c160 %.2f | set 14:%.2f 18:%.3f 1c:%.3f 2c:%.3f 30:%.3f 34:%.3f 38:%.3f 60:%.3f 64:%.3f 68:%.3f iters %.0f 3e %.0f",
   e.cls?"SOFT":"CLOTH",e.self,e.n,e.steps,e.steps?1000.0*e.dtSum/e.steps:0,e.g,e.wMax,e.wAvg,e.wNz,e.steps?100.0*e.sChg/e.steps:0,e.distN?100.0*e.dist/e.distN:0,e.mN>1?100.0*sqrt(max(0.0,e.m2/e.mN-(e.m1/e.mN)*(e.m1/e.mN))):0.0,e.jitN?100.0*e.jit/e.jitN:0.0,e.zigN?1000.0*e.zig/e.zigN:0.0,e.flipN?100.0*e.flip/e.flipN:0.0,e.sTot>0?100.0*e.sMaxTot/e.sTot:0.0,e.wiTot>0?100.0*e.wiMaxTot/e.wiTot:0.0,e.wind,e.c160,
   e.d[0],e.d[1],e.d[2],e.d[3],e.d[4],e.d[5],e.d[6],e.d[7],e.d[8],e.d[9],e.d[10],e.d[11]);Log(m);}
}
// v60: owner rotation (rows = axes, as the engine's skinning 0x53b2a0 uses them: world = x*r0+y*r1+z*r2),
// ctx+0x10/+0x20/+0x30 = current matrix of the object or bone the cloth hangs on (written every frame by 0x6c4c10)
static bool GwRot(void* self,float* R){__try{DWORD ctx=*(DWORD*)((BYTE*)self+0x54);if(!ctx)return false;
 for(int i=0;i<3;i++){const float* row=(const float*)(ctx+0x10+i*0x10);R[i*4]=row[0];R[i*4+1]=row[1];R[i*4+2]=row[2];
  float l=row[0]*row[0]+row[1]*row[1]+row[2]*row[2];if(!(l>1e-8f))return false;R[i*4+3]=1.0f/l;}return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}

// ---- v64: the Ubisoft way (GDC 2015, A. Vaisse "Ubisoft Cloth Simulation", slides 14-16): "simulation frame
// rate must be fixed (limitation of the algorithm), 30 fps...; the simulation is called several times per frame
// and the SKINNED vertices and collision objects are interpolated". Mode 3 for ClothLODInstance (sails, dresses):
// the game's own solver steps exactly 1/30 s on a frame-time accumulator (physics exactly as the 30 FPS game), the
// animated pose S (sail yards/bones, body) is refreshed EVERY frame (0x53b2a0 bone matrices + 0x5379f0 skinned
// positions), and between steps only the cloth's deviation from that pose is interpolated:
//   shown = S_now + lerp(P_A - S_A, P_B - S_B, t)
// so the cloth never lags the rigging or the body (v60 interpolated the whole cloth and lagged the yards).
static void GwSkinStep(void* self,GwInst* e,float dt,float* P,WORD n,LONGLONG now,float dx,float dy,float dz){
 const float H=1.0f/30.0f;BYTE* sb=(BYTE*)self;
 for(int i=0;i<n;i++){float* b=e->B+i*4;float* a=e->A+i*4;b[0]+=dx;b[1]+=dy;b[2]+=dz;a[0]+=dx;a[1]+=dy;a[2]+=dz;}
 memcpy(P,e->B,(SIZE_T)n*16);
 e->acc+=dt;int steps=0;tlsE30=1.0f;JkForStep(1.0f);
 while(e->acc>=H&&steps<2){memcpy(e->A,P,(SIZE_T)n*16);memcpy(e->SA,e->SB,(SIZE_T)n*16);origClStep[0](self,H,1);steps++;e->acc-=H;
  P=*(float**)(sb+0xc);if(P!=e->P||*(WORD*)(sb+0x12)!=n){e->valid=0;return;}
  memcpy(e->B,P,(SIZE_T)n*16);const float* S=*(const float**)(sb+0x9c);if(S)memcpy(e->SB,S,(SIZE_T)n*16);}
 if(e->acc>=H)e->acc=0;
 if(!steps){   // refresh the animated pose for this frame without simulating
  DWORD ctx=*(DWORD*)(sb+0x54);
  if(ctx){typedef void(__thiscall*Anch)(void*,DWORD,float);typedef void(__thiscall*Skin)(void*);
   ((Anch)0x53b2a0)(self,*(DWORD*)(ctx+0x150),*(float*)(ctx+0xc0));((Skin)0x5379f0)(self);}}
 const float* Sn=*(const float**)(sb+0x9c);if(!Sn){memcpy(e->D,e->B,(SIZE_T)n*16);return;}
 float t=e->acc/H;if(t<0)t=0;if(t>1)t=1;
 for(int i=0;i<n;i++){const float* a=e->A+i*4;const float* b=e->B+i*4;const float* sa=e->SA+i*4;const float* s2=e->SB+i*4;const float* sn=Sn+i*4;float* d=e->D+i*4;
  for(int c=0;c<3;c++){float oa=a[c]-sa[c],ob=b[c]-s2[c];d[c]=sn[c]+oa+(ob-oa)*t;}d[3]=b[3];}
 memcpy(P,e->D,(SIZE_T)n*16);
 InterlockedExchangeAdd(&gwSteps,steps);InterlockedIncrement(&gwFrames);
}
static void GwStepCommon(int k,void* self,float dt,int nsub){
 ClothDumpNote(k,self,dt);
 tlsE30=dt*30.0003f;JkForStep(iterFix?tlsE30:1.0f);
 if(!gwOn||!gwLockReady||nsub!=1||!(dt>0.0f)||dt>0.1f||(gwOn==3&&k!=0)){origClStep[k](self,dt,nsub);return;}
 float* P;WORD n;__try{P=*(float**)((BYTE*)self+0xc);n=*(WORD*)((BYTE*)self+0x12);}__except(EXCEPTION_EXECUTE_HANDLER){origClStep[k](self,dt,nsub);return;}
 if(!P||!n||n>8192){origClStep[k](self,dt,nsub);return;}
 LONGLONG now=QpcNow();const float H=1.0f/30.0f;
 EnterCriticalSection(&gwLock);
 GwInst* e=GwFind((DWORD)self,now);
 LeaveCriticalSection(&gwLock);   // the entry belongs to this instance; only one thread updates an instance
 if(!e){origClStep[k](self,dt,nsub);return;}
 if(gwOn!=3&&now-hkTickQpc>frequency/10){   // no physics ticks (paused/loading): run as the game does
  if(e->valid&&e->P==P&&e->n==n)memcpy(P,e->B,(SIZE_T)n*16);e->valid=0;origClStep[k](self,dt,nsub);return;}
 bool reinit=!e->valid||e->P!=P||e->n!=n||now-e->last>frequency/4;
 float dx=0,dy=0,dz=0;
 if(!reinit){dx=P[0]-e->D[0];dy=P[1]-e->D[1];dz=P[2]-e->D[2];if(dx*dx+dy*dy+dz*dz>4.0f||dx!=dx)reinit=true;}
 e->last=now;
 if(reinit){
  if(e->A&&e->n!=n){HeapFree(GetProcessHeap(),0,e->A);e->A=0;}
  if(!e->A)e->A=(float*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)n*16*5);
  if(!e->A){e->valid=0;origClStep[k](self,dt,nsub);return;}
  e->B=e->A+n*4;e->D=e->B+n*4;e->SA=e->D+n*4;e->SB=e->SA+n*4;e->P=P;e->n=n;e->acc=0;e->tick=hkTick;if(!GwRot(self,e->RB)){static const float I[12]={1,0,0,1,0,1,0,1,0,0,1,1};memcpy(e->RB,I,sizeof(I));}memcpy(e->RA,e->RB,sizeof(e->RA));
  origClStep[k](self,dt,1);
  memcpy(e->A,P,(SIZE_T)n*16);memcpy(e->B,P,(SIZE_T)n*16);memcpy(e->D,P,(SIZE_T)n*16);
  {const float* S=k==0?*(const float**)((BYTE*)self+0x9c):0;if(S){memcpy(e->SA,S,(SIZE_T)n*16);memcpy(e->SB,S,(SIZE_T)n*16);}else{memcpy(e->SA,P,(SIZE_T)n*16);memcpy(e->SB,P,(SIZE_T)n*16);}}
  e->valid=1;
  InterlockedIncrement(&gwInits);return;
 }
 if(gwOn==3){GwSkinStep(self,e,dt,P,n,now,dx,dy,dz);return;}
 // restore the real state of the last step (plus the follow shift applied since the last frame)
 for(int i=0;i<n;i++){float* b=e->B+i*4;float* a=e->A+i*4;b[0]+=dx;b[1]+=dy;b[2]+=dz;a[0]+=dx;a[1]+=dy;a[2]+=dz;}
 memcpy(P,e->B,(SIZE_T)n*16);
 // v53: step exactly once per physics tick (the ship sails' wind and state are computed in that tick, 30 Hz),
 // as at 30 FPS where every frame has one tick and one cloth step
 LONG tick=hkTick;int steps=tick-e->tick;if(steps<0)steps=0;if(steps>2)steps=2;e->tick=tick;
 tlsE30=1.0f;JkForStep(1.0f);
 for(int j=0;j<steps;j++){memcpy(e->A,P,(SIZE_T)n*16);origClStep[k](self,H,1);}
 P=*(float**)((BYTE*)self+0xc);
 if(P!=e->P||*(WORD*)((BYTE*)self+0x12)!=n){e->valid=0;return;}
 float Rn[12];bool rot=GwRot(self,Rn);
 if(steps){memcpy(e->B,P,(SIZE_T)n*16);if(rot){memcpy(e->RA,steps>1?Rn:e->RB,sizeof(e->RA));memcpy(e->RB,Rn,sizeof(e->RB));}e->acc=0;}
 else e->acc+=dt;
 if(gwOn==2){memcpy(e->D,e->B,(SIZE_T)n*16);}   // mode 2: show the latest step (cloth moves at 30 Hz like at 30 FPS)
 else{   // mode 1 (v60): smooth by FRAME time since the step, in the owner's rotating frame (mast/body), so the
         // shown cloth turns with the ship instead of lagging one physics tick behind it
  float t=e->acc/H;if(t<0)t=0;if(t>1)t=1;const float* RA=e->RA;const float* RB=e->RB;
  for(int i=0;i<n;i++){const float* a=e->A+i*4;const float* b=e->B+i*4;float* d=e->D+i*4;
   if(rot){float la[3],lb[3];for(int q=0;q<3;q++){la[q]=(a[0]*RA[q*4]+a[1]*RA[q*4+1]+a[2]*RA[q*4+2])*RA[q*4+3];lb[q]=(b[0]*RB[q*4]+b[1]*RB[q*4+1]+b[2]*RB[q*4+2])*RB[q*4+3];}
    float l0=la[0]+(lb[0]-la[0])*t,l1=la[1]+(lb[1]-la[1])*t,l2=la[2]+(lb[2]-la[2])*t;
    d[0]=l0*Rn[0]+l1*Rn[4]+l2*Rn[8];d[1]=l0*Rn[1]+l1*Rn[5]+l2*Rn[9];d[2]=l0*Rn[2]+l1*Rn[6]+l2*Rn[10];}
   else{d[0]=a[0]+(b[0]-a[0])*t;d[1]=a[1]+(b[1]-a[1])*t;d[2]=a[2]+(b[2]-a[2])*t;}
   d[3]=b[3];}
  memcpy(P,e->D,(SIZE_T)n*16);}
 InterlockedExchangeAdd(&gwSteps,steps);InterlockedIncrement(&gwFrames);
}
static void __fastcall GwStep0(void* self,void*,float dt,int n){GwStepCommon(0,self,dt,n);}
static void __fastcall GwStep1(void* self,void*,float dt,int n){GwStepCommon(1,self,dt,n);}
static bool InstallCloth30(){
 InitializeCriticalSection(&gwLock);gwLockReady=true;InitializeCriticalSection(&cdLock);cdReady=true;cdOn=CfgIntW(L"Debug",L"ClothDump",0,configPath)!=0;if(cdOn)Log("DIAG: cloth instance dump on (ClothDump=1).");
 bool a=SwapSlot(0x1e4e760+0x48,(void*)GwStep0,(void**)&origClStep[0],0x53bb90);
 bool b=SwapSlot(0x1e50ce0+0x48,(void*)GwStep1,(void**)&origClStep[1],0x569f10);
 return a&&b;
}
static void LogCloth30(){LONG f=InterlockedExchange(&gwFrames,0),s=InterlockedExchange(&gwSteps,0),i=InterlockedExchange(&gwInits,0),x=InterlockedExchange(&gwFull,0);if(!f&&!i)return;
 char b[200];sprintf_s(b,"CLOTH30 10s: mode %ld | cloth updates %ld, real 1/30 s steps %ld, (re)starts %ld, table full %ld",gwOn,f,s,i,x);Log(b);}
// ---- v52 measurement (Diagnostics only, no behaviour change) ---------------------------------------
// (1) Which functions of the ship sail system run and how often: every code slot of the vtables of
//     SailManager (0x268a098) and PlayerArticulatedSail (0x2687b6c) gets a counting thunk.
// (2) How often the body capsules a cloth collides with (ClothLODInstance::Collide 0x548d90, slot +0x60,
//     capsules [[inst+0x54]+0x154]) really change: once per frame per instance, the first capsule's ends
//     are compared with the previous frame; only instances whose capsules moved in the last 0.5 s count.
//     ~100% = updated every frame (smooth); ~30/FPS = updated at 30 Hz (steps the cloth must chase).
static const DWORD spVt[2]={0x268a098,0x2687b6c};static const char* spName[2]={"SailManager","PlayerArticulatedSail"};
static volatile LONG spCnt[128];static DWORD spOrig[128],spSlot[128];static int spN=0;
static void InstallSailProbe(){
 BYTE* code=(BYTE*)VirtualAlloc(0,128*16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);if(!code)return;
 for(int v=0;v<2;v++)for(int s=0;s<64;s++){
  DWORD slot=spVt[v]+s*4,fn=*(DWORD*)slot;
  if(fn<0x401000||fn>=0x1e00000)break;          // end of the vtable (next item is data)
  if(fn==0x1426100||fn==0x1243a70||fn==0x6c7780||spN>=128)continue;   // shared empty stubs
  int i=spN;BYTE* t=code+i*16;spOrig[i]=fn;spSlot[i]=v<<16|s*4;
  t[0]=0xF0;t[1]=0xFF;t[2]=0x05;*(DWORD*)(t+3)=(DWORD)&spCnt[i];t[7]=0xFF;t[8]=0x25;*(DWORD*)(t+9)=(DWORD)&spOrig[i];
  DWORD old;if(!VirtualProtect((void*)slot,4,PAGE_READWRITE,&old))continue;*(DWORD*)slot=(DWORD)t;DWORD ig;VirtualProtect((void*)slot,4,old,&ig);spN++;
 }
 FlushInstructionCache(GetCurrentProcess(),code,128*16);
 char b[96];sprintf_s(b,"DIAG: sail system probe on %d vtable slots.",spN);Log(b);
}
static void LogSailProbe(){
 char b[900];int n=sprintf_s(b,"SAILPROBE 10s calls/s:");bool any=false;
 for(int i=0;i<spN;i++){LONG c=InterlockedExchange(&spCnt[i],0);if(!c)continue;any=true;
  if(n<820)n+=sprintf_s(b+n,sizeof(b)-n," %s+0x%x(%06lx)=%.1f",spName[spSlot[i]>>16],spSlot[i]&0xffff,spOrig[i],c/10.0);}
 if(any)Log(b);
}
typedef void(__thiscall*CollFn)(void*);static CollFn origColl;
struct CapEnt{DWORD self;LONG frame;float p[8];LONGLONG moved;};static CapEnt capTab[512];static volatile LONG capSame=0,capMoved=0,capCalls=0;
static void __fastcall CapProbe(void* self,void*){
 InterlockedIncrement(&capCalls);
 __try{
  DWORD owner=*(DWORD*)((BYTE*)self+0x54);DWORD caps=owner?*(DWORD*)(owner+0x154):0;
  if(caps&&*(WORD*)(caps+0x1e)){const float* q=*(const float**)(caps+0x18);
   UINT h=(((DWORD)self>>4)*2654435761u)>>23;CapEnt& e=capTab[h];LONG f=frameIndex;LONGLONG now=QpcNow();
   if(e.self!=(DWORD)self){e.self=(DWORD)self;e.frame=f;memcpy(e.p,q,32);e.moved=0;}
   else if(e.frame!=f){e.frame=f;bool ch=memcmp(e.p,q,32)!=0;
    if(ch){memcpy(e.p,q,32);e.moved=now;InterlockedIncrement(&capMoved);}
    else if(e.moved&&now-e.moved<frequency/2)InterlockedIncrement(&capSame);}
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){}
 origColl(self);
}
static void LogCapProbe(){LONG m=InterlockedExchange(&capMoved,0),s=InterlockedExchange(&capSame,0),c=InterlockedExchange(&capCalls,0);if(!c)return;
 char b[200];sprintf_s(b,"CAPSULES 10s: collide calls %ld | frames with moving capsules: changed %ld, unchanged %ld (%.0f%% of frames carry a new capsule pose)",c,m,s,m+s?100.0*m/(m+s):0.0);Log(b);}
static void InstallCapProbe(){bool ok=SwapSlot(0x1e4e760+0x60,(void*)CapProbe,(void**)&origColl,0x548d90);Log(ok?"DIAG: cloth capsule probe on.":"DIAG: cloth capsule probe - slot not recognised.");}
static bool InstallWindExperiment(){
 bool a=SwapSlot(0x1e4e760+0x54,(void*)PreSub0,(void**)&origPreSub[0],0x573110);
 bool b=SwapSlot(0x1e50ce0+0x54,(void*)PreSub1,(void**)&origPreSub[1],0x573110);
 if(!a&&b){SwapSlot(0x1e50ce0+0x54,(void*)origPreSub[1],(void**)&origPreSub[1],(DWORD)PreSub1);return false;}
 return a&&b;
}

// v45: LOD the way AC4Tweaker 2.3 (Kamzik123) does it - researched from its AC4Tweaker.asi, both sites
// byte-identical in our EXE. (1) Character LOD: the character renderer loads its LOD factor at 0x718ed1
// (movss xmm0,[esi+728h]); its square goes to [esi+0xa0c]. One factor for the whole character, so body,
// head, eyes and mouth stay on matching levels (the v43 distance multiplier split faces). Lower = more
// detail, 0 = always the best model. (2) Graphics settings object: at 0x8a277d (movss xmm1,[eax+170h])
// eax is the active quality settings; fields +0x1a4 LOD scale (lower = more detail; also used by the LOD
// selectors, see 0x869657), +0x184 character cull distance, +0x170/+0x174 dynamic props / their shadows,
// +0x178/+0x17c static props / their shadows. Values < 0 in Settings.ini = game.
static float gfxCharLod=-1,gfxLodScale=-1,gfxCharCull=-1,gfxDyn=-1,gfxDynSh=-1,gfxStat=-1,gfxStatSh=-1;
static DWORD charLodBack=0x718ed9,gfxBack=0x8a2785;static float gfxOrig[7];static volatile LONG gfxOrigOk=0;
static const int gfxOff[6]={0x1a4,0x184,0x170,0x174,0x178,0x17c};
static void __stdcall GfxApply(DWORD o){
 if(!o)return;
 __try{
  if(!gfxOrigOk||gfxObj!=o){for(int i=0;i<6;i++)gfxOrig[i]=*(float*)(o+gfxOff[i]);gfxObj=o;
   if(!InterlockedExchange(&gfxOrigOk,1)){char b[220];sprintf_s(b,"LOD: game settings - LOD scale %.3f, character cull %.1f m, dynamic props %.1f (shadows %.1f), static props %.1f (shadows %.1f)",gfxOrig[0],gfxOrig[1],gfxOrig[2],gfxOrig[3],gfxOrig[4],gfxOrig[5]);Log(b);}}
  const float want[6]={gfxLodScale,gfxCharCull,gfxDyn,gfxDynSh,gfxStat,gfxStatSh};
  for(int i=0;i<6;i++){float v=(gfxOn&&want[i]>=0)?want[i]:gfxOrig[i];*(float*)(o+gfxOff[i])=v;}
 }__except(EXCEPTION_EXECUTE_HANDLER){}
}
__declspec(naked) static void GfxHook(){__asm{
 pushad
 push eax
 call GfxApply
 popad
 movss xmm1,dword ptr [eax+170h]
 jmp dword ptr [gfxBack]
}}
static volatile LONG charLodSeen=0;static float charLodGame=0;
static void __stdcall CharLodNote(float g){if(!InterlockedExchange(&charLodSeen,1)){charLodGame=g;char b[120];sprintf_s(b,"LOD: game character LOD factor %.3f, patch %.3f",g,gfxCharLod);Log(b);}}
__declspec(naked) static void CharLodHook(){__asm{
 movss xmm0,dword ptr [esi+728h]
 cmp charLodSeen,0
 jne cl_go
 pushad
 sub esp,4
 movss dword ptr [esp],xmm0
 call CharLodNote
 popad
 movss xmm0,dword ptr [esi+728h]
cl_go:
 cmp gfxOn,0
 je cl_back
 movss xmm0,dword ptr [gfxCharLod]
cl_back:
 jmp dword ptr [charLodBack]
}}
static float GfxIni(const wchar_t* key,float def){wchar_t v[32];v[0]=0;if(userPath[0])GetPrivateProfileStringW(L"Graphics",key,L"",v,32,userPath);if(!v[0])GetPrivateProfileStringW(L"Graphics",key,L"",v,32,configPath);if(!v[0])return def;return (float)_wtof(v);}
static void InstallGfxLod(){
 gfxCharLod=GfxIni(L"CharacterLOD",0.0f);gfxLodScale=GfxIni(L"LODScale",0.5f);gfxCharCull=GfxIni(L"CharacterCullDistance",-1);
 gfxDyn=GfxIni(L"DynamicPropsDistance",-1);gfxDynSh=GfxIni(L"DynamicPropShadowsDistance",-1);gfxStat=GfxIni(L"StaticPropsDistance",-1);gfxStatSh=GfxIni(L"StaticPropShadowsDistance",-1);
 if(gfxLodScale>=1.0f&&gfxLodScale<1.0001f&&CfgIntW(L"Graphics",L"LODScaleIsMultiplier",0,configPath)==0)gfxLodScale=-1;   // old v40-v44 key "LODScale=1" meant "game"
 bool c=false,g=false;
 if(gfxCharLod>=0){static const BYTE b1[]={0xf3,0x0f,0x10,0x86,0x28,0x07,0x00,0x00};c=Jump(0x718ed1,b1,8,CharLodHook);}
 {static const BYTE b2[]={0xf3,0x0f,0x10,0x88,0x70,0x01,0x00,0x00};g=Jump(0x8a277d,b2,8,GfxHook);}
 char b[260];sprintf_s(b,"LOD: character LOD %s%.2f%s, settings override %s (LOD scale %.2f, char cull %.0f, props %.0f/%.0f, shadows %.0f/%.0f; <0 = game); F5 toggles with Diagnostics.",
  c?"":"(off) ",gfxCharLod,"",g?"on":"off",gfxLodScale,gfxCharCull,gfxDyn,gfxStat,gfxDynSh,gfxStatSh);Log(b);
}

// v47: streaming range (as AC4Tweaker "LoadingRange"). The pop-in "chessboard" is the LOD blend, and the
// blend also follows how far a detailed mesh has been STREAMED in (0x86b819: per-object byte table at
// [ctx+0x378]) - a passer-by whose mesh is still loading appears from the low model and dithers in. At
// 0x5d5e43 the loading grid takes its range from [esi+0x70] (capped by [esi+0x6c] / a table): close grids
// have 0x2e or 0xb6, the rest are far. Range = 32 x multiplier (Tweaker); the cap is lifted. More range
// = more loaded objects, so the engine's limit 0x4000 (0x92eb7b / 0x92ec23) is raised to 0xfffe.
static int lrClose=-1,lrFar=-1;static DWORD lrBack=0x5d5e49;static volatile LONG lrSeen[8],lrSeenN=0;
static void __stdcall LrNote(DWORD v){LONG n=lrSeenN;for(int i=0;i<n&&i<8;i++)if((DWORD)lrSeen[i]==v)return;if(n<8){lrSeen[n]=v;InterlockedIncrement(&lrSeenN);char b[96];sprintf_s(b,"Streaming: loading grid range %lu (%s)",v,(v==0x2e||v==0xb6)?"close":"far");Log(b);}}
__declspec(naked) static void LrHook(){__asm{
 mov eax,dword ptr [esi+6ch]
 mov edx,dword ptr [esi+70h]
 pushad
 push edx
 call LrNote
 popad
 cmp edx,2eh
 je lr_close
 cmp edx,0b6h
 je lr_close
 cmp lrFar,0
 jle lr_back
 mov edx,lrFar
 shl edx,5
 mov eax,1
 jmp lr_back
lr_close:
 cmp lrClose,0
 jle lr_back
 mov edx,lrClose
 shl edx,5
 mov eax,1
lr_back:
 jmp dword ptr [lrBack]
}}
static void InstallLoadingRange(){
 lrClose=CfgIntW(L"Graphics",L"CloseLoadingRange",3,configPath);lrFar=CfgIntW(L"Graphics",L"FarLoadingRange",-1,configPath);
 if(lrClose>8)lrClose=8;if(lrFar>40)lrFar=40;
 if(lrClose<=0&&lrFar<=0)return;
 static const BYTE h[]={0x8b,0x46,0x6c,0x8b,0x56,0x70};static const BYTE l1[]={0xbe,0xff,0x3f,0x00,0x00},l1n[]={0xbe,0xfd,0xff,0x00,0x00};
 static const BYTE l2[]={0x81,0xff,0x00,0x40,0x00,0x00},l2n[]={0x81,0xff,0xfe,0xff,0x00,0x00};
 bool lim=Queue(0x92eb7b,l1,l1n,5)&&Queue(0x92ec23,l2,l2n,6);
 bool ok=Jump(0x5d5e43,h,6,LrHook);
 char b[200];sprintf_s(b,"Streaming: loading range close x%d, far %s%d (32 per step), object limit %s.",lrClose,lrFar>0?"x":"game ",lrFar,lim?"raised 0x4000 -> 0xfffe":"unchanged");Log(ok?b:"Streaming: loading grid code not recognised; unchanged.");
}

// v48 experiment: LOD crossfade ("chessboard"). In the main LOD path (0x86b037) the selected level is drawn
// together with the next one while a blend factor 0..1 is between them (0x86b94a: factor x 255 -> alpha of
// the second model, drawn dithered). The factor comes from the distance band of the LOD descriptor
// ([desc+0x74/0x78], 0x86b7af) or - if the detailed mesh is not streamed in yet - from the streaming
// progress (0x86b8bc branch). F12 (with Diagnostics) / [Graphics] LODBlend: 0 game, 1 no crossfade (hard
// switch, no dither). Counters every 10 s: selections, with a crossfade, of them by streaming.
static volatile LONG lbAll=0,lbPartial=0,lbStream=0,lbPartialScaled=0;/* v50: crossfades of objects that follow LODScale (selector flag 4) */static DWORD lbBack=0x86b952,lsBack=0x86b8c3;static const float lbZero=0,lbOne=1;
__declspec(naked) static void LodBlendHook(){__asm{
 lock inc lbAll
 comiss xmm0,dword ptr [lbZero]
 jbe lb_full
 comiss xmm0,dword ptr [lbOne]
 jae lb_full
 lock inc lbPartial
 push eax
 mov eax,dword ptr [esi+10h]
 test byte ptr [eax+68h],4
 pop eax
 jz lb_ns
 lock inc lbPartialScaled
lb_ns:
 cmp lbMode,0
 je lb_full
 xorps xmm0,xmm0
lb_full:
 mulss xmm0,dword ptr ds:[1e3fbd4h]
 jmp dword ptr [lbBack]
}}
__declspec(naked) static void LodStreamHook(){__asm{
 lock inc lbStream
 xor edx,edx
 div ecx
 mov ecx,dword ptr [esi+10h]
 jmp dword ptr [lsBack]
}}
static void InstallLodBlend(){
 lbMode=CfgIntW(L"Graphics",L"LODBlend",0,configPath)==1;
 static const BYTE a[]={0xf3,0x0f,0x59,0x05,0xd4,0xfb,0xe3,0x01},b[]={0x33,0xd2,0xf7,0xf1,0x8b,0x4e,0x10};
 bool x=Jump(0x86b94a,a,8,LodBlendHook),y=Jump(0x86b8bc,b,7,LodStreamHook);
 char m[120];sprintf_s(m,"LOD blend probe %s/%s, mode %ld (0 game crossfade, 1 hard switch); F12 toggles with Diagnostics.",x?"on":"off",y?"on":"off",lbMode);Log(m);
}
static void LogCorpseFilter(){LONG h=InterlockedExchange(&ccHits,0),bn=InterlockedExchange(&ccBorn,0);if(!h&&!bn)return;
 char b[200];sprintf_s(b,"Corpses 10s: contacts between bodies of different dead NPCs allowed %ld, pairs born inside each other kept apart %ld",h,bn);Log(b);}
static void LogLodBlend(){LONG a=InterlockedExchange(&lbAll,0),p=InterlockedExchange(&lbPartial,0),st=InterlockedExchange(&lbStream,0),ps=InterlockedExchange(&lbPartialScaled,0);if(!a)return;
 char b[240];sprintf_s(b,"LOD BLEND 10s (mode %ld): selections %ld, in crossfade %ld (%.1f%%; of them follow LODScale %ld), limited by streaming %ld",lbMode,a,p,100.0*p/a,ps,st);Log(b);}
// v31: APEX hang guard. APEX_Particles_x86.dll+0x31540 (destructor of an object with a child list)
// releases its children in a loop "while (count) children[count-1]->vfunc+0x28()" and relies on every
// child removing itself. Near campfires a child pointer was stale (its memory reused by another object:
// the call landed in a CUDA reduce-kernel launcher +0x14c860), the child never left the list and the main
// thread spun there forever (2026-10-12 20:19, LISTPROF 1712+450 of 2166 samples). The loop is replaced:
// a child that does not leave the list is dropped from it, the destructor finishes.
static volatile LONG apexDrop=0;static DWORD apexLoopBack=0;
__declspec(naked) static void ApexChildLoop(){__asm{
al_loop:
 mov eax,[esi+8]
 push eax
 mov ecx,[esi+4]
 mov ecx,[ecx+eax*4-4]
 mov edx,[ecx]
 call dword ptr [edx+28h]
 pop eax
 cmp [esi+8],eax
 jb al_ok
 dec dword ptr [esi+8]
 lock inc apexDrop
al_ok:
 cmp dword ptr [esi+8],0
 jne al_loop
 jmp dword ptr [apexLoopBack]
}}
static bool PatchApexChildLoop(HMODULE m){
 BYTE* at=(BYTE*)m+0x31560;
 static const BYTE in[]={0x8b,0x46,0x08,0x8b,0x4e,0x04,0x8b,0x4c,0x81,0xfc,0x8b,0x11,0x8b,0x42,0x28,0xff,0xd0,0x83,0x7e,0x08,0x00,0x75,0xe9};
 if(memcmp(at,in,sizeof(in)))return false;apexLoopBack=(DWORD)m+0x31577;
 BYTE code[6];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)ApexChildLoop-((DWORD)at+5);code[5]=0x90;
 DWORD old;if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&old))return false;memcpy(at,code,6);FlushInstructionCache(GetCurrentProcess(),at,6);DWORD ig;VirtualProtect(at,6,old,&ig);return true;
}
// v31: hang watchdog ([Debug] HangKill seconds, 0 = off). If the game, while it is the foreground
// window, shows no new frame for that long (after the first 600 frames), the reason is logged and the
// game is closed - a hung full-screen game otherwise leaves only a reboot.
static int hangKill=20;
static DWORD WINAPI Watchdog(void*){
 bool apexDone=false;LONG lastF=-1,lastDrop=0;int still=0;
 for(;;){Sleep(1000);
  if(!apexDone){HMODULE m=GetModuleHandleA("APEX_Particles_x86.dll");if(m){apexDone=true;Log(PatchApexChildLoop(m)?"PhysX: APEX child-release loop guarded (no endless loop on a stale particle child).":"PhysX: APEX child-release loop not recognised; unchanged.");}}
  LONG d=apexDrop;if(d!=lastDrop){char b[160];sprintf_s(b,"PhysX: APEX child that did not leave its parent's list dropped (%ld so far) - the game would have hung here",d);Log(b);lastDrop=d;}
  if(hangKill<=0)continue;
  LONG f=wdFrames;HWND w=GetForegroundWindow();DWORD pid=0;if(w)GetWindowThreadProcessId(w,&pid);
  if(f!=lastF||f<600||pid!=gamePID){still=0;lastF=f;continue;}
  if(++still<hangKill)continue;
  char b[300];int k=sprintf_s(b,"HANG: no new frame for %d s while the game is in front - closing the game.",still);
  if(mainThreadId){HANDLE th=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,mainThreadId);
   if(th){SuspendThread(th);CONTEXT c;c.ContextFlags=CONTEXT_CONTROL;if(GetThreadContext(th,&c)){char wh[64];Where(c.Eip,wh,64);sprintf_s(b+k,sizeof(b)-k," Main thread at %s.",wh);}CloseHandle(th);}}
  Log(b);Sleep(200);TerminateProcess(GetCurrentProcess(),0xdead);
 }
}

#include "camera_test.h"

static void Initialize(HMODULE self){
 wchar_t exe[MAX_PATH];GetModuleFileNameW(0,exe,MAX_PATH);wchar_t* leaf=wcsrchr(exe,L'\\');
 if(!leaf || _wcsicmp(leaf+1,L"AC4BFSP.exe"))return;
 gamePID=GetCurrentProcessId();
 GetModuleFileNameW(self,configPath,MAX_PATH);wchar_t* slash=wcsrchr(configPath,L'\\');if(!slash)return;slash[1]=0;
 wcscpy_s(logPath,configPath);wcscat_s(configPath,L"AC4Fix\\Settings.ini");WideCharToMultiByte(CP_ACP,0,configPath,-1,configPathA,MAX_PATH,0,0);wcscat_s(logPath,L"AC4Fix\\runtime.log");
 {   // 0.3: a log past 8 MB is moved to runtime.prev.log at start (one previous session kept; it grew to 95 MB)
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if(GetFileAttributesExW(logPath,GetFileExInfoStandard,&fa)&&(fa.nFileSizeHigh||fa.nFileSizeLow>(8u<<20))){
   wchar_t prev[MAX_PATH];wcscpy_s(prev,logPath);wchar_t* dot=wcsrchr(prev,L'.');if(dot){wcscpy_s(dot,MAX_PATH-(dot-prev),L".prev.log");MoveFileExW(logPath,prev,MOVEFILE_REPLACE_EXISTING);}}
 }
 {wcscpy_s(userPath,configPath);wchar_t* us=wcsrchr(userPath,L'\\');if(us)wcscpy_s(us+1,MAX_PATH-(us+1-userPath),L"User.ini");if(GetFileAttributesW(userPath)==INVALID_FILE_ATTRIBUTES)userPath[0]=0;}
 BYTE* image=(BYTE*)GetModuleHandleW(0);if((DWORD)image!=0x400000){Log("Unsupported image base; no changes.");return;}
 IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
 const Sites* s=0;
 for(unsigned i=0;i<sizeof(versions)/sizeof(versions[0]);i++)if(nt->FileHeader.TimeDateStamp==versions[i].stamp && nt->OptionalHeader.SizeOfImage==versions[i].imageSize)s=&versions[i];
 if(!s){Log("Unsupported EXE; no changes.");return;}
 bool cpu=CfgIntW(L"Modules",L"CPUFix",1,configPath)!=0;
 bool fps=CfgIntW(L"Modules",L"FPSAndElevator",1,configPath)!=0;
 bool modes=CfgIntW(L"FPS",L"AllDisplayModes",1,configPath)!=0;
 bool wait=CfgIntW(L"FPS",L"PrecisionWait",1,configPath)!=0;
 configuredFPS=CfgIntW(L"FPS",L"Limit",0,configPath);
 maxModes=CfgIntW(L"FPS",L"MaxModes",24,configPath);if(maxModes<1)maxModes=1;if(maxModes>1000)maxModes=1000;
 minRefresh=CfgIntW(L"FPS",L"MinRefresh",60,configPath);
 autoMargin=CfgIntW(L"FPS",L"AutoMargin",3,configPath);if(autoMargin<0||autoMargin>20)autoMargin=3;
 minWidth=CfgIntW(L"FPS",L"MinWidth",1920,configPath);
 minHeight=CfgIntW(L"FPS",L"MinHeight",1080,configPath);
 if(configuredFPS && (configuredFPS<30 || configuredFPS>1000)){Log("Invalid FPS limit; no changes.");return;}
 activeFPS=configuredFPS?configuredFPS:60;LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=f.QuadPart;ticks=frequency/activeFPS;
 cpuLoop=s->cpuLoop;cpuExit=s->cpuExit;cpuFail=s->cpuFail;timerExit=s->timerExit;waitExit=s->waitExit;modeExit=s->modeExit;modeSkip=s->modeSkip;
 bool ok=true;
 if(cpu)ok=Jump(s->cpu,s->cpuBytes,8,CpuHook)&&ok;
 if(fps){
  ok=Jump(s->timer,s->timerBytes,13,TimerHook)&&ok;
  if(wait)ok=Jump(s->wait,s->waitBytes,8,WaitHook)&&ok;
  const BYTE lift1[]={0x57,0xe9,0x29,0,0,0,0x90,0x90};const BYTE nop[]={0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
  ok=Queue(s->lift,s->liftBytes,lift1,8)&&ok;
  ok=Queue(s->lift+0x41,s->lift2,nop,4)&&ok;
  ok=Queue(s->lift+0x49,s->lift3,nop,8)&&ok;
  if(modes)ok=Jump(s->modes,s->modesBytes,10,ModesHook)&&ok;
 }
 int havokRate=CfgIntW(L"Physics",L"HavokRate",30,configPath);   // v36: default 30 (60 tested worse)
 if(havokRate!=30 && havokRate>=30 && havokRate<=120)ok=QueueHavokRate(havokRate)&&ok;
 busySkip=CfgIntW(L"PhysX",L"BusySkip",1,configPath)!=0;
 smokeReuse=CfgIntW(L"PhysX",L"SmokeReuse",0,configPath)!=0;
 spriteFirstFix=CfgIntW(L"PhysX",L"SpriteFirstFix",1,configPath)!=0;
 wantMeshCache=CfgIntW(L"PhysX",L"MeshCache",1,configPath)!=0;
 if(wantMeshCache){HANDLE th=CreateThread(0,0,CookWatcher,0,0,0);if(th)CloseHandle(th);}
 fireDensity=CfgIntW(L"PhysX",L"FireDensity",100,configPath);if(fireDensity<50||fireDensity>300)fireDensity=100;
 fireParticles=CfgIntW(L"PhysX",L"FireParticles",100000,configPath);if(fireParticles<100000||fireParticles>250000)fireParticles=100000;
 if(s->stamp!=0x64a813d4)fireParticles=100000; // the bigger pool needs the Steam-build buffer move
 redirects[0].wanted=fireDensity!=100;redirects[1].wanted=fireParticles!=100000;
 {
  int full=CfgIntW(L"PhysX",L"SmokeFullDistance",6,configPath),fade=CfgIntW(L"PhysX",L"SmokeFadeDistance",15,configPath),start=CfgIntW(L"PhysX",L"SmokeStartDistance",20,configPath);
  int stop=CfgIntW(L"PhysX",L"SmokeStopDistance",start+5,configPath);if(stop<start)stop=start;if(stop>start+50)stop=start+50;
  bool changed=!(full==6&&fade==15&&start==20&&stop==20);
  if(changed&&s->stamp==0x64a813d4){
   if(full>=1&&full<fade&&fade<=start&&start<=200){
    smokeFadeNear=(float)full;smokeFadeFar=(float)fade;smokeActivate=(float)start;smokeKeepFire=(float)stop;smokeKeepOther=(float)(stop>20?stop:20);smokeKeepShots=(float)(start+5>30?start+5:30);
    if(!QueueSmokeDistances())Log("PhysX: smoke distance code not recognised; distances unchanged.");
   }else Log("PhysX: invalid smoke distances in Settings.ini; unchanged.");
  }
 }
 bool diag=CfgIntW(L"Debug",L"Diagnostics",0,configPath)!=0;
 autoTest=CfgIntW(L"Debug",L"AutoTest",0,configPath);autoTestSec=CfgIntW(L"Debug",L"AutoTestSec",20,configPath);if(autoTestSec<5||autoTestSec>300)autoTestSec=20;if(autoTest)Log("AUTOTEST: plan on - phases switch every AutoTestSec s (default 20); the frame limit alternates full FPS (high beep) / 30 FPS (two low beeps).");
 if(s->stamp==0x64a813d4&&CfgIntW(L"Physics",L"ClothFollowFix",1,configPath)){
  // NPC cloth and sails: see SetFollowFix. comiss xmm2,[1e-5]; jbe +4A -> jmp +4A
  static const BYTE ffOld[]={0x0f,0x2f,0x15,0x60,0xf9,0xe3,0x01,0x76,0x4a},ffNew[]={0x0f,0x2f,0x15,0x60,0xf9,0xe3,0x01,0xeb,0x4a};
  if(Queue(0x6c5669,ffOld,ffNew,9)){clothSmooth=1;Log("Cloth: NPC cloth/sail follow step uses the real frame movement (FPS-independent).");}
  else Log("Cloth: follow-step code not recognised; unchanged.");
  dampFix=CfgIntW(L"Physics",L"ClothDampFix",0,configPath)!=0;iterFix=CfgIntW(L"Physics",L"ClothIterFix",0,configPath)!=0;if(!InstallJk())Log("Cloth: projection softness hook not recognised.");
  if(InstallXpbd()){xpOn=CfgIntW(L"Physics",L"ClothSolver",1,configPath)!=0;
   {wchar_t v[32];GetPrivateProfileStringW(L"Physics",L"ClothRippleDamping",L"20",v,32,configPath);if(userPath[0]){wchar_t u[32];GetPrivateProfileStringW(L"Physics",L"ClothRippleDamping",L"",u,32,userPath);if(u[0])wcscpy_s(v,u);}float k=(float)_wtof(v);if(k>=0&&k<1000)xpK=k;}
   if(xpOn){dampFix=1;skinFix=1;}{char b[160];sprintf_s(b,"Cloth: own frame-rate independent thread solver (XPBD) %s - softness %.2f, thread damping %.1f, ripple damping %.0f /s.",xpOn?"on":"off",xpC,xpD,xpK);Log(b);}
   if(0)Log(xpOn?"":"Cloth: own thread solver installed, off (ClothSolver=0).");}else Log("Cloth: thread solver slot not recognised; unchanged.");
  if(InstallIterFix())Log(iterFix?"Cloth: thread projections of cloth, sails and flags per second as at 30 FPS.":"Cloth: iteration fix installed, off (ClothIterFix=0).");else{iterFix=0;Log("Cloth: solver iteration code not recognised; unchanged.");}
  skinFix=CfgIntW(L"Physics",L"ClothSkinFix",0,configPath)!=0;if(xpOn)skinFix=1;   // the own solver works with the per-dt pull and damping
  xpSoftOn=xpSoftOn&&CfgIntW(L"Physics",L"FlagSolver",1,configPath)!=0;
  {wchar_t v[32];GetPrivateProfileStringW(L"Physics",L"FlagRippleDamping",L"5",v,32,configPath);if(userPath[0]){wchar_t u[32];GetPrivateProfileStringW(L"Physics",L"FlagRippleDamping",L"",u,32,userPath);if(u[0])wcscpy_s(v,u);}float k=(float)_wtof(v);if(k>=0&&k<1000)xpKSoft=k;}
  {char b[120];sprintf_s(b,"Cloth: flags on the own solver %s, ripple damping %.0f /s.",xpSoftOn?"on":"off",xpKSoft);Log(b);}
if(InstallSkinFix())Log(skinFix?"Cloth: pull of cloth/sails toward their animation converted per frame time as at 30 FPS.":"Cloth: pull-to-animation fix installed, off (ClothSkinFix=0).");else{skinFix=0;Log("Cloth: pull-to-animation code not recognised; unchanged.");}
  if(InstallDampFix())Log(dampFix?"Cloth: damping of cloth, sails and flags converted per frame time as at 30 FPS (F7 toggles with Diagnostics).":"Cloth: damping fix installed, off (ClothDampFix=0).");else{dampFix=0;Log("Cloth: damping code not recognised; unchanged.");}
 }
 if(s->stamp==0x64a813d4){InstallGfxLod();InstallLoadingRange();InstallLodBlend();}
 if(s->stamp==0x64a813d4){int wm=CfgIntW(L"Physics",L"ClothWindMode",0,configPath);
  if(InstallWindExperiment()){if(wm<0||wm>3)wm=0;windHookOk=true;if(wm)SetWindMode(wm);char b[160];sprintf_s(b,"Cloth: wind mode %d (0 game, 1 gusts at most 30/s, 2 at most 60/s, 3 off); (set in Settings.ini).",wm);Log(b);}
  gwOn=CfgIntW(L"Physics",L"Cloth30",0,configPath);if(gwOn<0||gwOn>3)gwOn=0;bool g=InstallCloth30();Log(g?(gwOn?"Cloth: 30 FPS gateway on - cloth, sails and flags step exactly 1/30 s, shown interpolated (F11 toggles with Diagnostics).":"Cloth: 30 FPS gateway installed, off (Cloth30=0)."):"Cloth: 30 FPS gateway - code not recognised, unchanged.");if(!g)gwOn=0;}
 if(s->stamp==0x64a813d4&&CfgIntW(L"Physics",L"ClothAnchorSmooth",0,configPath))
  Log(InstallClothAnchorSmooth()?"Cloth: anchor smoothing on (sails, dresses, flags follow their bones without jumps).":"Cloth: anchor code not recognised; smoothing off.");
 if(s->stamp==0x64a813d4&&CfgIntW(L"Physics",L"RagdollFollowFix",1,configPath)){
  // Ragdolls / driven bodies (0x5fdec0): target motion is measured from the last Havok step and
  // divided by the real time since then (see FollowTarget). Lab v14: max 8-16 m/s at 141 FPS vs 83-113.
  Log(QueueRagdollFollowFix(diag)?"Ragdolls: driven body velocities follow the physics step (FPS-independent).":"Ragdolls: follow code not recognised; unchanged.");
 }
 if(s->stamp==0x64a813d4&&CfgIntW(L"Physics",L"CorpsePhysics",1,configPath)){
  // v28 (lab v25-v27): corpses of different NPCs collide (CorpseCollide) and a lying corpse stays a
  // ragdoll instead of being frozen into animation (KeepPhysics: dead NPC's 1.0 s fade-out and the
  // immediate physics-off of 0x1b42b50 are skipped). Without this the game freezes every corpse ~1 s
  // after it lands and corpses dropped on each other sink into one another.
  if(!keptLockReady){InitializeCriticalSection(&keptLock);keptLockReady=true;}
  corpseLimit=CfgIntW(L"Physics",L"CorpseLimit",20,configPath);if(corpseLimit<0)corpseLimit=0;if(corpseLimit>60)corpseLimit=60;
  bool cc=InstallCorpseCollide(),fp=InstallFadeProbe(),io=InstallImmOffProbe();
  if(CfgIntW(L"Physics",L"CorpseCCD",1,configPath)){
   static const BYTE mtIn[]={0x55,0x8b,0xec,0x83,0xec,0x0c};BYTE* t=(BYTE*)VirtualAlloc(0,256,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
   if(t&&!memcmp((void*)0xc4e4c0,mtIn,6)){int k=0;t[k++]=0x60;
    t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;t[k++]=0xff;t[k++]=0x74;t[k++]=0x24;t[k++]=0x24;t[k++]=0x51;t[k++]=0x55;
    t[k++]=0xe8;*(DWORD*)(t+k)=(DWORD)MotionNote-(DWORD)(t+k+4);k+=4;t[k++]=0x61;
    memcpy(t+k,mtIn,6);k+=6;t[k++]=0xe9;*(DWORD*)(t+k)=0xc4e4c6-(DWORD)(t+k+4);k+=4;
    if(HookEntry(0xc4e4c0,mtIn,t))corpseQuality=4;}
   Log(corpseQuality>=0?"Corpses: continuous collision for ragdoll parts on (quality MOVING).":"Corpses: continuous collision - setMotionType not recognised; off.");}
  if(cc)InterlockedExchange(&corpseCollide,1);if(fp&&io)InterlockedExchange(&keepPhys,1);
  char b[200];sprintf_s(b,"Corpses: collide with each other %s, keep ragdoll physics %s, at most %d at once.",cc?"on":"off (code not recognised)",fp&&io?"on":"off (code not recognised)",corpseLimit);Log(b);
 }
 if(s->stamp==0x64a813d4&&CfgIntW(L"Physics",L"NanGuard",1,configPath)){
  // v32: a body given a NaN/inf velocity, spin or impulse by game code gets zero instead (lab: one item
  // with an infinite velocity flew "into the stratosphere"). hkpMotion setters 0xc41050/0xc41070/0xc41280.
  nanTramp=(BYTE*)VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);int ok=0;
  if(nanTramp){static const BYTE setLin[]={0x55,0x8b,0xec,0x8b,0x45,0x08},addImp[]={0x55,0x8b,0xec,0x0f,0x28,0x81,0xc0,0x00,0x00,0x00};
   ok=RgProbe(0xc41050,6,setLin,0,nanTramp)+RgProbe(0xc41070,6,setLin,1,nanTramp+64)+RgProbe(0xc41280,10,addImp,2,nanTramp+128);}
  if(ok==3)InterlockedExchange(&nanGuard,1);char b[96];sprintf_s(b,"Physics: NaN guard %s (%d of 3 setters).",ok==3?"on":"off",ok);Log(b);
 }
 if(s->stamp==0x64a813d4){
  InitializeCriticalSection(&apexLock);apexLockReady=true;
  static const BYTE turbBytes[]={0x8d,0x95,0x04,0xff,0xff,0xff,0x52,0xff,0xd0};
  static const BYTE listBytes[]={0x8b,0x11,0x8b,0x42,0x54,0x53,0xff,0xd0};
  if(Jump(0x855620,turbBytes,9,TurbHook))Log(busySkip?"PhysX: turbulence grid waits removed (BusySkip on, F9 toggles).":"PhysX: turbulence grid hook active (BusySkip off, F9 toggles).");
  else Log("PhysX: turbulence grid code not recognised; unchanged.");
  if(diag&&!Jump(0x857615,listBytes,8,ListHook))Log("PhysX: shape build code not recognised; not timed.");
  {static const BYTE fxBytes[]={0x8b,0x89,0x00,0x07,0x00,0x00};if(diag&&!Jump(0x850d94,fxBytes,6,FxCreateHook))Log("PhysX: effect start code not recognised; not logged.");}
  {static const BYTE fxAfter[]={0x89,0x46,0x04,0x3b,0xc3};if(diag&&!Jump(0x850da7,fxAfter,5,FxAfterHook))Log("PhysX: effect result code not recognised; not logged.");}
  if(diag&&CfgIntW(L"PhysX",L"ParticleMetrics",0,configPath)){
   static const BYTE mapBytes[]={0x8b,0x41,0x38,0xff,0xd0};
   static const BYTE copyBytes[]={0xe8,0x2b,0x3d,0x6b,0x00};
   BYTE mapCall[5]={0xe8},copyCall[5]={0xe8};
   *(DWORD*)(mapCall+1)=(DWORD)ParticleMapMeasured-0x8642bb-5;
   *(DWORD*)(copyCall+1)=(DWORD)ParticleCopyMeasured-0x8642d0-5;
   unsigned saved=count;
   if(!memcmp((void*)0x864180,particlePackSignature,sizeof(particlePackSignature))&&Queue(0x8642bb,mapBytes,mapCall,5)&&Queue(0x8642d0,copyBytes,copyCall,5)){
    particleMetricsOn=true;Log("PARTICLE METRICS2: pack/map/copy counters enabled; original rendering and synchronization calls preserved.");
   }else {count=saved;Log("PARTICLE METRICS2: signatures or queue capacity rejected; no measurement hooks applied.");}
  }
  bool fastFill=CfgIntW(L"PhysX",L"FastParticleFill",1,configPath)!=0;
  if(fireParticles>100000){
   static const BYTE pushOld[]={0x80,0xed,0xdc,0x02};static const BYTE movOld[]={0x9c,0xed,0xdc,0x02};
   SIZE_T bytes=(SIZE_T)fireParticles*256+65536;BYTE* buf=(BYTE*)VirtualAlloc(0,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
   bool okb=buf!=0;
   if(okb){stagingBase=(DWORD)buf;stagingFirst=stagingBase+0x1c;okb=Queue(0x8642cb,pushOld,(const BYTE*)&stagingBase,4);
    if(okb&&!fastFill)okb=Queue(0x86419b,movOld,(const BYTE*)&stagingFirst,4);}
   if(!okb){if(buf)VirtualFree(buf,0,MEM_RELEASE);stagingBase=0x2dced80;stagingFirst=0x2dced9c;fireParticles=100000;redirects[1].wanted=false;Log("PhysX: could not enlarge the particle buffer; campfire pool stays at 100000.");}
  }
  if(fastFill){
   static const BYTE fillHead[]={0xbf,0x9c,0xed,0xdc,0x02,0x90};
   static const BYTE fillLoop[]={0x6a,0x40,0x8d,0x47,0xe4,0x6a,0x00,0x50,0xe8,0x53,0x33,0x6b,0x00,0x83,0xc4,0x0c};
   static const BYTE skip[]={0xeb,0x0e};
   bool packed=CfgIntW(L"PhysX",L"ParticlePackV1",0,configPath)!=0;
   if(packed&&memcmp((void*)0x864180,particlePackSignature,sizeof(particlePackSignature))){packed=false;Log("PhysX PACK1: full loop signature mismatch; keeping existing FastParticleFill path.");}
   particlePackCapacity=(unsigned)fireParticles;
   unsigned savedCount=count;
   if(!memcmp((void*)0x8641a0,fillLoop,sizeof(fillLoop))&&Jump(0x86419a,fillHead,6,packed?(void*)ParticlePackHook:(void*)ParticleFillHook)&&Queue(0x8641a0,fillLoop,skip,2))Log(packed?"PhysX PACK1: enabled; one record per particle, identical four vertices. Baseline v69-clean.":"PhysX PACK1: off; existing FastParticleFill path.");
   else {count=savedCount;Log("PhysX: smoke particle upload code not recognised; queued fill hooks rolled back.");}
  }
 }
 if(s->stamp==0x64a813d4)CameraTest_Queue();
 if(!ok){Log("Byte validation failed; no changes applied.");return;}
 if(cpu && CfgIntW(L"CPU",L"PerformanceCoresOnly",1,configPath))ApplyCoreLayout();
 if(CfgIntW(L"Debug",L"Diagnostics",0,configPath))StartDiagnostics(s);
 if(CfgIntW(L"Modules",L"SkipIntro",0,configPath)){skipIntro=true;WatchDatabases();}   // 0.3: intros shown by default
 hangKill=CfgIntW(L"Debug",L"HangKill",15,configPath);   /* 0.3: 15 s */if(hangKill>0&&hangKill<10)hangKill=10;
 {HANDLE th=CreateThread(0,0,Watchdog,0,0,0);if(th)CloseHandle(th);}
 int simRate=CfgIntW(L"PhysX",L"SimRate",60,configPath);
 if(simRate>=20 && simRate<=1000){physxMinStep=1.0f/simRate;wantSimRate=true;HANDLE th=CreateThread(0,0,ApexWatcher,0,0,0);if(th)CloseHandle(th);}
 if(wantSimRate||wantMeshCache||redirects[0].wanted||redirects[1].wanted){if(!HookImport("KERNEL32.dll","LoadLibraryA",(void*)HookLoadA,(void**)&realLoadA))Log("PhysX: could not watch the particle module load; using fallback.");}
 bool cameraTestApplied=Apply();
 if(cameraTestApplied)CameraTest_Applied();
 Log(cameraTestApplied?"Selected fixes applied in memory. Original EXE unchanged.":"Patch failed; rollback attempted.");
}
static HMODULE real=0;static INIT_ONCE once=INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK LoadOriginal(PINIT_ONCE,void*,void**){wchar_t p[MAX_PATH];GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\dinput8.dll");real=LoadLibraryW(p);return real!=0;}
static FARPROC Resolve(const char* name){if(!InitOnceExecuteOnce(&once,LoadOriginal,0,0))return 0;return GetProcAddress(real,name);}
// ---- CAMINPUT (0.3, diagnostics + CameraLog only): mouse movement the game reads, for camera.csv ----------------
// Read-only taps on the game's DirectInput: CreateDevice (IDirectInput8 slot 3) remembers the system mouse,
// GetDeviceState / GetDeviceData (IDirectInputDevice8 slots 9 / 10, shared by all devices) add up its X/Y after the
// real call; the game gets exactly what DirectInput returned. Slots are swapped once; originals are called directly.
typedef HRESULT (WINAPI *DiCreateDevFn)(void*,REFGUID,void**,void*);typedef HRESULT (WINAPI *DiStateFn)(void*,DWORD,void*);
typedef HRESULT (WINAPI *DiDataFn)(void*,DWORD,void*,DWORD*,DWORD);
static DiCreateDevFn diCreateDevO=0;static DiStateFn diStateO=0;static DiDataFn diDataO=0;static void* volatile diMouse=0;
static const GUID kSysMouse={0x6F1D2B60,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static bool SwapVt(void** slot,void* fn,void** orig){DWORD old;if(!VirtualProtect(slot,4,PAGE_READWRITE,&old))return false;*orig=*slot;*slot=fn;VirtualProtect(slot,4,old,&old);return true;}
// v2: the game may create its mouse by instance GUID, not GUID_SysMouse, so a device is a mouse when GetCapabilities
// says DI8DEVTYPE_MOUSE (0x12); a GetDeviceState of 16/20 bytes (DIMOUSESTATE/2) is counted from any device.
static void* volatile diMice[4]={0,0,0,0};
static bool IsMouse(void* d){for(int i=0;i<4;i++)if(diMice[i]==d)return true;return false;}
static HRESULT WINAPI DiState(void* self,DWORD cb,void* data){HRESULT r=diStateO(self,cb,data);
 if(SUCCEEDED(r)&&data&&(cb==16||cb==20||(IsMouse(self)&&cb>=8))){InterlockedIncrement(&diMouseStatesPub);InterlockedExchangeAdd(&camMouseX,((LONG*)data)[0]);InterlockedExchangeAdd(&camMouseY,((LONG*)data)[1]);}return r;}
static HRESULT WINAPI DiData(void* self,DWORD cbo,void* rg,DWORD* n,DWORD fl){HRESULT r=diDataO(self,cbo,rg,n,fl);
 if(SUCCEEDED(r)&&IsMouse(self)&&rg&&n&&cbo>=16&&!(fl&1)){BYTE* p=(BYTE*)rg;for(DWORD i=0;i<*n;i++,p+=cbo){DWORD ofs=*(DWORD*)p;LONG v=*(LONG*)(p+4);InterlockedIncrement(&diMouseEventsPub);if(ofs==0)InterlockedExchangeAdd(&camMouseX,v);else if(ofs==4)InterlockedExchangeAdd(&camMouseY,v);}}return r;}
static HRESULT WINAPI DiCreateDev(void* self,REFGUID g,void** dev,void* outer){HRESULT r=diCreateDevO(self,g,dev,outer);
 if(SUCCEEDED(r)&&dev&&*dev){void** vt=*(void***)*dev;
  DWORD caps[11]={44};typedef HRESULT (WINAPI *CapsFn)(void*,void*);bool mouse=IsEqualGUID(g,kSysMouse);
  if(SUCCEEDED(((CapsFn)vt[3])(*dev,caps))&&(caps[2]&0xff)==0x12)mouse=true;   // DIDEVCAPS.dwDevType low byte
  if(mouse)for(int i=0;i<4;i++)if(!diMice[i]){diMice[i]=*dev;break;}
  if(!diStateO){bool ok=SwapVt(&vt[9],(void*)DiState,(void**)&diStateO)&&SwapVt(&vt[10],(void*)DiData,(void**)&diDataO);Log(ok?"CAMINPUT: DirectInput device reads tapped":"CAMINPUT: tap failed");}
  char b[96];sprintf_s(b,"CAMINPUT: device created, type %02lx%s",caps[2]&0xff,mouse?" = mouse":"");Log(b);}
 return r;}
extern "C" HRESULT WINAPI ProxyCreate(HINSTANCE h,DWORD ver,REFIID id,LPVOID* out,LPUNKNOWN outer){typedef HRESULT(WINAPI*Fn)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);Fn fn=(Fn)Resolve("DirectInput8Create");
 HRESULT r=fn?fn(h,ver,id,out,outer):E_FAIL;
 if(SUCCEEDED(r)&&camPath[0]&&out&&*out&&!diCreateDevO){void** vt=*(void***)*out;SwapVt(&vt[3],(void*)DiCreateDev,(void**)&diCreateDevO);}
 return r;}
extern "C" HRESULT WINAPI ProxyCanUnload(){typedef HRESULT(WINAPI*Fn)();Fn fn=(Fn)Resolve("DllCanUnloadNow");return fn?fn():S_FALSE;}
extern "C" HRESULT WINAPI ProxyGetClass(REFCLSID cls,REFIID id,LPVOID* out){typedef HRESULT(WINAPI*Fn)(REFCLSID,REFIID,LPVOID*);Fn fn=(Fn)Resolve("DllGetClassObject");return fn?fn(cls,id,out):E_FAIL;}
extern "C" HRESULT WINAPI ProxyRegister(){return E_NOTIMPL;}
extern "C" HRESULT WINAPI ProxyUnregister(){return E_NOTIMPL;}
extern "C" void* WINAPI ProxyDataFormat(){typedef void*(WINAPI*Fn)();Fn fn=(Fn)Resolve("GetdfDIJoystick");return fn?fn():0;}
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,LPVOID reserved){if(reason==DLL_PROCESS_ATTACH){ctStartupLoad=reserved!=0;DisableThreadLibraryCalls(self);Initialize(self);}return TRUE;}

extern "C" DWORD __cdecl CameraCandidateStatus(){return (ctStartupLoad?1u:0u)|(ctQueued?2u:0u)|(ctArmed?4u:0u)|(camera_module::enabled?8u:0u);}
