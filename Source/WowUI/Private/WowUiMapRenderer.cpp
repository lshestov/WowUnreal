#include "WowUiMapRenderer.h"
#include "WowMapUi.h"
#include "WowMinimapUi.h"
#include "WowFrameManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
UImage* ImageFor(FWowFrameManager& Frames,int64 Handle,int32 Layer)
{
    auto* Canvas=Cast<UCanvasPanel>(Frames.GetWidgetForHandle(Handle)); if(!Canvas) return nullptr;
    auto* Image=NewObject<UImage>(Canvas); Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(auto* Slot=Canvas->AddChildToCanvas(Image)) { Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(Layer); }
    return Image;
}
UTextureRenderTarget2D* Target(int32 Width,int32 Height)
{
    auto* Result=NewObject<UTextureRenderTarget2D>(); Result->RenderTargetFormat=RTF_RGBA8;
    Result->ClearColor=FLinearColor(.025f,.025f,.025f,1); Result->InitAutoFormat(Width,Height); Result->UpdateResourceImmediate(true); return Result;
}
void Arrow(UCanvas* Canvas,FVector2D Point,float Angle,float Size)
{
    const FVector2D Forward(FMath::Sin(Angle),-FMath::Cos(Angle)),Right(-Forward.Y,Forward.X);
    Canvas->K2_DrawLine(Point+Forward*Size,Point-Forward*Size*.7f+Right*Size*.6f,2,FLinearColor(1,.9f,.2f));
    Canvas->K2_DrawLine(Point+Forward*Size,Point-Forward*Size*.7f-Right*Size*.6f,2,FLinearColor(1,.9f,.2f));
    Canvas->K2_DrawLine(Point-Forward*Size*.7f+Right*Size*.6f,Point-Forward*Size*.7f-Right*Size*.6f,2,FLinearColor(1,.9f,.2f));
}
}
FWowUiMapRenderer::FWowUiMapRenderer(FWowFrameManager& InFrames,FWowLuaContext& InContext):Frames(InFrames),Context(InContext) {}

