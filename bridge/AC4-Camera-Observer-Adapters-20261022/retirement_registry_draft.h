#pragma once
#include "../AC4-Camera-Duplicate-Lifetime-20261022/retired_roll_policy_draft.h"
namespace retirement_registry {
using namespace retired_roll;
template<unsigned Capacity>class Registry {
 static_assert(Capacity>0,"Nonempty registry");
 struct Entry {Identity id;DWORD state,initThread,initFrame;bool requested;}; // 0 empty, 1 initializing, 2 ready
 volatile LONG lock_,poisoned_;DWORD nextGeneration_;Entry entries_[Capacity];
 Registry(const Registry&);Registry& operator=(const Registry&);
 bool Lock(bool mutation){
  if(InterlockedCompareExchange(&poisoned_,0,0))return false;
  if(InterlockedCompareExchange(&lock_,1,0)){
   // A missed lifecycle mutation may otherwise leave an old permission alive.
   if(mutation)InterlockedExchange(&poisoned_,1);
   return false;
  }
  if(InterlockedCompareExchange(&poisoned_,0,0)){InterlockedExchange(&lock_,0);return false;}
  return true;
 }
 void Unlock(){InterlockedExchange(&lock_,0);}
 Entry* Find(DWORD runtime){for(unsigned i=0;i<Capacity;i++)if(entries_[i].state&&entries_[i].id.runtime==runtime)return entries_+i;return 0;}
 Identity BeginLocked(DWORD runtime){
  Identity none={};Entry* entry=Find(runtime);
  if(!entry)for(unsigned i=0;i<Capacity;i++)if(!entries_[i].state){entry=entries_+i;break;}
  if(!entry)return none;
  if(nextGeneration_==0xffffffff){InterlockedExchange(&poisoned_,1);return none;}
  *entry={};entry->id.runtime=runtime;entry->id.generation=++nextGeneration_;entry->state=1;return entry->id;
 }
#ifdef REGISTRY_TESTING
 friend struct TestAccess;
#endif
public:
 Registry():lock_(0),poisoned_(0),nextGeneration_(0){for(unsigned i=0;i<Capacity;i++)entries_[i]={};}
 bool Poisoned(){return InterlockedCompareExchange(&poisoned_,0,0)!=0;}
 // Observe BEFORE initialization changes the object's identity. Scalar inputs only.
 Identity BeginInit(DWORD runtime){
  Identity none={};if(!runtime||!Lock(true))return none;
  Identity token=BeginLocked(runtime);Unlock();return token;
 }
 // For synchronous original calls: entry ESP equals exit EBP+4, thread unchanged.
 bool ObserveInitBegin(DWORD runtime,DWORD thread,DWORD frame){
  if(!runtime)return false;
  if(!thread||!frame){InvalidateRuntime(runtime);return false;}
  if(!Lock(true))return false;Identity token=BeginLocked(runtime);
  if(token.generation){Entry* entry=Find(runtime);entry->initThread=thread;entry->initFrame=frame;}
  Unlock();return token.generation!=0;
 }
 bool ObserveInitComplete(DWORD runtime,DWORD thread,DWORD frame,DWORD definition,DWORD reference){
  if(!thread||!frame||!definition||!reference)return false;
  if(!Lock(true))return false;Entry* entry=Find(runtime);
  bool ok=entry&&entry->state==1&&entry->initThread==thread&&entry->initFrame==frame;
  if(ok){entry->id.definition=definition;entry->id.reference=reference;entry->state=2;entry->initThread=entry->initFrame=0;}
  Unlock();return ok;
 }
 // Observe AFTER the original initialization successfully returns. Requires its token.
 bool CompleteInit(Identity token,DWORD definition,DWORD reference){
  if(!definition||!reference||!Lock(true))return false;
  Entry* entry=Find(token.runtime);bool ok=entry&&entry->state==1&&entry->id.generation==token.generation&&token.generation;
  if(ok){entry->id.definition=definition;entry->id.reference=reference;entry->state=2;}
  Unlock();return ok;
 }
 // Resolve a CURRENT observed identity; this is not a lifetime pin for game memory.
 bool Current(DWORD runtime,Identity& out){
  out={};if(!Lock(false))return false;Entry* entry=Find(runtime);bool ok=entry&&entry->state==2;
  if(ok)out=entry->id;Unlock();return ok;
 }
 // Call only for the verified manager request, not inferred from matching curves.
 bool ObserveManagerRetire(Identity id){
  if(!Lock(true))return false;Entry* entry=Find(id.runtime);
  bool ok=entry&&entry->state==2&&Same(entry->id,id);if(ok)entry->requested=true;
  Unlock();return ok;
 }
 // A new phase selection invalidates prior retirement intent, even without reallocation.
 // Manager completion may explicitly certify the new phase-2 request afterwards.
 bool ClearRetirementRequest(DWORD runtime){
  if(!Lock(true))return false;Entry* entry=Find(runtime);if(entry)entry->requested=false;Unlock();return true;
 }
 // Observe BEFORE destructor/reinitialization releases any old state.
 bool InvalidateRuntime(DWORD runtime){
  if(!Lock(true))return false;Entry* entry=Find(runtime);if(entry)*entry={};Unlock();return true;
 }
 bool ShouldSuppress(const retired_roll::Snapshot& snapshot,bool enabled=false){
  if(!enabled||!Lock(false))return false;Entry* entry=Find(snapshot.id.runtime);bool result=false;
  if(entry&&entry->state==2){Proof proof={entry->id,entry->requested};result=retired_roll::Suppress(snapshot,proof,true);}
  // Linearization point of the decision. Later invalidation still requires engine lifetime rules.
  result=result&&!InterlockedCompareExchange(&poisoned_,0,0);Unlock();return result;
 }
};
}
