#pragma once
// Called under keptLock. IDs are metadata only; never dereference game objects here.
namespace corpse_groups {
static WORD references[1024]={};
static volatile LONG acquired=0,released=0,invalid=0;
static void Acquire(DWORD tag,DWORD& held,DWORD* bits){
 if(held)return;
 if(!tag)return; // Group zero has no kept collision bit.
 if(tag>=1024||references[tag]==0xffff){InterlockedIncrement(&invalid);return;}
 if(++references[tag]==1)_interlockedbittestandset((volatile LONG*)bits,tag);
 held=1;InterlockedIncrement(&acquired);
}
static void Release(DWORD tag,DWORD& held,DWORD* bits){
 if(!held)return;
 held=0;
 if(!tag||tag>=1024||!references[tag]){InterlockedIncrement(&invalid);return;}
 if(--references[tag]==0)_interlockedbittestandreset((volatile LONG*)bits,tag);
 InterlockedIncrement(&released);
}
}
