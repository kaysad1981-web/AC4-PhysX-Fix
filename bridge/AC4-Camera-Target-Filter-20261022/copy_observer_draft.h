#pragma once
#include "../AC4-Camera-Observer-Adapters-20261022/observer_bridge_draft.h"
namespace copy_observer {
template<unsigned Capacity>class Bridge {
 retirement_registry::Registry<Capacity>& registry_;
 retirement_observer::Bridge<Capacity> previous_;
public:
 explicit Bridge(retirement_registry::Registry<Capacity>& registry):registry_(registry),previous_(registry){}
 bool Dispatch(unsigned kind,const DWORD* regs,DWORD thread){
  // 70D1AF before replay: EAX=destination; invalidate BEFORE subsequent base/state copying.
  // Conservative: copied instances remain untracked until another observed full initialization.
  if(kind==6)return registry_.InvalidateRuntime(regs[7]);
  return previous_.Dispatch((retirement_observer::Kind)kind,regs,thread);
 }
};
}
