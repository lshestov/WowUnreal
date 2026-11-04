#pragma once

// Deliberately independent of Unreal so the client coordinate rules can be
// checked without a renderer. Point indices follow 12340's FRAMEPOINT order.
#include <cmath>
#include <limits>
#include <cctype>

namespace WowPlacedFrameLayout
{
struct Anchor { int Point=0; double X=0,Y=0; };

inline Anchor Nearest(double X,double Y,double Width,double Height,double ScreenWidth,double ScreenHeight,double Scale)
{
    Anchor Result;
    double Best=std::numeric_limits<double>::max();
    for(int Point=0;Point<9;++Point)
    {
        const double DX=X+(Width-ScreenWidth)*(Point%3)*.5;
        const double DY=Y+(Height-ScreenHeight)*(Point/3)*.5;
        const double Distance=DX*DX+DY*DY;
        if(Distance<Best) { Best=Distance; Result={Point,DX/Scale,-DY/Scale}; }
    }
    return Result;
}

// CMath::fint_ in 12340 uses round-to-nearest (ties to even), rather than
// truncation or Unreal's asymmetric RoundToInt for negative half values.
inline int Round(double Value) { return static_cast<int>(std::nearbyint(Value)); }

inline bool StringBoolean(const char* Value)
{
    switch(*Value)
    {
        case '0': case 'F': case 'N': case 'f': case 'n': return false;
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        case 'T': case 'Y': case 't': case 'y': return true;
    }
    // The caller uses StringToBOOL's defaultValue=1.
    const auto Equals=[&](const char* Other)
    {
        const char* P=Value;
        while(*P && *Other && std::tolower(static_cast<unsigned char>(*P))==*Other) {++P; ++Other;}
        return *P==0 && *Other==0;
    };
    return !Equals("off") && !Equals("disabled");
}
}