FVector FWowUiMapRenderer::Position() const
{
    if(Context.EntityManager)
    {
        if(Context.ResolveActor) if(auto* Actor=Context.ResolveActor(Context.EntityManager->LocalPlayerGuid)) return FWowCoordinate::UEToWow(Actor->GetActorLocation());
        if(const auto* Player=Context.EntityManager->GetLocalPlayer()) return Player->Movement.Position;
    }
    return FVector::ZeroVector;
}
UTexture2D* FWowUiMapRenderer::Texture(const FString& Path)
{
    if(auto* Found=Tiles.Find(Path)) return Found->Get();
    Pending.AddUnique(Path); return nullptr;
}
void FWowUiMapRenderer::Tick(UWorld* World)
{
    if(!World || !Context.ConnectionManager || !Context.EntityManager || !Context.EntityManager->GetLocalPlayer()) return;
    if(!bLoadedIndex)
    {
        bLoadedIndex=true; TArray<uint8> Bytes;
        if(auto* Mpq=Frames.GetMpqManager(); Mpq && Mpq->ReadFile(TEXT("Textures\\Minimap\\md5translate.trs"),Bytes))
        {
            Bytes.Add(0); TArray<FString> Lines; FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()))).ParseIntoArrayLines(Lines);
            for(const auto& Line:Lines) { FString Key,Value; if(Line.Split(TEXT("\t"),&Key,&Value)) TileNames.Add(Key.ToLower(),Value.TrimStartAndEnd()); }
        }
    }
    if(!MiniImage.IsValid()) if(const auto Handle=Frames.FindFrame(TEXT("Minimap")); Handle>=0)
    {
        MiniImage=ImageFor(Frames,Handle,-100); MiniTarget.Reset(Target(256,256));
        if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiImage.M_WowUiImage")))
        {
            MiniMaterial.Reset(UMaterialInstanceDynamic::Create(Base,GetTransientPackage())); MiniMaterial->SetTextureParameterValue(TEXT("Image"),MiniTarget.Get()); MiniMaterial->SetScalarParameterValue(TEXT("Circle"),1);
            FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.SetResourceObject(MiniMaterial.Get()); if(MiniImage.IsValid()) MiniImage->SetBrush(Brush);
        }
    }
    // At most one MPQ texture is decoded/uploaded per frame. Missing art is cached too.
    if(Pending.Num())
    {
        const FString Path=Pending[0]; Pending.RemoveAt(0);
        if(Tiles.Num()>=48) { Tiles.Reset(); Pending.Reset(); }
        Tiles.Add(Path,TStrongObjectPtr<UTexture2D>(Frames.LoadUITexture(Path))); NextDraw=0;
    }
    const double Now=FPlatformTime::Seconds(); if(Now<NextDraw) return; NextDraw=Now+.1;
    const FVector Pos=Position(); const uint32 MapId=Context.ConnectionManager->PacketHandler.CurrentMapId;
    const auto* Map=FDbcStore::Get().Maps().GetById(MapId);
    const auto* Player=Context.EntityManager->GetLocalPlayer(); const float Facing=Player->Movement.Orientation;
    auto& Mini=WowMinimapUi::View(&Context);
    if(Mini.OwnsTooltip && !Frames.IsFrameDrawn(Frames.FindFrame(TEXT("Minimap"))))
    {const int64 T=Frames.FindFrame(TEXT("GameTooltip")); const auto* S=Frames.GetTooltipState(T); if(S && S->OwnerHandle==Frames.FindFrame(TEXT("Minimap"))) Frames.SetFrameVisible(T,false); Mini.OwnsTooltip=false;}
    if(MiniImage.IsValid() && MiniTarget.IsValid() && Frames.IsFrameDrawn(Frames.FindFrame(TEXT("Minimap"))) && Map)
    {
        UKismetRenderingLibrary::ClearRenderTarget2D(World,MiniTarget.Get(),FLinearColor::Black);
        UCanvas* Canvas=nullptr; FVector2D Size; FDrawToRenderTargetContext Draw;
        UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World,MiniTarget.Get(),Canvas,Size,Draw);
        if(Canvas)
        {
            const float X=32-Pos.Y/(1600.f/3),Y=32-Pos.X/(1600.f/3);
            const float Radius=WowMinimapUi::Radius(&Context);
            const float TileSize=256.f*(1600.f/3)/(2*Radius);
            const float Angle=Mini.Rotates?Facing:0;
            const auto Rotate=[&](FVector2D P){P-=FVector2D(128); return FVector2D(128)+FVector2D(P.X*FMath::Cos(Angle)-P.Y*FMath::Sin(Angle),P.X*FMath::Sin(Angle)+P.Y*FMath::Cos(Angle));};
            for(int32 DY=-1; DY<=1; ++DY) for(int32 DX=-1; DX<=1; ++DX)
            {
                const int32 TX=FMath::FloorToInt(X)+DX,TY=FMath::FloorToInt(Y)+DY;
                const FString Key=FString::Printf(TEXT("%s\\map%d_%d.blp"),*Map->InternalName,TX,TY).ToLower();
                if(const auto* Hash=TileNames.Find(Key)) if(auto* Tile=Texture(TEXT("Textures\\Minimap\\")+*Hash)) Canvas->K2_DrawTexture(Tile,Rotate(FVector2D(128+(TX-X)*TileSize,128+(TY-Y)*TileSize)),FVector2D(TileSize),FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor::White,BLEND_Opaque,FMath::RadiansToDegrees(Angle),FVector2D::ZeroVector);
            }
            const auto Handle=Frames.FindFrame(TEXT("Minimap")); const auto Rect=Frames.GetLayoutRect(Handle); const auto Blips=WowMinimapUi::Blips(&Context);
            const uint32 MarkerHash=HashCombineFast(Context.ConnectionManager->PacketHandler.QuestGiverStatusRevision,Mini.TrackingHash);
            if(MarkerHash!=Mini.MarkerLogHash)
            {
                Mini.MarkerLogHash=MarkerHash;
                UE_LOG(LogWowLuaFrame,Log,TEXT("Minimap: tracking=%s nearby=%d questgivers=%d radius=%.2f"),*Mini.CommonKey,Blips.Num(),Context.ConnectionManager->PacketHandler.QuestGiverStatuses.Num(),Radius);
            }
            const FString* Hover=nullptr; const FVector2D Pointer=Frames.GetPointerPosition()/FMath::Max(.001f,Frames.GetScreenScale());
            if(auto* Icons=Texture(Mini.BlipTexture)) for(const auto& B:Blips)
            {
                const auto P=WowMinimapUi::Offset(&Context,Pos,B.Position,Radius,Facing);
                // ObjectIcons uses the 8x2 object/class UV table; the separate
                // 8x4 table in build 12340 belongs to the vehicle atlas.
                Canvas->K2_DrawTexture(Icons,FVector2D(128)+P*256-FVector2D(8),FVector2D(16),FVector2D((B.Icon&7)/8.,(B.Icon>>3)/2.),FVector2D(1./8,1./2),FLinearColor::White);
                const FVector2D Screen(Rect.X+Rect.Z*(.5+P.X),Rect.Y+Rect.W*(.5+P.Y));
                if(Frames.IsMouseOverFrame(Handle) && FVector2D::DistSquared(Screen,Pointer)<=FMath::Square(Rect.Z/256*10)) Hover=&B.Name;
            }
            const int64 Tooltip=Frames.FindFrame(TEXT("GameTooltip"));
            if(Hover && Tooltip>=0) {Frames.TooltipSetOwner(Tooltip,Handle,TEXT("ANCHOR_CURSOR")); Frames.TooltipSetText(Tooltip,*Hover); Frames.SetFrameVisible(Tooltip,true); Mini.OwnsTooltip=true;}
            else if(Mini.OwnsTooltip) {const auto* State=Frames.GetTooltipState(Tooltip); if(State && State->OwnerHandle==Handle) Frames.SetFrameVisible(Tooltip,false); Mini.OwnsTooltip=false;}
            const auto ArrowSize=Mini.PlayerSize*256/FVector2D(FMath::Max(1.,Rect.Z),FMath::Max(1.,Rect.W))*Frames.GetEffectiveFrameScale(Handle);
            if(auto* Art=Texture(Mini.PlayerTexture)) Canvas->K2_DrawTexture(Art,FVector2D(128)-ArrowSize/2,ArrowSize,FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor::White,BLEND_Translucent,Mini.Rotates?0:-FMath::RadiansToDegrees(Facing),FVector2D(.5));
            else Arrow(Canvas,FVector2D(128),Mini.Rotates?0:-Facing,8);
            if(auto* Compass=Frames.GetNamedTexture(TEXT("MinimapCompassTexture"))) Compass->SetRenderTransformAngle(Mini.Rotates?FMath::RadiansToDegrees(Facing):0);
        }
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World,Draw);
    }
    DrawQuestBlobs(World);
}
void FWowUiMapRenderer::DrawQuestBlobs(UWorld* World)
{
    if(!Context.Map) return;
    int32 Rendered=0;
    for(const auto& Pair:Context.Map->Blobs)
    {
        const int64 Handle=Pair.Key; if(!Frames.IsFrameDrawn(Handle) || ++Rendered>8) continue;
        auto& Image=BlobImages.FindOrAdd(Handle); auto& RenderTarget=BlobTargets.FindOrAdd(Handle);
        if(!Image.IsValid()) {Image=ImageFor(Frames,Handle,-10); if(!Image.IsValid()) continue;}
        if(!RenderTarget.IsValid()) {RenderTarget.Reset(Target(1024,768)); FSlateBrush B; B.DrawAs=ESlateBrushDrawType::Image; B.SetResourceObject(RenderTarget.Get()); B.SetUVRegion(FBox2D(FVector2D::ZeroVector,FVector2D(1002./1024,668./768))); Image->SetBrush(B);}
        const auto& Style=Pair.Value; const auto Shapes=WowMapUi::QuestShapes(&Context,Style);
        UKismetRenderingLibrary::ClearRenderTarget2D(World,RenderTarget.Get(),FLinearColor::Transparent);
        if(Shapes.IsEmpty()) continue;
        auto* Fill=Texture(Style.FillTexture); auto* Border=Texture(Style.BorderTexture);
        UCanvas* Canvas=nullptr; FVector2D Size; FDrawToRenderTargetContext Draw;
        UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World,RenderTarget.Get(),Canvas,Size,Draw);
        if(Canvas) for(const auto& Shape:Shapes)
        {
            if(Shape.Points.Num()<3) continue; const auto Pixels=[](const FVector2D& P){return P*FVector2D(1002,668);};
            TArray<FCanvasUVTri> Triangles; const FLinearColor Color(1,1,1,Style.FillAlpha/255);
            for(int32 I=1;I<Shape.Points.Num()-1;++I)
            {
                FCanvasUVTri T; T.V0_Pos=Pixels(Shape.Points[0]); T.V1_Pos=Pixels(Shape.Points[I]); T.V2_Pos=Pixels(Shape.Points[I+1]);
                T.V0_UV=T.V0_Pos/64; T.V1_UV=T.V1_Pos/64; T.V2_UV=T.V2_Pos/64; T.V0_Color=T.V1_Color=T.V2_Color=Color; Triangles.Add(T);
            }
            if(Fill) Canvas->K2_DrawTriangle(Fill,Triangles);
            for(int32 I=0;I<Shape.Points.Num();++I)
            {
                const auto A=Pixels(Shape.Points[I]),B=Pixels(Shape.Points[(I+1)%Shape.Points.Num()]),D=B-A;
                if(Border) Canvas->K2_DrawTexture(Border,A,FVector2D(D.Length(),8*Style.BorderScale),FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor(1,1,1,Style.BorderAlpha/255),BLEND_Translucent,FMath::RadiansToDegrees(FMath::Atan2(D.Y,D.X)),FVector2D(0,.5));
            }
        }
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World,Draw);
    }
}
