#pragma once
#include "particle_pack.h"
// Counters only: preserve the original Map/copy calls and every argument/result.
// QPC durations are wall time on this calling thread, not GPU execution times.
struct ParticleMetric {unsigned long long calls,bytes,ticks,maxTicks;};
struct ParticleMetrics {ParticleMetric pack,map,copy;unsigned long long accepted,fallback,mapFailures,offMain;unsigned masks[256];unsigned maxParticles;};
static ParticleMetrics particleMetrics={};
static bool particleMetricsOn=false;
static void AddParticleMetric(ParticleMetric& m,unsigned long long bytes,LONGLONG ticks){
 ++m.calls;m.bytes+=bytes;m.ticks+=ticks;if((unsigned long long)ticks>m.maxTicks)m.maxTicks=ticks;
}
static int MeasureParticlePack(const BYTE* desc,const BYTE* src,unsigned n,unsigned capacity,BYTE* dst){
 if(!particleMetricsOn||!diagEnabled||!apexLockReady)return PackParticleRecords(dst,src,n,capacity,desc)?1:0;
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
 bool ok=PackParticleRecords(dst,src,n,capacity,desc);
 QueryPerformanceCounter(&b);
 EnterCriticalSection(&apexLock);
 AddParticleMetric(particleMetrics.pack,ok?(unsigned long long)n*256:0,b.QuadPart-a.QuadPart);
 if(ok){++particleMetrics.accepted;++particleMetrics.masks[desc[0x30]];if(n>particleMetrics.maxParticles)particleMetrics.maxParticles=n;}
 else ++particleMetrics.fallback; // Baseline fallback time is NOT included in pack ticks.
 if(GetCurrentThreadId()!=mainThreadId)++particleMetrics.offMain;
 LeaveCriticalSection(&apexLock);return ok?1:0;
}
typedef DWORD (__stdcall *ParticleMapFn)(void*,void*,UINT,UINT,UINT,void*);
static DWORD __stdcall ParticleMapMeasured(void* ctx,void* resource,UINT sub,UINT mode,UINT flags,void* mapped){
 ParticleMapFn fn=(ParticleMapFn)(*(void***)ctx)[14];
 if(!particleMetricsOn||!diagEnabled||!apexLockReady)return fn(ctx,resource,sub,mode,flags,mapped);
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
 DWORD hr=fn(ctx,resource,sub,mode,flags,mapped);
 QueryPerformanceCounter(&b);
 EnterCriticalSection(&apexLock);AddParticleMetric(particleMetrics.map,0,b.QuadPart-a.QuadPart);
 if((LONG)hr<0)++particleMetrics.mapFailures;
 LeaveCriticalSection(&apexLock);return hr;
}
typedef void* (__cdecl *ParticleCopyFn)(void*,const void*,size_t);
static ParticleCopyFn particleOriginalCopy=(ParticleCopyFn)0xf18000;
static void* __cdecl ParticleCopyMeasured(void* dst,const void* src,size_t n){
 if(!particleMetricsOn||!diagEnabled||!apexLockReady)return particleOriginalCopy(dst,src,n);
 LARGE_INTEGER a,b;QueryPerformanceCounter(&a);void* result=particleOriginalCopy(dst,src,n);QueryPerformanceCounter(&b);
 EnterCriticalSection(&apexLock);AddParticleMetric(particleMetrics.copy,n,b.QuadPart-a.QuadPart);LeaveCriticalSection(&apexLock);
 return result;
}
static void LogParticleMetrics(){
 if(!particleMetricsOn||!apexLockReady)return;
 ParticleMetrics m;EnterCriticalSection(&apexLock);m=particleMetrics;memset(&particleMetrics,0,sizeof(particleMetrics));LeaveCriticalSection(&apexLock);
 if(!m.pack.calls&&!m.map.calls&&!m.copy.calls)return;
 char s[700];double scale=frequency?1000.0/frequency:0;
 sprintf_s(s,"PARTICLE 2s: pack calls %llu accepted %llu fallback %llu total %.3f max %.3f ms | map calls %llu total %.3f max %.3f ms failed %llu | copy calls %llu total %.3f max %.3f ms bytes %llu | max particles/call %u off-main %llu",
 m.pack.calls,m.accepted,m.fallback,m.pack.ticks*scale,m.pack.maxTicks*scale,
 m.map.calls,m.map.ticks*scale,m.map.maxTicks*scale,m.mapFailures,
 m.copy.calls,m.copy.ticks*scale,m.copy.maxTicks*scale,m.copy.bytes,m.maxParticles,m.offMain);Log(s);
 for(unsigned i=0;i<256;++i)if(m.masks[i]){sprintf_s(s,"PARTICLE mask 0x%02x calls %u",i,m.masks[i]);Log(s);}
}
