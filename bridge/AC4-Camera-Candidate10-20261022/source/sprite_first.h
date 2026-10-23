#pragma once
// ---- Campfire smoke flicker: ApexParticles drops the firstSprite of a sprite-buffer write --------------------
// ApexParticles_x86.dll+3B7C0 is SpriteBuffer::writeBuffer(const void* src, PxU32 firstSprite, PxU32 numSprites).
// Without CUDA interop it hands the data to the game's write callback ([this+24h]->vtbl[0](src, num)) and the
// firstSprite is lost: a chunk that belongs at sprite k is packed at sprite 0 and the draw count becomes its size.
// (The emulated-staging branch next to it has the opposite bug: writes at first*256, reads at first*64.)
// Where APEX writes a fire in more than one chunk, or not from 0, the game draws the wrong set of particles on
// those frames - one fire steady, the next one flickering. Fix (SpriteFirstFix=1): once an object gets a write with
// first != 0 it keeps a CPU copy of its sprites; every write lands at its own place and the game receives the
// whole range [0, first+num). Objects that always write from 0 are not touched (no copy, no cost).
// The first 60 writes are logged verbatim, and a SPRITES line every 2 s counts what happened.
static int spriteFirstFix=1;
static DWORD spriteBack=0;
struct SpriteShadow{DWORD self;BYTE* buf;DWORD cap;bool engaged;};
static SpriteShadow spriteTab[64];static CRITICAL_SECTION spriteLock;static bool spriteLockReady=false;
static volatile LONG spCalls=0,spFirstNZ=0,spFixed=0,spEmul=0,spMaxFirst=0,spLogged=0,spEngagedObjs=0,spTabFull=0;
typedef void (__fastcall* SpriteCallback)(DWORD obj,DWORD unusedEdx,const void* src,DWORD num);   // = thiscall(obj, src, num)

