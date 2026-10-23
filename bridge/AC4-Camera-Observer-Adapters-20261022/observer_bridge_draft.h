#pragma once
#include "retirement_registry_draft.h"
namespace retirement_observer {
enum Kind {ConstructorInvalidate=0,InitBegin=1,InitComplete=2,ManagerSoftRetireCompleted=3,DestructorInvalidate=4,PhaseSelectionBegin=5};
static bool ReadDefinition(DWORD runtime,DWORD& definition,DWORD& reference){
 definition=reference=0;
 __try {
  DWORD candidate=*(DWORD*)(runtime+0x10);
  if(!candidate||*(LONG*)(candidate+8)>=0)return false;
  DWORD object=*(DWORD*)candidate;if(!object)return false;
  definition=object;reference=candidate;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){definition=reference=0;return false;}
}
template<unsigned Capacity>class Bridge {
 retirement_registry::Registry<Capacity>& registry_;
public:
 explicit Bridge(retirement_registry::Registry<Capacity>& registry):registry_(registry){}
 // Saved context BEFORE replay, using the CALL wrapper's PUSHFD/PUSHAD layout.
 // regs[3]+8 is ESP at original window: wrapper return address + saved EFLAGS.
 // Calls must be inside verified original object lifetime; SEH does NOT pin objects.
 bool Dispatch(Kind kind,const DWORD* regs,DWORD thread){
  if(kind==ConstructorInvalidate||kind==DestructorInvalidate)return registry_.InvalidateRuntime(regs[6]);
  if(kind==PhaseSelectionBegin)return registry_.ClearRetirementRequest(regs[6]);
  if(kind==InitBegin)return registry_.ObserveInitBegin(regs[6],thread,regs[3]+8);
  if(kind==InitComplete){
   DWORD runtime=regs[1],definition=0,reference=0;
   if(!ReadDefinition(runtime,definition,reference)){registry_.InvalidateRuntime(runtime);return false;}
   return registry_.ObserveInitComplete(runtime,thread,regs[2]+4,definition,reference);
  }
  if(kind==ManagerSoftRetireCompleted){
   DWORD runtime=regs[4],definition=0,reference=0;retired_roll::Identity current={};
   if(!registry_.Current(runtime,current))return false;
   if(!ReadDefinition(runtime,definition,reference)){registry_.InvalidateRuntime(runtime);return false;}
   if(current.definition!=definition||current.reference!=reference){registry_.InvalidateRuntime(runtime);return false;}
   return registry_.ObserveManagerRetire(current);
  }
  return false;
 }
};
}
