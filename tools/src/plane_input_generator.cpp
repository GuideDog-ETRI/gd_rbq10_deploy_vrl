// Offline CPU-only FK + ray/plane intersection; no rollout, DDS or GL context.
#include <mujoco/mujoco.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) return 2;  // model.xml, STAND telemetry.json, output prefix
    using nlohmann::json;
    std::ifstream stateFile(argv[2]);
    json states; stateFile >> states;
    const auto state = states.at("rows").back();
    if (state.at("fsm").get<int>() != 6) throw std::runtime_error("STAND required");
    char error[1024]{};
    mjModel* m = mj_loadXML(argv[1], nullptr, error, sizeof(error));
    if (!m) throw std::runtime_error(error);
    mjData* d = mj_makeData(m);
    if (m->jnt_type[0] != mjJNT_FREE) throw std::runtime_error("free base required");
    const int base = m->jnt_qposadr[0];
    d->qpos[base] = d->qpos[base+1] = d->qpos[base+2] = 0;
    const auto rpy = state.at("rpy").get<std::vector<double>>();
    const double cr=std::cos(rpy[0]/2), sr=std::sin(rpy[0]/2);
    const double cp=std::cos(rpy[1]/2), sp=std::sin(rpy[1]/2);
    const double cy=std::cos(rpy[2]/2), sy=std::sin(rpy[2]/2);
    d->qpos[base+3]=cr*cp*cy+sr*sp*sy;
    d->qpos[base+4]=sr*cp*cy-cr*sp*sy;
    d->qpos[base+5]=cr*sp*cy+sr*cp*sy;
    d->qpos[base+6]=cr*cp*sy-sr*sp*cy;
    int applied=0;
    for (int j=1; j<m->njnt; ++j) {
        const char* name=mj_id2name(m,mjOBJ_JOINT,j);
        int index=-1;
        if (name && std::sscanf(name,"joint%d_",&index)==1 && index>=0 && index<12) {
            d->qpos[m->jnt_qposadr[j]]=state.at("q").at(index).get<double>();
            ++applied;
        }
    }
    if (applied!=12) throw std::runtime_error("expected 12 motor-ordered joints");
    mj_forward(m,d);
    std::vector<double> heights;
    for (const char* foot : {"RR","RL","FR","FL"}) {
        const int g=mj_name2id(m,mjOBJ_GEOM,foot);
        if (g<0 || m->geom_type[g]!=mjGEOM_SPHERE) throw std::runtime_error("sphere foot required");
        heights.push_back(m->geom_size[g*3]-d->geom_xpos[g*3+2]);
    }
    auto sorted=heights; std::sort(sorted.begin(),sorted.end());
    const double height=(sorted[1]+sorted[2])/2;
    if (height<.2 || height>.9 || sorted.back()-sorted.front()>.03)
        throw std::runtime_error("invalid standing/contact height estimate");
    d->qpos[base+2]=height;
    mj_forward(m,d);
    mjvScene scene; mjv_defaultScene(&scene); mjv_makeScene(m,&scene,2000);
    mjvOption option; mjv_defaultOption(&option);
    constexpr int W=80,H=45,N=W*H;
    std::vector<float> frames(4*2*N);
    json metadata={{"shape",{1,4,2,H,W}}, {"height_estimate_method","median foot-sphere FK contact with z=0 plane"},
        {"base_height_m",height},{"per_foot_height_estimates_m",heights},{"stand_rpy",rpy},
        {"depth_contract","optical_axis_metres, clip .15..5, mm quantization, normalized 0..1"},
        {"IR_uniform",128.0/255.0},{"robot_self_occlusion",false},{"plane_extent","infinite"}};
    for (int c=0;c<4;++c) {
        std::string name="BT"+std::to_string(c);
        mjvCamera camera; mjv_defaultCamera(&camera); camera.type=mjCAMERA_FIXED;
        camera.fixedcamid=mj_name2id(m,mjOBJ_CAMERA,name.c_str());
        if (camera.fixedcamid<0) throw std::runtime_error("camera missing");
        mjv_updateScene(m,d,&option,nullptr,&camera,mjCAT_ALL,&scene);
        const mjvGLCamera gl=mjv_averageCamera(&scene.camera[0],&scene.camera[1]);
        const double width=(gl.frustum_top-gl.frustum_bottom)*W/H;
        double right[3]={gl.forward[1]*gl.up[2]-gl.forward[2]*gl.up[1],
            gl.forward[2]*gl.up[0]-gl.forward[0]*gl.up[2],
            gl.forward[0]*gl.up[1]-gl.forward[1]*gl.up[0]};
        int hits=0; double sum=0, minimum=5, maximum=.15;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            const double u=(gl.frustum_center+((x+.5)/W-.5)*width)/gl.frustum_near;
            const double v=(gl.frustum_bottom+(H-y-.5)/H*(gl.frustum_top-gl.frustum_bottom))/gl.frustum_near;
            const double dz=gl.forward[2]+right[2]*u+gl.up[2]*v;
            double depth=dz < -1e-9 ? -gl.pos[2]/dz : 5.0;
            if (dz < -1e-9 && depth < 5) ++hits;
            depth=std::clamp(depth,.15,5.0);
            depth=std::floor(depth*1000)/1000;
            frames[c*2*N+y*W+x]=float((depth-.15)/4.85);
            frames[(c*2+1)*N+y*W+x]=128.0f/255.0f;
            minimum=std::min(minimum,depth); maximum=std::max(maximum,depth); sum+=depth;
        }
        metadata["cameras"][name]={{"position_world_m",{gl.pos[0],gl.pos[1],gl.pos[2]}},
            {"forward_world",{gl.forward[0],gl.forward[1],gl.forward[2]}},
            {"depth_min_m",minimum},{"depth_max_m",maximum},{"depth_mean_m",sum/N},
            {"within_5m_floor_fraction",double(hits)/N}};
        // Portable preview of depth only (near=black, far=white).
        std::ofstream preview(std::string(argv[3])+"_"+name+".pgm",std::ios::binary);
        preview<<"P5\n"<<W<<" "<<H<<"\n255\n";
        for(int i=0;i<N;++i) preview.put(static_cast<char>(std::lround(frames[c*2*N+i]*255)));
    }
    std::ofstream out(std::string(argv[3])+".f32",std::ios::binary);
    out.write(reinterpret_cast<const char*>(frames.data()),frames.size()*sizeof(float));
    std::ofstream meta(std::string(argv[3])+".json"); meta<<metadata.dump(2)<<"\n";
    std::cout<<metadata.dump(2)<<"\n";
    mjv_freeScene(&scene); mj_deleteData(d); mj_deleteModel(m);
}
