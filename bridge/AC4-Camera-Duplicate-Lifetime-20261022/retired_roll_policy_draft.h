#pragma once
#include <windows.h>
#include <cmath>
namespace retired_roll {
// Pure decision only. No live pointer reads, registry, detour or game installation.
struct Identity {DWORD runtime,definition,reference,generation;};
struct Proof {Identity id;bool explicitManagerRetireObserved;};
struct Snapshot {
 Identity id;bool metadataValid,cameraRollTarget;
 WORD flags;float time,previous,step,duration[3];
};
static bool Same(Identity a,Identity b){return a.runtime==b.runtime&&a.definition==b.definition&&a.reference==b.reference&&a.generation==b.generation;}
static bool Suppress(const Snapshot& s,const Proof& p,bool enabled=false){
 if(!enabled||!s.metadataValid||!s.cameraRollTarget||!p.explicitManagerRetireObserved||!Same(s.id,p.id))return false;
 if(!s.id.runtime||!s.id.definition||!s.id.reference||!s.id.generation)return false;
 if((s.flags&3)!=2||(s.flags&4)||!std::isfinite(s.time)||!std::isfinite(s.previous)||!std::isfinite(s.step)||s.time!=s.previous||s.step<=0||s.step>.000500000024f)return false;
 float end=0;for(float duration:s.duration){if(!std::isfinite(duration)||duration<0)return false;end+=duration;}
 return std::isfinite(end)&&s.time>=end;
}
}
