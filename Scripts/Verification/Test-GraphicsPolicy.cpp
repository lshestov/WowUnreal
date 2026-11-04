#include "WowRenderPolicy.h"
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    int Checks=0;
    const auto Check=[&](bool Passed,const char* Message) {
        ++Checks; if(!Passed) {std::cerr<<"FAIL: "<<Message<<'\n'; std::exit(1);}
    };
    const float Sizes[]={.5f,1,1.01f,4,4.01f,15,15.01f,100,100.01f};
    const int Levels[]={0,0,1,1,2,2,3,3,4};
    for(int I=0;I<9;++I) Check(WowRenderPolicy::DoodadFadeLevel(Sizes[I])==Levels[I],"Wrath size boundary");
    const float Low[]={3000,5000,10000,37500,125000},High[]={3000,15000,30000,112500,125000};
    for(int I=0;I<5;++I) {
        Check(WowRenderPolicy::DoodadCullDistanceCm(I,.5f)==Low[I],"minimum world distance");
        Check(WowRenderPolicy::DoodadCullDistanceCm(I,1.5f)==High[I],"maximum world distance");
    }
    Check(WowRenderPolicy::ParticleDensity(-99)==.1f && WowRenderPolicy::ParticleDensity(99)==1,"density bounds");
    Check(WowRenderPolicy::EnvironmentDetail(-99)==.5f && WowRenderPolicy::EnvironmentDetail(99)==1.5f,"detail bounds");
    Check(WowRenderPolicy::ParticleDensity(std::numeric_limits<float>::quiet_NaN())==1,"invalid density default");
    Check(WowRenderPolicy::EnvironmentDetail(std::numeric_limits<float>::infinity())==1,"invalid detail default");
    double A=0,B=0; int LowCount=0,HighCount=0;
    for(int I=0;I<10;++I) {
        LowCount+=WowRenderPolicy::EmitCount(A,100,.1f,.1f,128-LowCount);
        HighCount+=WowRenderPolicy::EmitCount(B,100,.1f,1,128-HighCount);
    }
    Check(LowCount==10 && HighCount==100,"density changes real emission counts tenfold");
    Check(WowRenderPolicy::EmitCount(B,1000,.1f,1,128)==32,"per-frame spawn budget");
    Check(WowRenderPolicy::EmitCount(B,1000,.1f,1,3)==3,"per-emitter capacity");
    Check(WowRenderPolicy::EmitCount(B,1000,.1f,1,-5)==0,"full emitter cannot emit negative count");
    A=0; int Fractional=0;
    for(int I=0;I<100;++I) Fractional+=WowRenderPolicy::EmitCount(A,1,.1f,.1f,128);
    Check(Fractional==1,"low rate retains fractional emissions");
    std::cout<<"PASS: "<<Checks<<" graphics policy checks\n";
}
