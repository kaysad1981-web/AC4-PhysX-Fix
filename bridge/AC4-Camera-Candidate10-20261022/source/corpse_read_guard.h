#pragma once
// A successful copy does not pin an engine object or prove its generation.
namespace corpse_read {
static volatile LONG groupReadFailed=0,bodyReadFailed=0,bodyAccepted=0;
static bool Copy(DWORD address,void* out,unsigned bytes){
 if(!address||!bytes||address>0xffffffffu-(bytes-1))return false;
 SIZE_T read=0;DWORD error=GetLastError();
 BOOL ok=ReadProcessMemory(GetCurrentProcess(),(const void*)address,out,bytes,&read);
 SetLastError(error);return ok&&read==bytes;
}
static bool Group(DWORD group,DWORD* bodies,unsigned& count){
 count=0;DWORD header[2]={};
 if(group>0xffffffffu-0x3f||!Copy(group+0x38,header,sizeof header)){InterlockedIncrement(&groupReadFailed);return false;}
 unsigned n=header[1]>>16;
 if(n>40){InterlockedIncrement(&groupReadFailed);return false;}
 if(!n)return true;
 BYTE cells[40*0x60];
 if(!Copy(header[0],cells,n*0x60)){InterlockedIncrement(&groupReadFailed);return false;}
 for(unsigned i=0;i<n;i++)memcpy(bodies+i,cells+i*0x60+0x50,4);
 count=n;return true;
}
// First body position/tag, copied before decoding; no live pointer escapes to cleanup.
static bool First(DWORD group,float* position,DWORD& tag){
 tag=0;DWORD bodies[40];unsigned n=0;
 if(!Group(group,bodies,n)||!n)return false;
 BYTE data[0x12c];
 if(!RgPtr(bodies[0])||!Copy(bodies[0],data,sizeof data)){InterlockedIncrement(&bodyReadFailed);return false;}
 DWORD flags,vt;memcpy(&flags,data+0x2c,4);memcpy(&vt,data+0xe0,4);
 if((flags&31)!=6||RgClass(vt)!=5)return false;
 float p[3];memcpy(p,data+0x120,12);
 if(!RgFinite(p[0])||!RgFinite(p[1])||!RgFinite(p[2]))return false;
 memcpy(position,p,12);tag=(flags>>16)&0x3ff;return true;
}
static bool Body(DWORD rb,bool allowClass4,float& speed,float& spin){
 BYTE data[0x1cc];
 if(!RgPtr(rb)||!Copy(rb,data,sizeof data)){InterlockedIncrement(&bodyReadFailed);return false;}
 DWORD flags,vt;memcpy(&flags,data+0x2c,4);memcpy(&vt,data+0xe0,4);
 int cls=RgClass(vt);if((flags&31)!=6||(cls!=5&&!(allowClass4&&cls==4)))return false;
 float v[3],w[3];memcpy(v,data+0x1b0,12);memcpy(w,data+0x1c0,12);
 speed=RgLen(v);spin=RgLen(w);
 if(!RgFinite(speed)||!RgFinite(spin))return false;
 InterlockedIncrement(&bodyAccepted);return true;
}
}
