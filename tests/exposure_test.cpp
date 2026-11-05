#include <array>
#include <cstdio>
#include <limits>
#include "engine/render/exposure_math.h"

int main() {
    using namespace pt::exposure_math;
    int failures = 0;
    const auto check = [&](const char* name, bool ok) { std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL"); failures += !ok; };
    std::array<glm::vec2, 1024> samples;
    samples.fill({0.18f, 1.0f});
    check("uniform gray preserved", std::abs(Meter(samples) - 0.18f) < 1e-6f);
    for (int i=0;i<10;++i) samples[i].x=60000.0f;
    check("one percent fireflies rejected", std::abs(Meter(samples) - 0.18f) < 1e-6f);
    samples[10]={std::numeric_limits<float>::infinity(),1};
    samples[11]={std::numeric_limits<float>::quiet_NaN(),1};
    samples[12]={100,0};
    check("invalid samples excluded", std::abs(Meter(samples)-0.18f)<1e-6f);
    for(float scale:{0.001f,0.1f,1.0f,16.0f,1000.0f}) {
        for(int i=0;i<1024;++i) samples[i]={scale*(0.01f+float(i)/1000),0.25f+float(i%17)/20};
        const float a=Meter(samples)/scale;
        for(auto& v:samples) v.x/=scale;
        check("meter homogeneous under pre-exposure",std::abs(a-Meter(samples))<1e-5f);
    }
    samples.fill({0,0});check("uncovered view holds exposure",Meter(samples)<0);
    samples.fill({0,1});check("black view has finite bounded target",TargetEv(Meter(samples),1,-10,1)==1);
    check("uniform key has zero EV",TargetEv(.18f,.18f,-10,1)==0);
    for(float target:{-8.0f,0.0f}) {
        const float start=target==-8?0:-8;
        float reference=start;
        for(int i=0;i<600;++i) reference=Adapt(reference,target,1.0f/60,1.6f);
        for(int fps:{15,30,60,120,240}) {
            float ev=start;
            bool monotonic=true;
            for(int i=0;i<fps*10;++i) {
                float next=Adapt(ev,target,1.0f/fps,1.6f);
                monotonic &= std::abs(next-target)<=std::abs(ev-target)+1e-6f && next>=std::min(start,target) && next<=std::max(start,target);
                ev=next;
            }
            check("transition monotonic, bounded, frame-rate independent",monotonic && std::abs(ev-reference)<1e-4f && std::abs(ev-target)<0.01f);
        }
    }
    check("pause holds",Adapt(-4,-8,0,1.6f)==-4);
    check("invalid dt holds",Adapt(-4,-8,std::numeric_limits<float>::quiet_NaN(),1.6f)==-4);
    check("long stall limited to 0.3 stop",std::abs(Adapt(-4,-8,30,1.6f)+4)<=.30001f);
    check("zero authored speed holds",Adapt(-4,-8,.016f,0)==-4);
    // A delayed readback with varying exposure must still reconstruct one target.
    for(float used:{0.002f,0.01f,0.1f,2.0f}) check("delayed pre-exposure cancels",std::abs(TargetEv(32*used/used,1,-10,1)+5)<1e-5f);
    float jitter=0;for(int i=0;i<600;++i) jitter=Adapt(jitter,i%2?.1f:-.1f,1.0f/60,1.6f);
    check("small alternating target damped",std::abs(jitter)<.04f);
    return failures?1:0;
}
