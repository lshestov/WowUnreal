#include "Misc/AutomationTest.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "WowCharacterCreateWidget.h"
#include "WowCharacterCreationArt.h"
#include "Formats/M2Parser.h"
#include "WowCharacterBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "WowLuaVM.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
bool LoadCreationData()
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    return true;
}
FDbcParser Table(const TArray<TArray<uint32>>& Rows)
{
    TArray<uint8> Bytes; auto Add=[&](uint32 V){Bytes.Append(reinterpret_cast<uint8*>(&V),4);};
    Add(0x43424457); Add(Rows.Num()); Add(Rows[0].Num()); Add(Rows[0].Num()*4); Add(1);
    for(const auto& Row:Rows) for(uint32 V:Row) Add(V);
    Bytes.Add(0); FDbcParser P; P.Parse(Bytes); return P;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationOriginalData,"WowUnreal.CharacterCreation.OriginalTables",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationOriginalData::RunTest(const FString&)
{
    if(!LoadCreationData()) {AddError(TEXT("Original MPQ data required"));return false;}
    const auto& D=FDbcStore::Get(); const auto& C=D.CharacterCreation();
    TestEqual(TEXT("Ten original playable races"),C.Races().Num(),10);
    TestTrue(TEXT("Human paladin"),C.Allows(1,2)); TestFalse(TEXT("Human hunter is not a Wrath combination"),C.Allows(1,3));
    TestTrue(TEXT("Draenei shaman"),C.Allows(11,7)); TestFalse(TEXT("Blood elf warrior is not a Wrath combination"),C.Allows(10,1));
    TestFalse(TEXT("No goblin"),C.Allows(9,1)); TestFalse(TEXT("No monk"),C.Allows(1,10));
    FString Path; FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path); FMpqManager Mpq; Mpq.Initialize(Path);
    TArray<uint8> Raw; Mpq.ReadFile(TEXT("DBFilesClient\\CharStartOutfit.dbc"),Raw); FDbcParser P; P.Parse(Raw);
    TestEqual(TEXT("Original outfit record size"),P.GetRecordSize(),296u);
    for(uint32 Row=0;Row<P.GetRecordCount();++Row)
    {
        const auto* Outfit=D.CharStartOutfit().GetById(P.GetUInt(Row,0));
        if(!Outfit) {AddError(TEXT("Missing original outfit"));return false;}
        for(int32 I=0;I<24;++I)
        {
            TestEqual(TEXT("Display from byte offset 104"),Outfit->ItemDisplayIds[I],int32(P.GetUInt(Row,26+I)));
            TestEqual(TEXT("Inventory type from byte offset 200"),Outfit->ItemInventorySlots[I],int32(P.GetUInt(Row,50+I)));
            if(Outfit->ItemDisplayIds[I]>0 && Outfit->ItemInventorySlots[I]>0 && Outfit->ItemInventorySlots[I]!=18)
                TestNotNull(TEXT("Original item display exists"),D.ItemDisplayInfo().GetById(Outfit->ItemDisplayIds[I]));
        }
    }
    int32 Count=0;
    for(uint8 Race:C.Races()) for(uint8 Class:C.Classes(Race)) for(uint8 Gender:{uint8(0),uint8(1)})
    {
        FWowCharacterAppearance A; A.Race=Race; A.Class=Class; A.Gender=Gender;
        A.Skin=A.Face=A.HairColor=A.HairStyle=A.FacialHair=255;
        TestTrue(*FString::Printf(TEXT("Valid DBC appearance %u/%u/%u"),Race,Class,Gender),FCharacterCreationDbc::Normalize(A,D.CharSections(),D.CharacterFacialHairStyles()));
        const auto Saved=A;
        TestTrue(TEXT("Normalize remains valid"),FCharacterCreationDbc::Normalize(A,D.CharSections(),D.CharacterFacialHairStyles()));
        TestTrue(TEXT("Valid choices never reset"),Saved==A);
        TestNotNull(TEXT("Class starter outfit exists"),D.CharStartOutfit().GetByRaceClassGender(Race,Class,Gender));
        TestFalse(TEXT("Class name available"),C.ClassName(Class).IsEmpty()); ++Count;
    }
    TestEqual(TEXT("62 Wrath combinations, both sexes"),Count,124);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationDependencies,"WowUnreal.CharacterCreation.Dependencies",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationDependencies::RunTest(const FString&)
{
    // Deliberately sparse values and class-only/barber-only records: slider
    // indices must never be sent as protocol appearance values.
    auto P=Table({{1,1,0,0,0,0,0,1,0,2},{2,1,0,0,0,0,0,1,0,7},
        {3,1,0,1,0,0,0,1,4,2},{4,1,0,1,0,0,0,1,9,7},
        {5,1,0,3,0,0,0,1,5,3},{6,1,0,3,0,0,0,1,8,6},
        {7,1,0,0,0,0,0,5,0,11},{8,1,0,0,0,0,0,2,0,14},
        {9,1,0,0,0,0,0,9,0,19}});
    FCharSectionsDbc S; S.Load(P); auto FP=Table({{1,0,0,0,0,0,0,0},{1,0,3,0,0,0,0,0}});
    FCharacterFacialHairStylesDbc F; F.Load(FP);
    FWowCharacterAppearance A;
    TestTrue(TEXT("Sparse normalize"),FCharacterCreationDbc::Normalize(A,S,F));
    TestEqual(TEXT("Skin raw ID"),A.Skin,uint8(2)); TestEqual(TEXT("Dependent face raw ID"),A.Face,uint8(4));
    TestEqual(TEXT("Hair raw style"),A.HairStyle,uint8(5)); TestEqual(TEXT("Hair raw colour"),A.HairColor,uint8(3));
    A.Skin=7; A.HairColor=6; TestTrue(TEXT("New dependent pair"),FCharacterCreationDbc::Normalize(A,S,F));
    TestEqual(TEXT("New face"),A.Face,uint8(9)); TestEqual(TEXT("New hair style"),A.HairStyle,uint8(8));
    const auto Skins=FCharacterCreationDbc::Options(EWowAppearanceOption::Skin,A,S,F);
    TestTrue(TEXT("Normal excludes DK, barber and NPC"),Skins==TArray<uint8>({2,7}));
    A.Class=6; const auto DK=FCharacterCreationDbc::Options(EWowAppearanceOption::Skin,A,S,F);
    TestTrue(TEXT("DK flags differ"),DK==TArray<uint8>({11}));
    TestTrue(TEXT("Nontexture facial geosets"),FCharacterCreationDbc::Options(EWowAppearanceOption::FacialHair,A,S,F)==TArray<uint8>({0,3}));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationSubmit,"WowUnreal.CharacterCreation.Submission",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationSubmit::RunTest(const FString&)
{
    if(!LoadCreationData()) return false;
    auto W=SNew(SWowCharacterCreateWidget); W->Tick(FGeometry(),0,0);
    FWowCharacterAppearance A; A.Race=11; A.Class=7; A.Gender=1; A.Skin=3; A.Face=2; A.HairStyle=4; A.HairColor=2; A.FacialHair=2;
    W->SetAppearance(A); A=W->GetAppearance(); int32 Sent=0;
    W->OnCharacterCreated.BindLambda([&](const FString& Name,uint8 Race,uint8 Class,uint8 Gender,uint8 Skin,uint8 Face,uint8 Hair,uint8 Color,uint8 Facial)
    {
        ++Sent; TestEqual(TEXT("Trimmed name"),Name,FString(TEXT("Qaface")));
        FWowCharacterAppearance Wire; Wire.Race=Race;Wire.Class=Class;Wire.Gender=Gender;Wire.Skin=Skin;Wire.Face=Face;Wire.HairStyle=Hair;Wire.HairColor=Color;Wire.FacialHair=Facial;
        TestTrue(TEXT("Every displayed parameter sent intact"),Wire==A);
    });
    W->SetName(TEXT("  ")); W->Submit(); TestEqual(TEXT("Empty name not sent"),Sent,0);
    W->SetName(TEXT(" Qaface ")); W->Submit(); W->Submit(); W->Submit(); TestEqual(TEXT("Single pending request"),Sent,1);
    auto Other=A;Other.Gender=0;W->SetAppearance(Other);TestTrue(TEXT("Pending appearance immutable"),W->GetAppearance()==A);
    W->CompleteRequest(TEXT("Name in use")); W->Submit(); TestEqual(TEXT("Error allows retry"),Sent,2);
    W->CompleteRequest(TEXT("Refreshing"),true);W->Submit();TestEqual(TEXT("Success remains locked until list refresh"),Sent,2);
    W->OnCharacterCreated.Unbind(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationGlue,"WowUnreal.CharacterCreation.OriginalGlueAssets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationGlue::RunTest(const FString&)
{
    FWowLuaVM DataVm;
    TestTrue(TEXT("Isolated glue-data VM starts"),DataVm.Initialize(false));
    TestTrue(TEXT("Data VM retains safe table operations"),DataVm.ExecuteString(TEXT("__probe = type(CreateFrame)..':'..type(os)..':'..table.concat({1,2},',')")));
    TestEqual(TEXT("Data VM cannot access gameplay API or unsafe modules"),DataVm.GetGlobalString(TEXT("__probe")),FString(TEXT("nil:nil:1,2")));
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq;if(!Mpq.Initialize(Path)) return false;
    FWowCharacterCreationArt Art(&Mpq);
    TestEqual(TEXT("Original localized action"),Art.String(TEXT("CHARACTER_CREATE_ACCEPT")),FString(TEXT("Accept")));
    TestFalse(TEXT("Original race description"),Art.String(TEXT("RACE_INFO_HUMAN")).IsEmpty());
    const TCHAR* Assets[]={TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OuterBorder"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Banners"),TEXT("Interface\\Glues\\CharacterCreate\\CharacterCreate-LabelFrame"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-InfoBox"),TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-IconShadow"),
        TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Highlights"),TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Gender"),
        TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"),TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Up"),
        TEXT("Interface\\Glues\\Common\\Glue-LeftArrow-Button-Up"),TEXT("Interface\\Glues\\Common\\Glue-RightArrow-Button-Up")};
    for(const TCHAR* Asset:Assets) TestNotNull(Asset,Art.Brush(Asset)->GetResourceObject());
    const auto* Race=Art.Icon(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Races"),TEXT("RACE_ICON_TCOORDS"),TEXT("HUMAN_MALE"));
    TestNotNull(TEXT("Original race atlas"),Race->GetResourceObject());
    const FBox2f Region=Race->GetUVRegion();
    TestTrue(TEXT("Original atlas coordinates, not whole atlas"),Region.bIsValid && Region.Max==FVector2f(.125f,.25f));
    const auto* Glow=Art.Brush(TEXT("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Highlights"),FVector2f(.5f,0),FVector2f(1,.5f),ESlateBrushMirrorType::NoMirror,true);
    auto* MID=Cast<UMaterialInstanceDynamic>(Glow->GetResourceObject());
    TestTrue(TEXT("Original ADD highlight uses the atlas-aware UI shader"),MID && MID->GetMaterial()->MaterialDomain==MD_UI && MID->GetMaterial()->BlendMode==BLEND_Additive);
    if(MID)
    {
        FLinearColor TL,BR;
        TestTrue(TEXT("Atlas top-left parameter"),MID->GetVectorParameterValue(TEXT("UVTL"),TL));
        TestTrue(TEXT("Atlas bottom-right parameter"),MID->GetVectorParameterValue(TEXT("UVBR"),BR));
        TestTrue(TEXT("Selected icon samples only the checked quadrant"),TL.Equals(FLinearColor(.5f,0,0,0)) && BR.Equals(FLinearColor(1,.5f,0,0)));
    }
    for(const TCHAR* Name:{TEXT("Human"),TEXT("Orc"),TEXT("Dwarf"),TEXT("NightElf"),TEXT("Scourge"),TEXT("Tauren"),TEXT("BloodElf"),TEXT("Draenei"),TEXT("DeathKnight")})
    {
        const FString Model=FString::Printf(TEXT("Interface\\Glues\\Models\\UI_%s\\UI_%s"),Name,Name);
        TArray<uint8> Raw,Skin;TestTrue(TEXT("Original creation scene"),Mpq.ReadFile(Model+TEXT(".m2"),Raw));
        TestTrue(TEXT("Original scene skin topology"),Mpq.ReadFile(Model+TEXT("00.skin"),Skin));
        const auto Data=FM2Parser::Parse(Raw,Skin);
        TestTrue(TEXT("Original authored camera"),Data.Cameras.Num()>0);
        TestTrue(TEXT("Original character attachment"),Data.Attachments.ContainsByPredicate([](const auto& A){return A.Id==0;}));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationGeosets,"WowUnreal.CharacterCreation.OriginalGeosets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationGeosets::RunTest(const FString&)
{
    if(!LoadCreationData()) return false;
    const auto& D=FDbcStore::Get();bool FoundZero=false;
    for(const auto& Entry:D.CharacterFacialHairStyles().GetAll())
    {
        if(!D.CharacterCreation().Races().Contains(uint8(Entry.RaceID))) continue;
        FWowCharacterTexture::FCustomization C;C.RaceId=Entry.RaceID;C.Gender=Entry.SexID;C.FacialHairStyle=Entry.VariationID;
        const auto G=FWowCharacterBuilder::ComputeDefaultGeosets(C);
        // Original SetBeardStyle uses 100,300,200,1600,1700 in this order.
        TestEqual(TEXT("Beard from column 0"),G[1],uint16(Entry.Geosets[0]));
        TestEqual(TEXT("Moustache from column 1"),G[3],uint16(Entry.Geosets[1]));
        TestEqual(TEXT("Sideburn from column 2"),G[2],uint16(Entry.Geosets[2]));
        TestEqual(TEXT("Jewellery from column 3"),G[16],uint16(Entry.Geosets[3]));
        if(Entry.Geosets[0]==0) {FoundZero=true;TestEqual(TEXT("Zero remains authored zero"),G[1],uint16(0));}
    }
    TestTrue(TEXT("Original zero variants exercised"),FoundZero);
    FWowCharacterTexture::FCustomization DK;DK.ClassId=6;
    TestEqual(TEXT("Original death knight eye geoset 1703"),FWowCharacterBuilder::ComputeDefaultGeosets(DK)[17],uint16(3));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCreationSelectionMemory,"WowUnreal.CharacterCreation.SelectionMemory",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCharacterCreationSelectionMemory::RunTest(const FString&)
{
    if(!LoadCreationData()) return false;
    auto W=SNew(SWowCharacterCreateWidget);W->Tick(FGeometry(),0,0);
    FWowCharacterAppearance Orc;Orc.Race=2;Orc.Class=7;Orc.Skin=3;Orc.Face=2;Orc.HairStyle=5;Orc.HairColor=2;Orc.FacialHair=2;
    W->SetAppearance(Orc);Orc=W->GetAppearance();
    W->SelectIdentity(0,11);TestEqual(TEXT("Direct Draenei selection retains the allowed shaman class"),W->GetAppearance().Class,uint8(7));
    W->SelectIdentity(0,2);TestTrue(TEXT("Returning race restores its exact valid look"),W->GetAppearance()==Orc);
    W->SelectIdentity(2,1);auto Female=W->GetAppearance();const auto Colors=W->GetOptions(EWowAppearanceOption::HairColor);
    Female.HairColor=Colors.Last();W->SetAppearance(Female);Female=W->GetAppearance();
    W->SelectIdentity(2,0);TestTrue(TEXT("Male choice retained"),W->GetAppearance()==Orc);
    W->SelectIdentity(2,1);TestTrue(TEXT("Female choice retained"),W->GetAppearance()==Female);
    W->SelectIdentity(1,6);W->SelectIdentity(1,7);TestTrue(TEXT("Returning from DK restores the normal appearance"),W->GetAppearance()==Female);
    W->SelectIdentity(0,9);W->SelectIdentity(1,11);TestTrue(TEXT("Unavailable race/class selection cannot replace the valid choice"),W->GetAppearance()==Female);
    return true;
}
#endif
