// Test PC des lecteurs Aurora (content.db + .asset).
// 1) python3 make_aurora_samples.py   2) g++ -std=c++14 -fsanitize=address,undefined -I.. test_aurora_library.cpp ../aurora_library.cpp -o t && ./t
#include "aurora_library.h"
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
static std::vector<unsigned char> rd(const char* p){ std::ifstream f(p,std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),{}); }
int main(){
  int fails=0;
  std::vector<unsigned char> db=rd("content.db");
  std::vector<AuroraTitle> t;
  bool ok=ReadAuroraTitles(&db[0],db.size(),t);
  std::ifstream ex("expected.txt"); std::string line; int n=0, bad=0;
  while(std::getline(ex,line)){ unsigned tid=strtoul(line.substr(0,8).c_str(),0,16); std::string nm=line.substr(9); bool f=false;
    for(auto&a:t) if(a.titleId==tid){ f=true; if(a.name!=nm){bad++; if(bad<3) printf("nom different %08X [%s] vs [%s]\n",tid,a.name.c_str(),nm.c_str());} }
    if(!f){bad++; if(bad<3) printf("absent %08X\n",tid);} n++; }
  printf("content.db : ok=%d titres=%zu attendus=%d erreurs=%d\n",ok,t.size(),n,bad); if(!ok||bad||(int)t.size()!=n) fails++;
  for(const char* f: {"rgba.asset","bc3.asset"}){
    std::vector<unsigned char> a=rd(f), px; int w,h;
    bool r=DecodeAuroraAsset(&a[0],a.size(),2,0,0,px,w,h);
    printf("%s : ok=%d %dx%d\n",f,r,w,h);
    if(!r||w!=90||h!=123) fails++;
    std::string o=std::string(f)+".raw"; FILE* fo=fopen(o.c_str(),"wb"); fwrite(&px[0],1,px.size(),fo); fclose(fo);
    std::vector<unsigned char> s; int sw,sh; DecodeAuroraAsset(&a[0],a.size(),2,40,40,s,sw,sh); printf("  reduit : %dx%d\n",sw,sh);
    if(!(sw<=40&&sh<=40)) fails++;
    std::vector<unsigned char> none; int nw,nh; if(DecodeAuroraAsset(&a[0],a.size(),0,0,0,none,nw,nh)) fails++;   // icone absente
  }
  // entrees corrompues : ne doivent pas planter
  for(int k=0;k<200;k++){ std::vector<unsigned char> a=rd("bc3.asset"); for(int j=0;j<20;j++) a[rand()%0x800]^=rand(); std::vector<unsigned char> px; int w,h; DecodeAuroraAsset(&a[0],a.size(),2,64,64,px,w,h); }
  for(int k=0;k<30;k++){ std::vector<unsigned char> d=db; for(int j=0;j<200;j++) d[100+rand()%(d.size()-100)]^=rand(); std::vector<AuroraTitle> tt; ReadAuroraTitles(&d[0],d.size(),tt); }
  printf(fails?"ECHEC %d\n":"TOUT OK\n",fails); return fails;
}
