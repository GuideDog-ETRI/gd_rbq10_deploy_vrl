#include "../tools/src/StudentDecoderDiagnostics.hpp"
#include <cassert>
int main(int argc,char** argv) {
    using namespace decoder_diagnostics;
    assert(displayCell(0,0)==std::make_pair(16,10));
    assert(displayCell(10,16)==std::make_pair(0,0));
    assert(displayCell(5,8)==std::make_pair(8,5));
    for(int r=0;r<11;++r)for(int c=0;c<17;++c) {
        const auto cell=displayCell(r,c);
        assert(cell.first==16-c&&cell.second==10-r);
    }
    if(argc!=2)return 2;
    std::filesystem::path dir=argv[1];
    Frame f; f.sequence=1234567890123ULL;f.inputStampMs=1234567890000LL;f.observerMs=f.inputStampMs+10;
    f.frames.assign(28800,.25f);f.hidden.assign(6116,.123f);f.latent.assign(32,-.5f);f.decoded.assign(1122,.75f);
    assert(writeFrame(dir/"frame_test.json",f,2000000)>0);
    bool collision=false;try{writeFrame(dir/"frame_test.json",f,2000000);}catch(...){collision=true;}assert(collision);
    assert(writeFrame(dir/"too_big.json",f,1)==0);
    assert(!std::filesystem::exists(dir/"too_big.json"));
    f.frames[0]=NAN;bool invalid=false;
    try{writeFrame(dir/"invalid.json",f,2000000);}catch(...){invalid=true;}assert(invalid);f.frames[0]=.25f;
    std::filesystem::create_directory(dir/"async");
    {AsyncRecorder writer(dir/"async",1,2000000);assert(writer.submit(f));writer.close();assert(writer.error().empty());}
    assert(std::filesystem::exists(dir/"async"/"frame_1234567890123.json"));
    return 0;
}
