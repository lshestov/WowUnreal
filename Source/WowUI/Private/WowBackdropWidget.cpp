#include "WowBackdropWidget.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElementTypes.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SLeafWidget.h"

TArray<FWowBackdropQuad> UWowBackdropWidget::BuildQuads(const FWowBackdrop& D,FVector2f Size,float Scale)
{
    TArray<FWowBackdropQuad> Out;
    if(!FMath::IsFinite(Scale) || Scale<=0 || Size.X<=0 || Size.Y<=0) return Out;
    auto Add=[&](float X,float Y,float W,float H,float U0,float V0,float U1,float V1,bool Border,bool Rotate=false)
    {
        if(W<=0 || H<=0) return;
        FWowBackdropQuad Q; Q.Rect=FVector4f(X,Y,W,H); Q.Border=Border;
        if(Rotate) { Q.UV[0]={U0,V1}; Q.UV[1]={U0,V0}; Q.UV[2]={U1,V1}; Q.UV[3]={U1,V0}; }
        else { Q.UV[0]={U0,V0}; Q.UV[1]={U1,V0}; Q.UV[2]={U0,V1}; Q.UV[3]={U1,V1}; }
        Out.Add(Q);
    };
    const float Edge=FMath::Min3(float(D.EdgeSize>0?D.EdgeSize:39)*Scale,Size.X*.5f,Size.Y*.5f);
    if(!D.BgFile.IsEmpty())
    {
        const float X=D.InsetLeft*Scale,Y=D.InsetTop*Scale;
        const float W=FMath::Max(0.f,Size.X-X-D.InsetRight*Scale),H=FMath::Max(0.f,Size.Y-Y-D.InsetBottom*Scale);
        // Bound geometry for addon-supplied tiny tile sizes without allocating
        // a native widget for each repeated patch.
        const float Tile=FMath::Max3(float(D.TileSize>0?D.TileSize:D.EdgeSize>0?D.EdgeSize:32)*Scale,W/64,H/64);
        if(D.Tile) for(float V=0;V<H;V+=Tile) for(float U=0;U<W;U+=Tile)
            Add(X+U,Y+V,FMath::Min(Tile,W-U),FMath::Min(Tile,H-V),0,0,FMath::Min(1.f,(W-U)/Tile),FMath::Min(1.f,(H-V)/Tile),false);
        else Add(X,Y,W,H,0,0,1,1,false);
    }
    if(D.EdgeFile.IsEmpty() || Edge<=0) return Out;
    // Original border atlas: left, right, top, bottom, then the four corners.
    // Insets avoid sampling neighboring atlas strips under linear filtering.
    constexpr float PadU=1.f/128,PadV=1.f/16;
    Add(0,0,Edge,Edge,.5f+PadU,PadV,.625f-PadU,1-PadV,true);
    Add(Size.X-Edge,0,Edge,Edge,.625f+PadU,PadV,.75f-PadU,1-PadV,true);
    Add(0,Size.Y-Edge,Edge,Edge,.75f+PadU,PadV,.875f-PadU,1-PadV,true);
    Add(Size.X-Edge,Size.Y-Edge,Edge,Edge,.875f+PadU,PadV,1-PadU,1-PadV,true);
    for(int32 Side=0;Side<4;++Side)
    {
        const bool Horizontal=Side>=2;
        const float Length=FMath::Max(0.f,(Horizontal?Size.X:Size.Y)-2*Edge);
        const float Tile=FMath::Max(Edge,Length/64);
        for(float T=0;T<Length;T+=Tile)
        {
            const float Span=FMath::Min(Tile,Length-T),End=PadV+(1-2*PadV)*Span/Tile;
            Add(Horizontal?Edge+T:Side==0?0:Size.X-Edge,Horizontal?(Side==2?0:Size.Y-Edge):Edge+T,
                Horizontal?Span:Edge,Horizontal?Edge:Span,Side*.125f+PadU,PadV,(Side+1)*.125f-PadU,End,true,Horizontal);
        }
    }
    return Out;
}

class SWowBackdrop : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowBackdrop) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    FWowBackdrop Definition;
    float Scale=1;
    FSlateBrush BackgroundBrush,BorderBrush;
    FLinearColor BackgroundColor=FLinearColor::White,BorderColor=FLinearColor::White;
    mutable FVector2f CachedSize=FVector2f(-1,-1);
    mutable TArray<FWowBackdropQuad> Quads;
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
    {
        const FVector2f Size(G.GetLocalSize());
        if(Size!=CachedSize) { CachedSize=Size; Quads=UWowBackdropWidget::BuildQuads(Definition,Size,Scale); }
        for(int32 Pass=0;Pass<2;++Pass)
        {
            const auto& Brush=Pass?BorderBrush:BackgroundBrush;
            if(!Brush.GetResourceObject()) continue;
            TArray<FSlateVertex> Vertices; TArray<SlateIndex> Indices;
            const FColor Color=((Pass?BorderColor:BackgroundColor)*Style.GetColorAndOpacityTint()).ToFColor(true);
            for(const auto& Q:Quads) if(Q.Border==(Pass!=0))
            {
                const SlateIndex Base=Vertices.Num();
                const FVector2f Positions[]={{Q.Rect.X,Q.Rect.Y},{Q.Rect.X+Q.Rect.Z,Q.Rect.Y},{Q.Rect.X,Q.Rect.Y+Q.Rect.W},{Q.Rect.X+Q.Rect.Z,Q.Rect.Y+Q.Rect.W}};
                for(int32 I=0;I<4;++I) Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),Positions[I],Q.UV[I],Color));
                for(const int32 I:{0,1,2,2,1,3}) Indices.Add(Base+I);
            }
            if(Vertices.Num()) FSlateDrawElement::MakeCustomVerts(Elements,Layer+Pass,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush),Vertices,Indices,nullptr,0,0,
                Enabled?ESlateDrawEffect::None:ESlateDrawEffect::DisabledEffect);
        }
        return Layer+1;
    }
};

void UWowBackdropWidget::Configure(const FWowBackdrop& Value,UTexture2D* Background,UTexture2D* Edge,float Scale)
{
    Definition=Value; BackgroundTexture=Background; EdgeTexture=Edge; UnitScale=Scale;
    BackgroundColor=Value.Color; BorderColor=Value.BorderColor; SynchronizePainter();
}
void UWowBackdropWidget::SetUnitScale(float Scale) { if(UnitScale!=Scale) { UnitScale=Scale; SynchronizePainter(); } }
void UWowBackdropWidget::SetColors(const FLinearColor& Background,const FLinearColor& Border) { BackgroundColor=Background; BorderColor=Border; SynchronizePainter(); }
void UWowBackdropWidget::SynchronizePainter()
{
    if(!Painter.IsValid()) return;
    Painter->Definition=Definition; Painter->Scale=UnitScale; Painter->BackgroundColor=BackgroundColor; Painter->BorderColor=BorderColor;
    Painter->BackgroundBrush.SetResourceObject(BackgroundTexture); Painter->BorderBrush.SetResourceObject(EdgeTexture);
    Painter->CachedSize=FVector2f(-1,-1); Painter->Invalidate(EInvalidateWidgetReason::Paint);
}
TSharedRef<SWidget> UWowBackdropWidget::RebuildWidget() { Painter=SNew(SWowBackdrop); SynchronizePainter(); return Painter.ToSharedRef(); }
void UWowBackdropWidget::ReleaseSlateResources(bool ReleaseChildren) { Super::ReleaseSlateResources(ReleaseChildren); Painter.Reset(); }
