#include "WowUiMapRenderer.h"
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
void FWowUiMapRenderer::CurrentZone()
{
    if(!Context.ConnectionManager) return;
    const auto PositionNow=Position(); const uint32 MapId=Context.ConnectionManager->PacketHandler.CurrentMapId;
    const auto& Areas=FDbcStore::Get().WorldMapAreas(); double Smallest=DBL_MAX; AreaIndex=INDEX_NONE;
    for(int32 I=0; I<Areas.Num(); ++I)
    {
        const auto& Area=Areas[I]; const auto P=Area.Project(PositionNow);
        const double Size=FMath::Abs((Area.X1-Area.X2)*(Area.Y1-Area.Y2));
        if(Area.MapId==MapId && P.X>=0 && P.X<=1 && P.Y>=0 && P.Y<=1 && Size<Smallest) { Smallest=Size; AreaIndex=I; }
    }
}
void FWowUiMapRenderer::CreateMap()
{
    if(MapHandle>=0) return;
    FWowFrameDef Def; Def.Name=TEXT("IdleWowWorldMapFrame"); Def.Parent=TEXT("UIParent"); Def.Width=1042; Def.Height=724;
    Def.bHidden=true; Def.Strata=EWowFrameStrata::FULLSCREEN; Def.bEnableMouse=true;
    FWowAnchor Center; Center.RelativeTo=Def.Parent; Def.Anchors.Add(Center);
    FWowBackdrop Backdrop; Backdrop.BgFile=TEXT("Interface\\DialogFrame\\UI-DialogBox-Background"); Def.Backdrop=Backdrop;
    FWowLayer Layer; FWowFontStringElement Title; Title.Name=TEXT("IdleWowMapTitle"); Title.Text=TEXT("Map"); Title.Width=800; Title.Height=24; Title.FontHeight=16;
    FWowAnchor TitleAnchor; TitleAnchor.Point=TitleAnchor.RelativePoint=EWowAnchorPoint::TOP; TitleAnchor.OffsetY=-5; Title.Anchors.Add(TitleAnchor); Layer.FontStrings.Add(Title); Def.Layers.Add(Layer);
    MapHandle=Frames.CreateFrame(Def);
    FWowFrameDef Art; Art.Name=TEXT("IdleWowWorldMapArt"); Art.Parent=Def.Name; Art.Width=1002; Art.Height=668; Art.bEnableMouse=true;
    FWowAnchor A; A.Point=A.RelativePoint=EWowAnchorPoint::TOPLEFT; A.OffsetX=20; A.OffsetY=-32; Art.Anchors.Add(A);
    Art.Scripts.Add({TEXT("OnMouseUp"),TEXT("IdleWowMapCommand(4)"),TEXT("")});
    const auto ArtHandle=Frames.CreateFrame(Art); MapImage=ImageFor(Frames,ArtHandle,1); MapTarget.Reset(Target(1024,768));
    if(MapImage.IsValid()) { FSlateBrush Brush; Brush.DrawAs=ESlateBrushDrawType::Image; Brush.SetResourceObject(MapTarget.Get()); Brush.SetUVRegion(FBox2D(FVector2D(0,0),FVector2D(1002.f/1024,668.f/768))); MapImage->SetBrush(Brush); }
    const TCHAR* Names[]={TEXT("IdleWowMapParent"),TEXT("IdleWowMapCurrent"),TEXT("IdleWowMapClose")};
    const TCHAR* Labels[]={TEXT("Continent"),TEXT("Current zone"),TEXT("Close")};
    for(int32 I=0; I<3; ++I)
    {
        FWowFrameDef Button; Button.Name=Names[I]; Button.Parent=Def.Name; Button.Type=EWowFrameType::Button; Button.Inherits=TEXT("UIPanelButtonTemplate"); Button.Width=130; Button.Height=22; Button.ButtonText=Labels[I]; Button.FrameLevel=10;
        FWowAnchor B; B.Point=B.RelativePoint=EWowAnchorPoint::BOTTOMLEFT; B.OffsetX=20+I*145; B.OffsetY=2; Button.Anchors.Add(B);
        Button.Scripts.Add({TEXT("OnClick"),FString::Printf(TEXT("IdleWowMapCommand(%d)"),I+1),TEXT("")}); Frames.CreateFrame(Button);
    }
}
void FWowUiMapRenderer::Command(int32 Cmd)
{
    CreateMap(); const auto& Areas=FDbcStore::Get().WorldMapAreas();
    if(Cmd==0) { const bool Visible=Frames.IsFrameHierarchyShown(MapHandle); if(!Visible) CurrentZone(); Frames.SetFrameVisible(MapHandle,!Visible); }
    else if(Cmd==3) Frames.SetFrameVisible(MapHandle,false);
    else if(Cmd==2) CurrentZone();
    else if(Cmd==1 && Areas.IsValidIndex(AreaIndex))
    {
        const uint32 MapId=Areas[AreaIndex].MapId;
        for(int32 I=0; I<Areas.Num(); ++I) if(Areas[I].MapId==MapId && Areas[I].AreaId==0) { AreaIndex=I; break; }
    }
    else if(Cmd==4 && Areas.IsValidIndex(AreaIndex))
    {
        const auto& Area=Areas[AreaIndex]; const auto R=Frames.GetLayoutRect(Frames.FindFrame(TEXT("IdleWowWorldMapArt")));
        const auto P=Frames.GetPointerPosition()/Frames.GetScreenScale();
        const FVector World(Area.X1-(P.Y-R.Y)/R.W*(Area.X1-Area.X2),Area.Y1-(P.X-R.X)/R.Z*(Area.Y1-Area.Y2),0);
        for(int32 I=0; I<Areas.Num(); ++I) if(Areas[I].MapId==Area.MapId && Areas[I].AreaId)
        { const auto Q=Areas[I].Project(World); if(Q.X>=0 && Q.X<=1 && Q.Y>=0 && Q.Y<=1) { AreaIndex=I; break; } }
    }
    NextDraw=0;
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
    if(MiniImage.IsValid() && MiniTarget.IsValid() && Frames.IsFrameDrawn(Frames.FindFrame(TEXT("Minimap"))) && Map)
    {
        UKismetRenderingLibrary::ClearRenderTarget2D(World,MiniTarget.Get(),FLinearColor::Black);
        UCanvas* Canvas=nullptr; FVector2D Size; FDrawToRenderTargetContext Draw;
        UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World,MiniTarget.Get(),Canvas,Size,Draw);
        if(Canvas)
        {
            const float X=32-Pos.Y/(1600.f/3),Y=32-Pos.X/(1600.f/3);
            const float TileSize=256.f*FMath::Pow(1.5f,Frames.MinimapZoom-2);
            for(int32 DY=-1; DY<=1; ++DY) for(int32 DX=-1; DX<=1; ++DX)
            {
                const int32 TX=FMath::FloorToInt(X)+DX,TY=FMath::FloorToInt(Y)+DY;
                const FString Key=FString::Printf(TEXT("%s\\map%d_%d.blp"),*Map->InternalName,TX,TY).ToLower();
                if(const auto* Hash=TileNames.Find(Key)) if(auto* Tile=Texture(TEXT("Textures\\Minimap\\")+*Hash)) Canvas->K2_DrawTexture(Tile,FVector2D(128+(TX-X)*TileSize,128+(TY-Y)*TileSize),FVector2D(TileSize),FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor::White,BLEND_Opaque);
            }
            Arrow(Canvas,FVector2D(128),-Facing,8);
        }
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World,Draw);
    }
    const auto& Areas=FDbcStore::Get().WorldMapAreas();
    if(MapTarget.IsValid() && Frames.IsFrameDrawn(MapHandle) && Areas.IsValidIndex(AreaIndex))
    {
        const auto& Area=Areas[AreaIndex];
        if(auto* Title=Frames.GetNamedFontString(TEXT("IdleWowMapTitle"))) { const auto* Named=FDbcStore::Get().AreaTable().GetById(Area.AreaId); Title->SetText(FText::FromString(Named?Named->Name:Map?Map->Name:Area.Name)); }
        UKismetRenderingLibrary::ClearRenderTarget2D(World,MapTarget.Get(),FLinearColor::Black);
        UCanvas* Canvas=nullptr; FVector2D Size; FDrawToRenderTargetContext Draw;
        UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World,MapTarget.Get(),Canvas,Size,Draw);
        if(Canvas)
        {
            for(int32 I=0; I<12; ++I) if(auto* Tile=Texture(FString::Printf(TEXT("Interface\\WorldMap\\%s\\%s%d.blp"),*Area.Name,*Area.Name,I+1))) Canvas->K2_DrawTexture(Tile,FVector2D((I%4)*256,(I/4)*256),FVector2D(256),FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor::White,BLEND_Opaque);
            for(const auto& Overlay:FDbcStore::Get().WorldMapOverlays()) if(Overlay.MapAreaId==Area.Id)
            {
                bool Explored=false;
                for(uint32 Id:Overlay.Areas) if(const auto* A=FDbcStore::Get().AreaTable().GetById(Id); A && A->ExploreFlag<4096)
                    Explored|=(Player->GetField(PlayerField::BASE+0x037D+A->ExploreFlag/32)&(1u<<(A->ExploreFlag%32)))!=0;
                if(!Explored) continue;
                const int32 Columns=FMath::DivideAndRoundUp(Overlay.Width,256),Rows=FMath::DivideAndRoundUp(Overlay.Height,256);
                for(int32 Y=0; Y<Rows; ++Y) for(int32 X=0; X<Columns; ++X)
                    if(auto* Tile=Texture(FString::Printf(TEXT("Interface\\WorldMap\\%s\\%s%d.blp"),*Area.Name,*Overlay.Name,Y*Columns+X+1)))
                        Canvas->K2_DrawTexture(Tile,FVector2D(Overlay.X+X*256,Overlay.Y+Y*256),FVector2D(Tile->GetSizeX(),Tile->GetSizeY()),FVector2D::ZeroVector,FVector2D::UnitVector,FLinearColor::White,BLEND_Translucent);
            }
            const auto P=Area.Project(Pos); if(Area.MapId==MapId && P.X>=0 && P.X<=1 && P.Y>=0 && P.Y<=1) Arrow(Canvas,P*FVector2D(1002,668),-Facing,10);
        }
        UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World,Draw);
    }
}
