#pragma once
#include "../AC4-Camera-Observer-Adapters-20261022/retirement_registry_draft.h"
namespace camera_binding_filter {
static bool Span(DWORD p,unsigned size){return p&&size&&p<=0xffffffffu-(size-1);}
static DWORD Weak(DWORD p){return Span(p,12)&&*(LONG*)(p+8)<0?*(DWORD*)p:0;}
static bool Member(DWORD item,DWORD list,unsigned count,unsigned stride,unsigned limit){
 return count&&count<=limit&&Span(list,count*stride)&&item>=list&&(item-list)%stride==0&&(item-list)/stride<count;
}
// Exact current target, not "any roll somewhere in this FX". No whole-instance suppression.
static bool ReadSnapshot(DWORD binding,DWORD index,DWORD camera,retired_roll::Snapshot& out){
 out={};
 __try {
  if(!Span(binding,0x90)||!Span(camera,4)||*(DWORD*)camera!=0x1e5d688)return false;
  DWORD property=*(DWORD*)(binding+4);if(!Span(property,4))return false;
  DWORD flags=*(DWORD*)property;if((flags&7)!=1||((flags>>6)&7)!=1)return false;
  DWORD group=*(DWORD*)binding;if(!Span(group,0x1c))return false;
  DWORD runtime=*(DWORD*)group,node=*(DWORD*)(group+4);
  if(!Span(runtime,0x158)||*(DWORD*)runtime!=0x1e66170||!Span(node,0x14)||*(DWORD*)node!=0x1e659e0)return false;
  if(Weak(*(DWORD*)(group+0x14))!=camera)return false;
  if(!Member(group,*(DWORD*)(runtime+0x8c),*(WORD*)(runtime+0x92),0x1c,64))return false;
  if(!Member(binding,*(DWORD*)(group+8),*(WORD*)(group+0xe),0x90,128))return false;
  DWORD list=*(DWORD*)(binding+0x84);unsigned count=*(WORD*)(binding+0x8a);
  if(!count||count>64||index>=count||!Span(list,count*0x1c))return false;
  DWORD target=list+index*0x1c,proxy=*(DWORD*)(target+0x14),accessor=target;
  if(proxy){if(!Span(proxy,0x38)||!*(DWORD*)proxy)return false;accessor=proxy+4;}
  // Descriptor-based, indirect and indexed properties are deliberately unsupported here.
  if((*(BYTE*)(accessor+8)&3)!=1||*(DWORD*)accessor!=0x0070000a||*(DWORD*)(accessor+4)!=camera)return false;
  DWORD reference=*(DWORD*)(runtime+0x10),definition=Weak(reference);
  if(!Span(definition,0x24))return false;
  out.id.runtime=runtime;out.id.definition=definition;out.id.reference=reference;
  out.flags=*(WORD*)(runtime+0x94);out.time=*(float*)(runtime+0x14);
  out.previous=*(float*)(runtime+0x98);out.step=*(float*)(runtime+0x9c);
  for(unsigned i=0;i<3;i++)out.duration[i]=*(float*)(definition+0x18+i*4);
  out.metadataValid=out.cameraRollTarget=true;return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){out={};return false;}
}
template<unsigned Capacity>static bool Apply(retirement_registry::Registry<Capacity>& registry,DWORD binding,DWORD index,DWORD camera,BYTE* savedFx,bool enabled=false){
 if(!enabled)return false;
 retired_roll::Snapshot snapshot={};if(!ReadSnapshot(binding,index,camera,snapshot))return false;
 retired_roll::Identity id={};if(!registry.Current(snapshot.id.runtime,id))return false;
 if(id.definition!=snapshot.id.definition||id.reference!=snapshot.id.reference)return false;
 snapshot.id=id;
 float old=*(float*)(savedFx+160),delta=*(float*)(savedFx+176);
 if(!std::isfinite(old)||!std::isfinite(delta)||!registry.ShouldSuppress(snapshot,true))return false;
 *(DWORD*)(savedFx+176)=0;return true;
}
}
