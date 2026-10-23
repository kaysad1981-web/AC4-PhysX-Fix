#pragma once
// AC4 Steam CPU packing only. GPU upload, simulation and particle count are unchanged.
static bool PackParticleRecords(unsigned char* dst,const unsigned char* src,unsigned n,
                                unsigned capacity,const unsigned char* desc) {
 if(!n||n>capacity||n>250000||!src||!dst||!desc)return false;
 unsigned stride=*(const unsigned*)(desc+0xc),offset[8];
 const unsigned flags=desc[0x30];
 static const unsigned lengths[8]={12,16,8,4,4,4,12,4};
 unsigned maxEnd=0;
 for(unsigned i=0;i<8;++i){
  offset[i]=*(const unsigned*)(desc+0x10+i*4);
  if(flags&(1u<<i)){
   // Unknown layouts use the existing path; never expand the accepted input domain.
   if(offset[i]>0x3fffffffU)return false;
   unsigned long long end=(unsigned long long)offset[i]*4+lengths[i];
   if(end>(unsigned long long)stride*4)return false;
   if(end>maxEnd)maxEnd=(unsigned)end;
  }
 }
 unsigned long long inputBytes=(unsigned long long)(n-1)*stride*4+maxEnd;
 unsigned long long inputEnd=(unsigned long long)(uintptr_t)src+inputBytes;
 unsigned long long outputEnd=(unsigned long long)(uintptr_t)dst+(unsigned long long)n*256;
 if(inputEnd>0xffffffffULL||outputEnd>0xffffffffULL)return false;
 if(inputBytes&&(uintptr_t)src<outputEnd&&(uintptr_t)dst<inputEnd)return false;
 for(unsigned p=0;p<n;++p){
  memset(dst,0,64);
  if(flags&1)memcpy(dst,src+offset[0]*4,12);
  if(flags&2)memcpy(dst+12,src+offset[1]*4,16);
  if(flags&4)memcpy(dst+28,src+offset[2]*4,8);
  if(flags&8)memcpy(dst+36,src+offset[3]*4,4);
  if(flags&16)memcpy(dst+40,src+offset[4]*4,4);
  if(flags&32)memcpy(dst+44,src+offset[5]*4,4);
  if(flags&64)memcpy(dst+48,src+offset[6]*4,12);
  if(flags&128)memcpy(dst+60,src+offset[7]*4,4);
  memcpy(dst+64,dst,64);memcpy(dst+128,dst,64);memcpy(dst+192,dst,64);
  dst+=256;
  if(p+1<n)src+=(size_t)stride*4;
 }
 return true;
}
