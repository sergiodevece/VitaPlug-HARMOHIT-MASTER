// R12 audit: standalone analysis of HARMOHIT_MASTER_DRIVE_AB.wav (no deps).
// Build: c++ -std=c++20 -O2 wav_ab.cpp -o wav_ab ; Run: ./wav_ab file.wav
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
using namespace std;
struct Wav{int rate=0,ch=0;vector<double>L,R;};
Wav load(const char*p){FILE*f=fopen(p,"rb");if(!f){perror(p);exit(1);}vector<uint8_t>b;uint8_t t[65536];size_t n;while((n=fread(t,1,sizeof t,f))>0)b.insert(b.end(),t,t+n);fclose(f);
 Wav w;size_t i=12;int bits=0;while(i+8<=b.size()){string id((char*)&b[i],4);uint32_t sz;memcpy(&sz,&b[i+4],4);
  if(id=="fmt "){w.ch=b[i+10]|b[i+11]<<8;memcpy(&w.rate,&b[i+12],4);bits=b[i+22]|b[i+23]<<8;}
  if(id=="data"){size_t fr=sz/(w.ch*bits/8);for(size_t k=0;k<fr;k++)for(int c=0;c<2;c++){const uint8_t*q=&b[i+8+(k*w.ch+c)*3];int32_t v=(q[0]|q[1]<<8|q[2]<<16);if(v&0x800000)v|=~0xFFFFFF;(c?w.R:w.L).push_back(v/8388608.0);}}
  i+=8+sz+(sz&1);}
 if(bits!=24){fprintf(stderr,"expected 24-bit\n");exit(1);}return w;}
double db(double x){return 20*log10(max(x,1e-12));}
int main(int c,char**v){auto w=load(v[1]);size_t N=w.L.size();int fs=w.rate;
 printf("frames %zu rate %d dur %.3f\n",N,fs,N/(double)fs);
 // 1. Level profile per 500 ms
 double gr=0,gp=0;for(size_t i=0;i<N;i++){gr+=w.L[i]*w.L[i]+w.R[i]*w.R[i];gp=max({gp,fabs(w.L[i]),fabs(w.R[i])});}
 printf("global RMS %.2f dBFS  peak %.2f dBFS crest %.2f dB\n",db(sqrt(gr/(2*N))),db(gp),db(gp)-db(sqrt(gr/(2*N))));
 printf("\n# t_s  rms_dBFS peak_dBFS crest_dB  (500 ms)\n");
 for(size_t s=0;s+fs/2<=N;s+=fs/2){double r=0,p=0;for(size_t i=s;i<s+fs/2;i++){r+=w.L[i]*w.L[i]+w.R[i]*w.R[i];p=max({p,fabs(w.L[i]),fabs(w.R[i])});}r=sqrt(r/fs);printf("%5.1f %7.2f %7.2f %6.2f\n",s/(double)fs,db(r),db(p),db(p)-db(r));}
 // Sample-value histogram (fraction of time above levels)
 printf("\n# fraction of samples with |x| above level\n");for(double l:{-30.,-24.,-18.,-15.,-12.,-9.,-6.,-3.}){size_t k=0;double a=pow(10,l/20);for(size_t i=0;i<N;i++){if(fabs(w.L[i])>a)k++;if(fabs(w.R[i])>a)k++;}printf("%5.0f dBFS %.5f\n",l,k/(2.0*N));}
 // 2. Find exact integer repetition lag around candidates by minimising residual on a 1 s reference at 1 s
 auto resid=[&](size_t a,size_t b,size_t len){double e=0,r=0;size_t same=0;for(size_t i=0;i<len;i++){double d1=w.L[a+i]-w.L[b+i],d2=w.R[a+i]-w.R[b+i];e+=d1*d1+d2*d2;r+=w.L[a+i]*w.L[a+i]+w.R[a+i]*w.R[a+i];same+=(d1==0)+(d2==0);}return make_tuple(e,r,same);};
 printf("\n# repetition search (coarse 1ms grid on 1s ref at t=1s, then exact)\n");
 size_t ref=fs,len=fs; vector<pair<double,long>> best;
 for(long lag=fs/2;lag+ref+len<N;lag+=fs/1000){auto[e,r,s]=resid(ref,ref+lag,len/8);best.push_back({e/r,lag});}
 sort(best.begin(),best.end());
 vector<long> lags;
 for(int k=0;k<6&&k<(int)best.size();k++){long c0=best[k].second;double bb=1e9;long bl=c0;for(long l=c0-fs/1000;l<=c0+fs/1000;l++){auto[e,r,s]=resid(ref,ref+l,len);if(e/r<bb){bb=e/r;bl=l;}}
  if(find(lags.begin(),lags.end(),bl)==lags.end()){lags.push_back(bl);printf("lag %ld samples (%.6f s) rel residual %.1f dB\n",bl,bl/(double)fs,10*log10(max(bb,1e-30)));}}
 // 3. For each found lag, residual profile across whole overlap in 250 ms windows
 for(long lag:lags){double rel0;{auto[e,r,s]=resid(ref,ref+lag,len);rel0=10*log10(max(e/r,1e-30));}if(rel0>-60)continue;
  printf("\n# residual profile, lag %ld (%.4f s): window_start_s(a) vs a+lag | rel_resid_dB  identical_%%  rmsA_dB rmsB_dB  peakA peakB\n",lag,lag/(double)fs);
  for(size_t s=0;s+fs/4+lag<=N;s+=fs/4){auto[e,r,sm]=resid(s,s+lag,fs/4);double rb=0,pa=0,pb=0;for(size_t i=s;i<s+fs/4;i++){rb+=w.L[i+lag]*w.L[i+lag]+w.R[i+lag]*w.R[i+lag];pa=max({pa,fabs(w.L[i]),fabs(w.R[i])});pb=max({pb,fabs(w.L[i+lag]),fabs(w.R[i+lag])});}
   printf("%6.2f->%6.2f %8.1f %7.2f %7.2f %7.2f %7.2f %7.2f\n",s/(double)fs,(s+lag)/(double)fs,10*log10(max(e/r,1e-30)),100.0*sm/(fs/2.0),db(sqrt(r/(fs/2.0))),db(sqrt(rb/(fs/2.0))),db(pa),db(pb));}}
}