static SpriteShadow* SpriteFind(DWORD self,bool create){
 for(int i=0;i<64;i++)if(spriteTab[i].self==self)return &spriteTab[i];
 if(!create)return 0;
 for(int i=0;i<64;i++)if(!spriteTab[i].self){spriteTab[i].self=self;return &spriteTab[i];}
 InterlockedIncrement(&spTabFull);return 0;
}
// v2 diagnostic: per write, the mean of each of the 16 floats of a 64-byte sprite record (every 8th record sampled)
// and the share of records whose float k is above/below the mean - to see which field jumps on flicker frames.
static volatile LONG spDumped=0;
static void SpriteDump(const BYTE* src,DWORD num){
 LONG k=InterlockedIncrement(&spDumped);
 if(k<300||k>2300||!src||!num)return;   // skip loading; then ~40 s of writes
 double m[16]={0};DWORD n=0;
 __try{ for(DWORD i=0;i<num;i+=8){const float* f=(const float*)(src+(size_t)i*64);for(int j=0;j<16;j++){float v=f[j];if(v==v&&v>-1e30f&&v<1e30f)m[j]+=v;}n++;} }
 __except(EXCEPTION_EXECUTE_HANDLER){Log("SPRITE dump: read fault");return;}
 if(!n)return;
 char b[600];int o=sprintf_s(b,"SPRDUMP %ld num %lu src %08X |",k,num,(DWORD)src);
 for(int j=0;j<16;j++)o+=sprintf_s(b+o,sizeof(b)-o," %.4g",m[j]/n);
 Log(b);
}
// returns 1 when the write was done here, 0 = run the original code
static int __stdcall SpriteWrite(DWORD self,const BYTE* src,DWORD first,DWORD num){
 InterlockedIncrement(&spCalls);
 SpriteDump(src,num);
 if(first){InterlockedIncrement(&spFirstNZ);LONG m=spMaxFirst;while((LONG)first>m&&InterlockedCompareExchange(&spMaxFirst,(LONG)first,m)!=m)m=spMaxFirst;}
 bool emul=*(volatile DWORD*)(self+0xcc)!=0;
 if(emul)InterlockedIncrement(&spEmul);
 if(InterlockedIncrement(&spLogged)<=60){char b[160];sprintf_s(b,"SPRITE write: buffer %08X first %lu num %lu src %08X path %s",self,first,num,(DWORD)src,emul?"emulated staging":"game callback");Log(b);}
 if(!spriteFirstFix||emul||!spriteLockReady)return 0;
 DWORD cb=*(volatile DWORD*)(self+0x24);
 if(!src||!cb)return 0;
 EnterCriticalSection(&spriteLock);
 SpriteShadow* s=SpriteFind(self,first!=0);
 if(!s||(!s->engaged&&!first)){LeaveCriticalSection(&spriteLock);return 0;}   // never split: original path, no copy
 if(!s->engaged){s->engaged=true;InterlockedIncrement(&spEngagedObjs);}
 unsigned long long need=((unsigned long long)first+num)*64;
 if(need>(64ull<<20)){LeaveCriticalSection(&spriteLock);return 0;}           // beyond any campfire pool: leave it alone
 if(need>s->cap){
  DWORD cap=(DWORD)((need+0xFFFF)&~0xFFFFull);BYTE* nb=(BYTE*)VirtualAlloc(0,cap,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  if(!nb){LeaveCriticalSection(&spriteLock);return 0;}
  if(s->buf){memcpy(nb,s->buf,s->cap);VirtualFree(s->buf,0,MEM_RELEASE);}
  s->buf=nb;s->cap=cap;
 }
 memcpy(s->buf+(size_t)first*64,src,(size_t)num*64);
 BYTE* buf=s->buf;LeaveCriticalSection(&spriteLock);
 if(first)InterlockedIncrement(&spFixed);
 ((SpriteCallback)(*(DWORD*)(*(DWORD*)cb)))(cb,0,buf,first+num);
 return 1;
}
__declspec(naked) static void SpriteWriteHook(){__asm{
 push ecx
 push dword ptr [esp+16]
 push dword ptr [esp+16]
 push dword ptr [esp+16]
 push ecx
 call SpriteWrite
 pop ecx
 test eax,eax
 jz original
 ret 0ch
original:
 push ebp
 mov ebp,esp
 push esi
 mov esi,ecx
 jmp dword ptr [spriteBack]
}}
static void PatchSpriteWrite(HMODULE m){
 static volatile LONG done=0;if(!m||InterlockedExchange(&done,1))return;
 if(!spriteLockReady){InitializeCriticalSection(&spriteLock);spriteLockReady=true;}
 static const BYTE sig[]={0x55,0x8b,0xec,0x56,0x8b,0xf1,0x83,0xbe,0xcc,0x00,0x00,0x00,0x00,0x74,0x58};
 BYTE* hit=(BYTE*)m+0x3b7c0;
 if(IsBadReadPtr(hit,sizeof(sig))||memcmp(hit,sig,sizeof(sig))){Log("PhysX SPRITES: writeBuffer not recognised; sprite writes unchanged.");return;}
 spriteBack=(DWORD)hit+6;
 BYTE code[8];code[0]=0xe9;*(DWORD*)(code+1)=(DWORD)SpriteWriteHook-(DWORD)hit-5;code[5]=0x90;code[6]=hit[6];code[7]=hit[7];
 DWORD old;if(!VirtualProtect(hit,8,PAGE_EXECUTE_READWRITE,&old)){Log("PhysX SPRITES: could not patch.");return;}
 LONGLONG expected;memcpy(&expected,hit,8);LONGLONG wanted;memcpy(&wanted,code,8);
 bool ok=InterlockedCompareExchange64((volatile LONGLONG*)hit,wanted,expected)==expected;
 FlushInstructionCache(GetCurrentProcess(),hit,8);DWORD ignored;VirtualProtect(hit,8,old,&ignored);
 Log(ok?(spriteFirstFix?"PhysX SPRITES: writeBuffer watched, firstSprite fix ON (SpriteFirstFix=1).":"PhysX SPRITES: writeBuffer watched, firstSprite fix OFF (SpriteFirstFix=0)."):"PhysX SPRITES: patch failed.");
}
static void LogSpriteStats(){
 LONG c=InterlockedExchange(&spCalls,0),nz=InterlockedExchange(&spFirstNZ,0),fx=InterlockedExchange(&spFixed,0),em=InterlockedExchange(&spEmul,0),mf=InterlockedExchange(&spMaxFirst,0);
 if(!c)return;
 char b[220];sprintf_s(b,"SPRITES 2s: writes %ld, with first!=0 %ld (max first %ld), fixed %ld, emulated-staging path %ld | split buffers %ld, table full %ld | fix %s",
  c,nz,mf,fx,em,spEngagedObjs,spTabFull,spriteFirstFix?"on":"off");Log(b);
}
