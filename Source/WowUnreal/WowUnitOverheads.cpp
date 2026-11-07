#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowDoodadManager.h"
#include "WowFontManager.h"
#include "WowTerrainMaterial.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowUIManager.h"
#include "Coord/WowCoordinate.h"
#include "Formats/M2Types.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

namespace
{
const TCHAR* QuestMarkerModel(uint8 Status, bool TrackTrivial)
{
    // DIALOG_STATUS values in the build 12340 questgiver-status response.
    switch (Status)
    {
    case 1: return TEXT("TalkToMeGrey");
    case 2: return TrackTrivial ? TEXT("TalkToMe") : nullptr;
    case 3: return TrackTrivial ? TEXT("TalkToMeQuestion_LTBlue") : nullptr;
    case 4: return TrackTrivial ? TEXT("TalkToMeBlue") : nullptr;
    case 5: return TEXT("TalkToMeQuestion_Grey");
    case 6: return TEXT("TalkToMeQuestion_LTBlue");
    case 7: return TEXT("TalkToMeBlue");
    case 8: return TEXT("TalkToMe");
    case 9: case 10: return TEXT("TalkToMeQuestionMark");
    default: return nullptr;
    }
}

FVector OverheadAnchor(const UBoxComponent& Selection)
{
    const auto& Bounds = Selection.Bounds;
    return Bounds.Origin + FVector(0, 0, Bounds.BoxExtent.Z + 9);
}
}

class SWowUnitNameLayer final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowUnitNameLayer) {} SLATE_END_ARGS()
    void Construct(const FArguments&, AWowGameplayController* InController)
    {
        Controller = InController;
        SetVisibility(EVisibility::HitTestInvisible);
        ForceVolatile(true); // World-space anchors move with the camera between UI layout changes.
    }
    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        auto* C = Controller.Get();
        if (!C || !C->GetPawn() || !C->UIManager || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return Layer;
        auto* Fonts = C->UIManager->GetFontManager();
        if (!Fonts || !Fonts->IsInitialized()) return Layer;
        if (!BaseFont.IsSet()) BaseFont = Fonts->GetFont(TEXT("Fonts\\FRIZQT__.TTF"), 15);
        int32 Width, Height; C->GetViewportSize(Width, Height);
        const FVector2D Size = Geometry.GetLocalSize();
        if (Width <= 0 || Height <= 0 || Size.X <= 0 || Size.Y <= 0) return Layer;
        const FVector2D PixelsToLocal(Size.X / Width, Size.Y / Height);
        const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        for (auto& Pair : C->UnitOverheads)
        {
            auto& Info = Pair.Value;
            if (!Info.ShowName || !Info.Selection.IsValid()) continue;
            const FVector Anchor = OverheadAnchor(*Info.Selection.Get());
            FVector2D Pixel;
            if (!C->ProjectWorldLocationToScreen(Anchor, Pixel)) continue;
            const float Distance = FVector::Distance(C->GetPawn()->GetActorLocation(), Anchor) / FWowCoordinate::SCALE;
            FSlateFontInfo Font = BaseFont.GetValue();
            Font.Size = FMath::Max(1.f, FMath::RoundToFloat(Font.Size * FMath::Clamp(1.15f - Distance * .012f, .75f, 1.f) * float(Size.Y / 900.)));
            Font.OutlineSettings.OutlineSize = 1;
            Font.OutlineSettings.OutlineColor = FLinearColor::Black;
            const FVector2D NameSize = Measure->Measure(Info.Name, Font);
            if (NameSize.X <= 0 || NameSize.Y <= 0) continue;
            const FVector2D SubtitleSize = Info.Subtitle.IsEmpty() ? FVector2D::ZeroVector : FVector2D(Measure->Measure(Info.Subtitle, Font));
            const FVector2D Extent(FMath::Max(NameSize.X, SubtitleSize.X), NameSize.Y + SubtitleSize.Y);
            const FVector2D Origin = Pixel * PixelsToLocal - FVector2D(Extent.X * .5, Extent.Y);
            if (Origin.X + Extent.X <= 0 || Origin.Y + Extent.Y <= 0 || Origin.X >= Size.X || Origin.Y >= Size.Y) continue;
            const auto DrawLine = [&](const FText& Text, const FVector2D& LineSize, double OffsetY)
            {
                const FVector2D LineOrigin = Origin + FVector2D((Extent.X - LineSize.X) * .5, OffsetY);
                const auto Paint = Geometry.ToPaintGeometry(FVector2f(LineSize), FSlateLayoutTransform(FVector2f(LineOrigin)));
                FSlateDrawElement::MakeText(Elements, Layer, Paint, Text, Font, ESlateDrawEffect::None, Info.Color);
            };
            DrawLine(Info.Name, NameSize, 0);
            if (!Info.Subtitle.IsEmpty()) DrawLine(Info.Subtitle, SubtitleSize, NameSize.Y);
            Info.LastNamePaint = GFrameCounter;
            Info.NamePixelRect = FVector4(Origin.X / PixelsToLocal.X, Origin.Y / PixelsToLocal.Y, Extent.X / PixelsToLocal.X, Extent.Y / PixelsToLocal.Y);
        }
        return Layer;
    }
