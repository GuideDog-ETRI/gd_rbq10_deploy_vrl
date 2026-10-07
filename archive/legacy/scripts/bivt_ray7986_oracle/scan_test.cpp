#include "../../oracle_runtime/cvtt/perception/CvttTerrainScan.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
 CvttTerrainScan scan(argv[1]);
 auto flat=scan.observe({{0,0,.5},{1,0,0,0}},nullptr);
 for(int i=0;i<187;++i){assert(flat[i]==0);assert(flat[187+i]==1);}
 auto gap=scan.observe({{11.325,0,.5},{1,0,0,0}},nullptr);
 int deep=0;for(int i=0;i<187;++i)deep+=gap[i]>4.9;
 assert(deep==11);assert(gap[5*17+15]==5);
 std::cout<<"flat 187 valid; 5cm gap samples 11, normalized depth 5: PASS\n";
}
