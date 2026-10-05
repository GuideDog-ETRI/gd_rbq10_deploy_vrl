#pragma once
//
// PolicyBackend 의 vision-RL 확장. PolicyBackend.cpp/DreamBackend(blind, 2-input)
// 는 이 작업으로 인해 단 한 줄도 바뀌지 않는다 -- 그 파일이 field-tested 라는
// 확립된 컨벤션(PolicyBackend.cpp 머리주석 "2026-08-17 실기 검증된 경로라 수식과
// 상수를 손대지 않았다") 을 그대로 존중해서, proprio 조립 로직은 여기 새로
// 복제했다. gd_lab/vision_rl의 export_vrl.py가 내보내는 3-input 계약
// (direct_obs, cenet_obs, terrain_latent -> actions, z_t) 을 돌리고,
// terrain_latent 는 VisionStudentThread(카메라 4개 + student ONNX, 저속
// 비동기) 가 채운다.
//
// actor onnx 옆에 <stem>_student.onnx 가 있어야 한다 (export_student_vrl.py
// 가 그렇게 내보낸다) -- 파일 위치가 곧 계약이라는 이 저장소의 관례를 그대로
// 따른 것 (WalkConfig.hpp 머리주석 참고), 그래서 RBQ_POLICY_FILE 하나 말고
// "student 는 어디 있나" 를 위한 새 설정은 없다.
//
// PolicyBackend::create() 는 .onnx 파일의 입력 개수(2 vs 3)로 Dream 과
// DreamVrl 을 가른다 -- 그 판별 한 곳만 PolicyBackend.cpp 를 최소로 건드린다.

#include <memory>
#include <string>

#include "PolicyBackend.hpp"

class DreamVrlBackend final : public PolicyBackend {
public:
    DreamVrlBackend();
    ~DreamVrlBackend() override;

    bool load(const std::string& actorOnnxPath, float payloadKg = 0.f);

    int decimation() const override;
    void gains(float kp[12], float kd[12]) const override;
    void reset(const RbqLink::Snapshot& snap) override;
    bool infer(const RbqLink::Snapshot& snap, const float cmd[3], float targetPos[12]) override;
    std::string describe() const override;
    bool visionExpired() const override;
    bool readyForWalk() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