private:
    TWeakObjectPtr<AWowGameplayController> Controller;
    mutable TOptional<FSlateFontInfo> BaseFont;
};

void AWowGameplayController::RemoveUnitNameLayer()
{
    if (!UnitNameLayer.IsValid()) return;
    if (auto* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
        Viewport->RemoveViewportWidgetContent(UnitNameLayer.ToSharedRef());
    UnitNameLayer.Reset();
}

void AWowGameplayController::EndPlay(const EEndPlayReason::Type Reason)
{
    RemoveUnitNameLayer();
    Super::EndPlay(Reason);
}

void AWowGameplayController::ApplyUnitScale(AActor* Actor, const FWowEntity& Entity)
{
    if (!Actor || !Entity.IsUnit()) return;
    // The mesh carries the display/model scale. Build 12340 applies the family
    // minimum (or pet size) at the current level before OBJECT_FIELD_SCALE_X.
    // Apply the ratio on the actor so the body, equipment and picking bounds
    // all resize together, without multiplying the family size by display size.
    float Scale = FMath::IsFinite(Entity.Scale) && Entity.Scale > 0 ? Entity.Scale : 1.f;
    if (!Entity.IsPlayer() && ConnectionManager)
    {
        const auto& Dbc = FDbcStore::Get();
        const uint32 DisplayId = Entity.GetDisplayId() ? Entity.GetDisplayId() : Entity.GetNativeDisplayId();
        const auto* Display = Dbc.CreatureDisplayInfo().GetById(DisplayId);
        const auto* Model = Display ? Dbc.CreatureModelData().GetById(Display->ModelID) : nullptr;
        const auto* Family = Dbc.CreatureFamilies().GetById(ConnectionManager->PacketHandler.CreatureFamilies.FindRef(Entity.Entry));
        const float Base = Display && Model ? Display->Scale * Model->Scale : 0.f;
        if (Family && FMath::IsFinite(Base) && Base > 0)
            Scale *= Family->ModelScale(Base, Entity.GetLevel(), Entity.GetField(UnitField::PETNUMBER) != 0) / Base;
    }
    if (Actor->GetActorScale3D().Equals(FVector(Scale), KINDA_SMALL_NUMBER)) return;
    Actor->SetActorScale3D(FVector(Scale));
    NpcReferenceFeet.Remove(Entity.Guid);
    NpcGroundOffsets.Remove(Entity.Guid);
}

void AWowGameplayController::RefreshUnitOverheads()
{
    if (!UIManager || !ConnectionManager || !GetPawn()) return;
    if (!UnitNameLayer.IsValid())
    {
        auto* Viewport = GetWorld()->GetGameViewport();
        if (!Viewport) return;
        UnitNameLayer = SNew(SWowUnitNameLayer, this);
        Viewport->AddViewportWidgetContent(UnitNameLayer.ToSharedRef(), 40); // FrameXML root uses 50.
    }
    const double Now = GetWorld()->GetTimeSeconds();
    FVector Camera; FRotator Rotation;
    GetPlayerViewPoint(Camera, Rotation);
    for (const auto& Pair : UnitOverheads)
    {
        const auto& Info = Pair.Value;
        auto* Marker = Info.Marker.Get();
        if (!Marker || !Marker->IsVisible() || !Info.Selection.IsValid()) continue;
        // All build-12340 TalkToMe models use one upright billboard bone with a vertical Stand track.
        const FVector Anchor = OverheadAnchor(*Info.Selection.Get());
        const float Time = Info.MarkerPeriod > 0 ? FMath::Fmod(float(Now), Info.MarkerPeriod) : 0;
        const int32 NameLines = Info.ShowName ? (Info.Subtitle.IsEmpty() ? 1 : 2) : 0;
        const float Lift = Info.MarkerHeight.Eval(Time, 0.f) + 3.f + 24.f * NameLines;
        Marker->SetWorldLocationAndRotation(Anchor + FVector(0, 0, Lift), FRotator(0, (Camera - Anchor).Rotation().Yaw - 90, 0));
    }
    if (Now < NextOverheadUpdate) return;
    NextOverheadUpdate = Now + .12;
    bool ShowNames = true, TrackTrivial = false;
    UIManager->GetNpcOverheadSettings(ShowNames, TrackTrivial);
    const auto& Handler = ConnectionManager->PacketHandler;
    int32 Width, Height; GetViewportSize(Width, Height);
    for (const auto& Pair : SpawnedEntityActors)
    {
        AActor* Actor = Pair.Value;
        const auto* Entity = Handler.EntityManager.Find(Pair.Key);
        if (!IsValid(Actor) || !Entity || !Entity->IsUnit() || Entity->IsPlayer()) continue;
        auto& Info = UnitOverheads.FindOrAdd(Pair.Key);
        if (!Info.Selection.IsValid()) Info.Selection = FindObjectFast<UBoxComponent>(Actor, TEXT("UnitSelection"));
        UBoxComponent* Box = Info.Selection.Get();
        const bool Nearby = Box && !DeadEntityGuids.Contains(Pair.Key) &&
            FVector::DistSquared(GetPawn()->GetActorLocation(), Actor->GetActorLocation()) < FMath::Square(40 * FWowCoordinate::SCALE);
        const TCHAR* Model = Nearby && (Entity->GetField(UnitField::NPC_FLAGS) & 2) ?
            QuestMarkerModel(UIManager->GetQuestGiverStatus(Pair.Key), TrackTrivial) : nullptr;
        // The marker inherits the unit's movement, but keeps its own world size.
        auto* Marker = Info.Marker.Get();
        if (Model && CachedMpq && CachedAssetCache)
        {
            if (!Marker)
            {
                Marker = NewObject<UStaticMeshComponent>(Actor, TEXT("QuestGiverMarker"));
                Marker->SetupAttachment(Box);
                Marker->SetAbsolute(false, true, true);
                Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Marker->SetCastShadow(false);
                Marker->SetHiddenInSceneCapture(true);
                Marker->RegisterComponent();
                Info.Marker = Marker;
            }
            if (Info.MarkerModel != FName(Model))
            {
                const FString Path = FString(TEXT("Interface\\Buttons\\")) + Model + TEXT(".m2");
                Marker->SetStaticMesh(FWowDoodadManager::GetOrCreateStaticMesh(Path, CachedMpq, CachedAssetCache));
                Info.MarkerModel = FName(Model);
                Info.MarkerHeight.Reset();
                Info.MarkerPeriod = 0;
                const auto Data = FWowDoodadManager::GetOrParseM2(Path, CachedMpq);
                if (Data && !Data->RenderPasses.IsEmpty())
                {
                    const auto& Pass = Data->RenderPasses[0];
                    if ((Pass.Flags & 1) && Data->TexturePaths.IsValidIndex(Pass.TextureIndex))
                    {
                        auto* Texture = FWowTerrainMaterial::LoadBlpTexture(Data->TexturePaths[Pass.TextureIndex], CachedMpq, CachedAssetCache);
                        auto* Unlit = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowParticleAlpha"));
                        if (Texture && Unlit)
                        {
                            auto* Material = UMaterialInstanceDynamic::Create(Unlit, Marker);
                            Material->SetTextureParameterValue(TEXT("ParticleTexture"), Texture);
                            Material->SetScalarParameterValue(TEXT("IgnoreAlpha"), Pass.BlendMode == 0 ? 1.f : 0.f);
                            Marker->SetMaterial(0, Material);
                        }
                    }
                }
                if (Data && !Data->AnimationTracks.IsEmpty() && !Data->AnimationTracks[0].BoneTracks.IsEmpty())
                {
                    const auto& Clip = Data->AnimationTracks[0];
                    const auto& Root = Clip.BoneTracks[0];
                    Info.MarkerPeriod = Clip.Duration * .001f;
                    for (int32 K = 0; K < FMath::Min(Root.TransValues.Num(), Root.TransTimestamps.Num()); ++K)
                        Info.MarkerHeight.AddPoint(Root.TransTimestamps[K] * .001f, Root.TransValues[K].Z * FWowCoordinate::SCALE);
                }
            }
        }
        if (Marker) Marker->SetVisibility(Model != nullptr && Marker->GetStaticMesh());
        Info.ShowName = false;
        if (!Nearby || (!ShowNames && TargetGuid != Pair.Key)) continue;
        const auto* Name = Handler.CreatureNameCache.Find(Entity->Entry);
        if (!Name || Name->IsEmpty()) continue;
        const FVector Anchor = OverheadAnchor(*Box);
        if (FVector::DotProduct(Anchor - Camera, Rotation.Vector()) <= 0) continue;
        FVector2D Screen;
        if (!ProjectWorldLocationToScreen(Anchor, Screen)) continue;
        if (Screen.X < 0 || Screen.Y < 0 || Screen.X > Width || Screen.Y > Height) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WowUnitName), true, Actor);
        Query.AddIgnoredActor(GetPawn());
        if (GetWorld()->LineTraceTestByChannel(Camera, Anchor, ECC_Visibility, Query)) continue;
        if (Info.Name.ToString() != *Name) Info.Name = FText::FromString(*Name);
        const FString* Title = Handler.GetCreatureTitle(*Entity);
        const FString Subtitle = Title ? FString::Printf(TEXT("<%s>"), **Title) : FString();
        if (Info.Subtitle.ToString() != Subtitle) Info.Subtitle = FText::FromString(Subtitle);
        Info.Color = UIManager->GetUnitSelectionColor(Pair.Key);
        Info.ShowName = true;
    }
}
