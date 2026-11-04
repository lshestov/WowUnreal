#include "WowPlacedFrameLayout.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <initializer_list>

static void Check(bool Pass,const char* Message)
{
    if(!Pass) {std::fprintf(stderr,"FAIL: %s\n",Message); std::exit(1);}
}
int main()
{
    using namespace WowPlacedFrameLayout;
    // Actual 1024x768 fixture: a 160x80 frame dragged from 80,60 to 140,100.
    const auto Drag=Nearest(140,100,160,80,1024,768,1);
    Check(Drag.Point==0 && Drag.X==140 && Drag.Y==-100,"top-left drag has exact offsets");
    const auto Right=Nearest(804,100,160,80,1024,768,1);
    Check(Right.Point==2 && Right.X==-60 && Right.Y==-100,"nearest top-right preserves right inset");
    const auto Center=Nearest(432,344,160,80,1024,768,1);
    Check(Center.Point==4 && Center.X==0 && Center.Y==0,"center wins over corners");
    Check(Nearest(0,0,1024,768,1024,768,1).Point==0,"equal distances keep original FRAMEPOINT order");
    const auto Scaled=Nearest(40,60,120,60,1024,768,.5);
    Check(Scaled.Point==0 && Scaled.X==80 && Scaled.Y==-120,"offsets use full inherited frame scale");
    Check(Round(2.5)==2 && Round(3.5)==4 && Round(-2.5)==-2 && Round(-3.5)==-4,"disk coordinates round as 12340 lrintf");
    for(const char* V:{"false","False","nil","no","0","off","oFf","disabled","DiSaBlEd"}) Check(!StringBoolean(V),"legacy false string conversion");
    for(const char* V:{"true","yes","1","2","enabled","arbitrary",""}) Check(StringBoolean(V),"legacy default=true string conversion");
    // Independent geometric oracle and round trip for arbitrary scaled windows.
    std::mt19937 Rng(12340);
    std::uniform_real_distribution<double> Coord(-300,1500),Size(10,700),Scale(.25,2);
    for(int I=0;I<20000;++I)
    {
        const double X=Coord(Rng),Y=Coord(Rng),W=Size(Rng),H=Size(Rng),S=Scale(Rng);
        const auto A=Nearest(X,Y,W,H,1024,768,S);
        const double FX=(A.Point%3)*.5,FY=(A.Point/3)*.5;
        Check(std::abs((1024-W)*FX+A.X*S-X)<1e-9 && std::abs((768-H)*FY-A.Y*S-Y)<1e-9,"save/restore retains physical rectangle");
        const double Best=std::hypot(A.X*S,A.Y*S);
        for(int P=0;P<9;++P) Check(Best<=std::hypot(X+W*(P%3)*.5-1024*(P%3)*.5,Y+H*(P/3)*.5-768*(P/3)*.5)+1e-9,"chosen corresponding anchor has least screen distance");
        Check(std::abs((1024-W)*FX+Round(A.X)*S-X)<=S*.500001,"integer layout offset loses at most half a local unit");
    }
    // Width-only resolution change: a right-anchored 160x80 window stays 60
    // UI units from the edge, moving by 341 1/3 at 1280x720 (UI width 1365 1/3).
    const double ResizedX=1280.0*768/720-160+Right.X;
    Check(std::abs(ResizedX-1145.33333333333)<1e-8,"resolution change retains edge-relative placement");
    std::puts("PASS: exact coordinates, 9-anchor selection, scale, 20,000 round trips, resolution, rounding and legacy booleans");
}
