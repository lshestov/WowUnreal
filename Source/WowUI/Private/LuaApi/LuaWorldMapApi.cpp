#include "LuaApiRegistry.h"
#include "WowMapUi.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowConnectionManager.h"
#include "Components/Image.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/M2Parser.h"
#include "Mpq/MpqManager.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using namespace WowMapUi;
const FMapUiDbc& Data() {return FDbcStore::Get().MapUi();}
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
void String(lua_State* L,const FString& S) {lua_pushstring(L,TCHAR_TO_UTF8(*S));}
FString Name(const FMapUiDbc::FArea& A)
{
    if(A.AreaId) {if(const auto* Row=FDbcStore::Get().AreaTable().GetById(A.AreaId)) return Row->Name;}
    else if(const auto* Row=FDbcStore::Get().Maps().GetById(A.MapId)) return Row->Name;
    return A.Name;
}
TArray<const FWowEntity*> Vehicles(FWowLuaContext* C)
{
    TArray<const FWowEntity*> Out;
    if(C && C->EntityManager) for(const auto& P:C->EntityManager->GetAll()) if(P.Value->IsUnit() && P.Value->VehicleId) {Out.Add(P.Value.Get()); if(Out.Num()==40) break;}
    Out.Sort([](const auto& A,const auto& B){return A.Guid<B.Guid;}); return Out;
}
int CurrentZone(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(C) {C->SelectedRows.Remove(TEXT("mapArea")); C->SelectedRows.Remove(TEXT("mapFloor")); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("WORLD_MAP_UPDATE"));} return 0;
}
void ZoomOut(FWowLuaContext* C)
{
    const auto S=Selection(C); if(S.Floor && S.Area && S.Area->Parent>0) Select(C,S.Area->Parent);
    else if(S.Floor && S.Area && S.Area->DefaultFloor==-1) Select(C,S.Area->Id,0);
    else if(S.Area && S.Area->AreaId && S.Continent>0) SelectZoom(C,S.Continent,0);
    else if(S.Area && S.Area->Parent>0) Select(C,S.Area->Parent);
    else Select(C,S.Area?-1:-2);
}
void CreateArrows(lua_State* L,bool Mini)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return;
    const char* Prefix=Mini?"PlayerMiniArrow":"PlayerArrow";
    FString Parent=TEXT("WorldMapFrame");
    if(lua_istable(L,1)) {lua_getfield(L,1,"__handle"); Parent=C->FrameManager->GetFrameName(lua_tointeger(L,-1)); lua_pop(L,1);}
    for(int32 I=0;I<2;++I)
    {
        FWowFrameDef F; F.Name=UTF8_TO_TCHAR(Prefix); F.Name+=I?TEXT("EffectFrame"):TEXT("Frame");
        if(C->FrameManager->FindFrame(F.Name)>=0) continue;
        F.Parent=Parent; F.Width=F.Height=32; F.FrameLevel=105; F.bHidden=true;
        if(!I)
        {
            FWowTextureElement T; T.Name=F.Name+TEXT("Texture"); T.File=TEXT("Interface\\Minimap\\MinimapArrow"); T.bSetAllPoints=true;
            FWowLayer Layer; Layer.Textures.Add(T); F.Layers.Add(Layer);
        }
        C->FrameManager->CreateFrame(F);
    }
}
int PositionArrow(lua_State* L,bool Mini)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    FWowAnchor A; A.Point=FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(luaL_checkstring(L,1)));
    A.RelativeTo=UTF8_TO_TCHAR(luaL_checkstring(L,2)); A.RelativePoint=lua_isstring(L,3)?FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(lua_tostring(L,3))):A.Point;
    A.OffsetX=luaL_optnumber(L,4,0); A.OffsetY=luaL_optnumber(L,5,0);
    for(const TCHAR* Suffix:{TEXT("Frame"),TEXT("EffectFrame")}) C->FrameManager->SetFrameAnchors(C->FrameManager->FindFrame(FString(Mini?TEXT("PlayerMiniArrow"):TEXT("PlayerArrow"))+Suffix),{A});
    return 0;
}
int ShowArrow(lua_State* L,bool Mini)
{
    auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) for(const TCHAR* Suffix:{TEXT("Frame"),TEXT("EffectFrame")})
        C->FrameManager->SetFrameVisible(C->FrameManager->FindFrame(FString(Mini?TEXT("PlayerMiniArrow"):TEXT("PlayerArrow"))+Suffix),lua_toboolean(L,1) && (!lua_isnumber(L,1) || lua_tonumber(L,1)!=0)); return 0;
}
int Highlight(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const FVector2D P(luaL_checknumber(L,1),luaL_checknumber(L,2)); const auto* A=HoverArea(C,P);
    if(!A) {lua_pushnil(L); lua_pushnil(L); for(int32 I=0;I<6;++I) lua_pushnumber(L,0); return 8;}
    String(L,Name(*A)); const auto S=Selection(C); const FMapUiDbc::FOverlay* Overlay=nullptr;
    if(S.Area && S.Area->AreaId)
    {
        for(const auto* O:Overlays(C)) if(P.X*1002>=O->Left && P.X*1002<=O->Right && P.Y*668>=O->Top && P.Y*668<=O->Bottom) {Overlay=O; break;}
        if(Overlay) for(uint32 Id:Overlay->Areas) if(const auto* Row=FDbcStore::Get().AreaTable().GetById(Id)) {lua_pop(L,1); String(L,Row->Name); break;}
        lua_pushnil(L); for(int32 I=0;I<6;++I) lua_pushnumber(L,0); return 8;
    }
    String(L,A->Name); FVector4 R(0,0,0,0); double U=1,V=1;
    if(!S.Area)
    {
        if(const auto* Cont=Data().Continent(A->MapId)) {R=WorldContinentBounds(*Cont); const int32 W=Cont->Right-Cont->Left+1,H=Cont->Bottom-Cont->Top+1; U=FMath::Min(1.,double(W)/H); V=FMath::Min(1.,double(H)/W);}
    }
    else
    {
        const auto TL=Project(C,A->MapId,FVector(A->X1,A->Y1,0),0,true);
        R=FVector4(TL.X,TL.Y,(A->Y1-A->Y2)/(S.Area->Y1-S.Area->Y2),(A->X1-A->X2)/(S.Area->X1-S.Area->X2)); const int32 H=FMath::Max(1,int32(FMath::Abs(128*(A->X1-A->X2)/(A->Y1-A->Y2)))); V=double(H)/FMath::RoundUpToPowerOfTwo(uint32(H));
    }
    lua_pushnumber(L,U); lua_pushnumber(L,V); lua_pushnumber(L,R.Z); lua_pushnumber(L,R.W); lua_pushnumber(L,R.X); lua_pushnumber(L,R.Y); return 8;
}
}
void WowLuaApi::RegisterWorldMapApi(lua_State* L)
{
    Reg(L,"GetMapContinents",[](lua_State* S)->int {const auto Rows=Continents(); lua_checkstack(S,Rows.Num()); for(const auto* A:Rows) String(S,Name(*A)); return Rows.Num();});
    Reg(L,"GetMapZones",[](lua_State* S)->int {const auto Rows=Zones(luaL_checkinteger(S,1)); lua_checkstack(S,Rows.Num()); for(const auto* A:Rows) String(S,Name(*A)); return Rows.Num();});
    Reg(L,"GetCurrentMapContinent",[](lua_State* S)->int {lua_pushinteger(S,Selection(GetContext(S)).Continent); return 1;});
    Reg(L,"GetCurrentMapZone",[](lua_State* S)->int {lua_pushinteger(S,Selection(GetContext(S)).Zone); return 1;});
    Reg(L,"GetCurrentMapAreaID",[](lua_State* S)->int {const auto M=Selection(GetContext(S)); lua_pushinteger(S,M.Area?M.Area->Id+1:0); return 1;});
    Reg(L,"GetCurrentMapDungeonLevel",[](lua_State* S)->int {const auto M=Selection(GetContext(S)); lua_pushinteger(S,(M.Floor?M.Floor->Index:0)+(M.Area && M.Area->DefaultFloor==-1?1:0)); return 1;});
    Reg(L,"GetNumDungeonMapLevels",[](lua_State* S)->int {const auto M=Selection(GetContext(S)); TSet<uint32> Floors; if(M.Area && M.Area->AreaId && (M.Floor || M.Area->DefaultFloor==-1)) for(const auto& F:Data().Floors()) if(F.MapId==M.Area->MapId) Floors.Add(F.Index); lua_pushinteger(S,Floors.Num()+(M.Area && M.Area->DefaultFloor==-1?1:0)); return 1;});
    Reg(L,"DungeonUsesTerrainMap",[](lua_State* S)->int {const auto M=Selection(GetContext(S)); if(M.Area && M.Area->DefaultFloor==-1) lua_pushinteger(S,1); else lua_pushnil(S); return 1;});
    Reg(L,"GetMapInfo",[](lua_State* S)->int {
        const auto M=Selection(GetContext(S)); double W=0,H=0;
        if(M.Area) {String(S,M.Area->Name); W=FMath::Abs(M.Area->Y1-M.Area->Y2); H=FMath::Abs(M.Area->X1-M.Area->X2); if(M.Floor) {W=M.Floor->MaxX-M.Floor->MinX; H=M.Floor->MaxY-M.Floor->MinY;} else if(!M.Area->AreaId) if(const auto* Cont=Data().Continent(M.Area->MapId)) {W=Cont->Right-Cont->Left+1; H=Cont->Bottom-Cont->Top+1;}}
        else lua_pushnil(S); const int32 Height=W>0?int32(H*1002/W):0; lua_pushinteger(S,Height); lua_pushinteger(S,Height>0?FMath::RoundUpToPowerOfTwo(uint32(Height)):0); return 3;
    });
    Reg(L,"SetMapToCurrentZone",CurrentZone);
    Reg(L,"SetMapZoom",[](lua_State* S)->int {SelectZoom(GetContext(S),luaL_checkinteger(S,1),luaL_optinteger(S,2,0)); return 0;});
    Reg(L,"SetMapByID",[](lua_State* S)->int {Select(GetContext(S),luaL_checkinteger(S,1)); return 0;});
    Reg(L,"SetDungeonMapLevel",[](lua_State* S)->int {auto* C=GetContext(S); const auto M=Selection(C); const int32 Level=luaL_checkinteger(S,1)-(M.Area && M.Area->DefaultFloor==-1?1:0); if(!M.Area || Level<0) return 0; if(!Level && M.Area->DefaultFloor==-1) Select(C,M.Area->Id,0); else for(const auto& F:Data().Floors()) if(F.MapId==M.Area->MapId && int32(F.Index)==Level) {Select(C,M.Area->Id,F.Id); break;} return 0;});
    Reg(L,"IsZoomOutAvailable",[](lua_State* S)->int {const auto M=Selection(GetContext(S)); lua_pushboolean(S,!M.Cosmic && (!M.Area || M.Continent>=0 || (M.Area && M.Area->Parent>0) || M.Floor)); return 1;});
    Reg(L,"ZoomOut",[](lua_State* S)->int {ZoomOut(GetContext(S)); return 0;});
    Reg(L,"ProcessMapClick",[](lua_State* S)->int {auto* C=GetContext(S); const auto M=Selection(C); if(!M.Area || !M.Area->AreaId) if(const auto* A=HoverArea(C,{luaL_checknumber(S,1),luaL_checknumber(S,2)})) Select(C,A->Id); return 0;});
    Reg(L,"ClickLandmark",[](lua_State* S)->int {const auto* F=Data().Floor(luaL_checkinteger(S,1)); if(F) for(const auto& A:Data().Areas()) if(A.AreaId && A.MapId==F->MapId) {Select(GetContext(S),A.Id,F->Id); break;} return 0;});
    Reg(L,"UpdateMapHighlight",Highlight);
    Reg(L,"GetPlayerMapPosition",[](lua_State* S)->int {auto* C=GetContext(S); const FString Unit=UTF8_TO_TCHAR(luaL_checkstring(S,1)); const auto* E=ResolveUnitToken(S); FVector2D P(-1,-1); if(E && (Unit==TEXT("player") || Unit.StartsWith(TEXT("party")) || Unit.StartsWith(TEXT("raid")))) P=PlayerPosition(C,E->Guid); lua_pushnumber(S,P.X>=0?P.X:0); lua_pushnumber(S,P.Y>=0?P.Y:0); return 2;});
    Reg(L,"GetCorpseMapPosition",[](lua_State* S)->int {auto* C=GetContext(S); FVector2D P(-1,-1); if(C && C->ConnectionManager) {const auto& M=C->ConnectionManager->PacketHandler.CorpseMarker; if(M.Present) P=Project(C,M.MapId,M.Position,CurrentFloor(C));} lua_pushnumber(S,P.X>=0?P.X:0); lua_pushnumber(S,P.Y>=0?P.Y:0); return 2;});
    Reg(L,"GetDeathReleasePosition",[](lua_State* S)->int {auto* C=GetContext(S); FVector2D P(-1,-1); if(C && C->ConnectionManager) {const auto& M=C->ConnectionManager->PacketHandler.ReleaseMarker; if(M.Present) P=Project(C,M.MapId,M.Position,CurrentFloor(C));} lua_pushnumber(S,P.X>=0?P.X:0); lua_pushnumber(S,P.Y>=0?P.Y:0); return 2;});
    Reg(L,"GetNumMapOverlays",[](lua_State* S)->int {lua_pushinteger(S,Overlays(GetContext(S)).Num()); return 1;});
    Reg(L,"GetMapOverlayInfo",[](lua_State* S)->int {const auto Rows=Overlays(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) {lua_pushnil(S); for(int32 N=0;N<6;++N) lua_pushinteger(S,0); return 7;} const auto& O=*Rows[I]; const auto* A=Data().Area(O.MapAreaId); String(S,FString::Printf(TEXT("Interface\\WorldMap\\%s\\%s"),A?*A->Name:TEXT("World"),*O.Name)); lua_pushinteger(S,O.Width); lua_pushinteger(S,O.Height); lua_pushinteger(S,O.X); lua_pushinteger(S,O.Y); lua_pushinteger(S,O.MapPointX); lua_pushinteger(S,O.MapPointY); return 7;});
    Reg(L,"GetNumMapLandmarks",[](lua_State* S)->int {lua_pushinteger(S,Landmarks(GetContext(S)).Num()); return 1;});
    Reg(L,"GetMapLandmarkInfo",[](lua_State* S)->int {const auto Rows=Landmarks(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) return 0; const auto& P=Rows[I]; String(S,P.Name); String(S,P.Description); lua_pushinteger(S,P.Icon); lua_pushnumber(S,P.Point.X); lua_pushnumber(S,P.Point.Y); if(P.Link) lua_pushinteger(S,P.Link); else lua_pushnil(S); lua_pushboolean(S,P.Graveyard); return 7;});
    Reg(L,"CreateWorldMapArrowFrame",[](lua_State* S)->int {CreateArrows(S,false); return 0;});
    Reg(L,"CreateMiniWorldMapArrowFrame",[](lua_State* S)->int {CreateArrows(S,true); return 0;});
    Reg(L,"PositionWorldMapArrowFrame",[](lua_State* S)->int {return PositionArrow(S,false);});
    Reg(L,"PositionMiniWorldMapArrowFrame",[](lua_State* S)->int {return PositionArrow(S,true);});
    Reg(L,"ShowWorldMapArrowFrame",[](lua_State* S)->int {return ShowArrow(S,false);});
    Reg(L,"ShowMiniWorldMapArrowFrame",[](lua_State* S)->int {return ShowArrow(S,true);});
    Reg(L,"UpdateWorldMapArrowFrames",[](lua_State* S)->int {auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; if(P && C->FrameManager) for(const TCHAR* N:{TEXT("PlayerArrowFrameTexture"),TEXT("PlayerMiniArrowFrameTexture")}) if(auto* I=C->FrameManager->GetNamedTexture(N)) I->SetRenderTransformAngle(-FMath::RadiansToDegrees(P->Movement.Orientation)); return 0;});
    Reg(L,"InitWorldMapPing",[](lua_State* S)->int {
        auto* C=GetContext(S); if(!C || !C->FrameManager) return 0; auto& F=*C->FrameManager; const auto H=F.FindFrame(TEXT("WorldMapPing"));
        if(H<0 || F.GetNamedTexture(TEXT("WorldMapPingTexture"))) return 0;
        TArray<uint8> Bytes,Skin; if(auto* Mpq=F.GetMpqManager(); Mpq && Mpq->ReadFile(TEXT("Interface\\MiniMap\\Ping\\MinimapPing.m2"),Bytes) && Mpq->ReadFile(TEXT("Interface\\MiniMap\\Ping\\MinimapPing00.skin"),Skin))
        {const auto Model=FM2Parser::Parse(Bytes,Skin); for(const auto& Path:Model.TexturePaths) if(!Path.IsEmpty()) {FWowTextureElement T; T.Name=TEXT("WorldMapPingTexture"); T.File=Path; T.bSetAllPoints=true; F.LoadTextureDefinition(T,TEXT("WorldMapPing")); break;}}
        return 0;
    });
    Reg(L,"GetNumMapDebugObjects",[](lua_State* S)->int {lua_pushinteger(S,0); return 1;});
    Reg(L,"HasDebugZoneMap",[](lua_State* S)->int {lua_pushboolean(S,false); return 1;});
    Reg(L,"GetNumBattlefieldVehicles",[](lua_State* S)->int {lua_pushinteger(S,Vehicles(GetContext(S)).Num()); return 1;});
    Reg(L,"GetBattlefieldVehicleInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=Vehicles(C); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I) || !C || !C->ConnectionManager) return 0;
        const auto& E=*Rows[I]; const auto P=PlayerPosition(C,E.Guid); if(P.X<0) return 0;
        const auto* V=Data().Vehicle(E.VehicleId); const auto* N=C->ConnectionManager->PacketHandler.CreatureNameCache.Find(E.Entry);
        lua_pushnumber(S,P.X); lua_pushnumber(S,P.Y); if(N) String(S,*N); else lua_pushnil(S); lua_pushboolean(S,E.GetField(UnitField::FLAGS)&0x1000000);
        const char* Types[]={"Drive","Fly","Idle","Airship Horde","Airship Alliance"}; if(V && (V->Flags&0x10000000) && V->Locomotion<5) lua_pushstring(S,Types[V->Locomotion]); else lua_pushnil(S);
        lua_pushnumber(S,E.Movement.Orientation); const auto* Player=C->EntityManager->GetLocalPlayer(); lua_pushboolean(S,E.Guid==C->EntityManager->LocalPlayerGuid || (Player && Player->GetField64(UnitField::CHARM)==E.Guid)); lua_pushboolean(S,E.GetField(UnitField::HEALTH)>0); return 8;
    });
}
#else
void WowLuaApi::RegisterWorldMapApi(lua_State*) {}
#endif
