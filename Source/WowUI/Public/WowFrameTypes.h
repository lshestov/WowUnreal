#pragma once
#include "CoreMinimal.h"

/** Anchor point names */
UENUM()
enum class EWowAnchorPoint : uint8
{
	TOPLEFT, TOP, TOPRIGHT,
	LEFT, CENTER, RIGHT,
	BOTTOMLEFT, BOTTOM, BOTTOMRIGHT
};

/** Frame strata levels (z-order) */
UENUM()
enum class EWowFrameStrata : uint8
{
	BACKGROUND = 0,
	LOW,
	MEDIUM,
	HIGH,
	DIALOG,
	FULLSCREEN,
	FULLSCREEN_DIALOG,
	TOOLTIP
};

/** Draw layers within a frame */
UENUM()
enum class EWowDrawLayer : uint8
{
	BACKGROUND = 0,
	BORDER,
	ARTWORK,
	OVERLAY,
	HIGHLIGHT
};

/** An anchor definition */
struct FWowAnchor
{
	EWowAnchorPoint Point = EWowAnchorPoint::CENTER;
	FString RelativeTo;
	EWowAnchorPoint RelativePoint = EWowAnchorPoint::CENTER;
	float OffsetX = 0.f;
	float OffsetY = 0.f;
};

/** Native XML animation data; names remain relative to the immediate owner. */
struct FWowAnimationDef
{
    FString Type,Name,ParentKey,Looping,Smoothing;
    float Duration=0,StartDelay=0,EndDelay=0,Change=0,X=0,Y=0;
    int32 Order=1;
    TMap<FString,FString> Scripts;
    TArray<FWowAnimationDef> Children;
};

/** A texture element within a layer */
struct FWowTextureElement
{
	FString Inherits;
	bool bVirtual=false;
    bool bXmlDefaults=false; // PostLoadXML stretches textures lacking anchors.
	bool bHasTexCoords=false;
	bool bAdditive = false;
    bool bSliderThumb=false;
	FString Name;
	FString ParentKey;
	FString File;
    TArray<FWowAnimationDef> Animations;
	float Width = 0.f;
	float Height = 0.f;
	TArray<FWowAnchor> Anchors;
	FLinearColor VertexColor = FLinearColor::White;
	bool bSetAllPoints = false;
	bool bHidden = false;
	// Tex coords
	float Left = 0.f, Right = 1.f, Top = 0.f, Bottom = 1.f;
};

/** A font string element within a layer */
struct FWowFontStringElement
{
	bool bHasColor = false;
	bool bHidden = false;
	FString Name;
	FString ParentKey;
	FString Inherits;
	FString Text;
	float Width = 0.f;
	float Height = 0.f;
	TArray<FWowAnchor> Anchors;
	FString JustifyH = TEXT("CENTER");
	FString JustifyV = TEXT("MIDDLE");
	FLinearColor Color = FLinearColor::White;
	float FontHeight = 0.f;
	FString FontFlags; // OUTLINE, THICKOUTLINE, MONOCHROME
};

/** A layer containing textures and font strings */
struct FWowLayer
{
	EWowDrawLayer Level = EWowDrawLayer::ARTWORK;
	TArray<FWowTextureElement> Textures;
	TArray<FWowFontStringElement> FontStrings;
};

/** Script handler (event name -> Lua code) */
struct FWowScriptHandler
{
	FString Event; // OnLoad, OnEvent, OnClick, OnUpdate, etc.
	FString Code;  // Inline Lua code
	FString File;  // External Lua file reference
};

/** Backdrop definition (9-slice background) */
struct FWowBackdrop
{
	FString BgFile;
	FString EdgeFile;
	float EdgeSize = 0;
	float TileSize = 0;
	bool Tile = false;
	float InsetLeft = 0, InsetRight = 0, InsetTop = 0, InsetBottom = 0;
	FLinearColor Color=FLinearColor::White,BorderColor=FLinearColor::White;
};

/** Frame types */
UENUM()
enum class EWowFrameType : uint8
{
	Frame,
	Button,
	CheckButton,
	EditBox,
	ScrollFrame,
	ScrollingMessageFrame,
	MessageFrame,
	SimpleHTML,
	Slider,
	StatusBar,
	Cooldown,
	ColorSelect,
	GameTooltip,
	Minimap,
	Model,
	PlayerModel,
	DressUpModel,
	TabardModel,
	WorldFrame
};

/** Complete parsed frame definition */
struct WOWUI_API FWowFrameDef
{
	EWowFrameType Type = EWowFrameType::Frame;
	FString Name;
	FString ParentKey;
	FString Parent;
	FString Inherits; // Comma-separated template names
	bool bVirtual = false;
	bool bAnonymous = false; // Private layout identity; Lua GetName() remains nil.
	bool bHidden = false;
	bool bHiddenSpecified = false; // XML hidden="false" overrides a hidden template.
	bool bSetAllPoints = false;
	bool bEnableMouse = false;
	bool bEnableKeyboard = false;
	bool bScrollChild = false;
	bool bMovable=false,bResizable=false,bClampedToScreen=false;
	bool bMotionScriptsWhileDisabled=false;
	FVector4 HitInsets=FVector4(0,0,0,0);
	FVector4 ClampInsets=FVector4(0,0,0,0); // left, right, top, bottom
	FVector2D MinResize=FVector2D(0,0),MaxResize=FVector2D(100000,100000);

	float Width = 0.f;
	float Height = 0.f;

	float Scale = 1.f;
	EWowFrameStrata Strata = EWowFrameStrata::MEDIUM;
	int32 FrameLevel = 0;
	int32 FrameID = 0; // WoW XML "id" attribute

	TArray<FWowAnchor> Anchors;
	TArray<FWowLayer> Layers;
	TArray<FWowScriptHandler> Scripts;
	TArray<FString> NamedObjectGlobals;
	TArray<FWowFrameDef> Children;

	TOptional<FWowBackdrop> Backdrop;

	// Type-specific properties
	FString ButtonText;
	FString NormalFont,HighlightFont,DisabledFont;
	bool bMultiLine=false;
	int32 EditBoxLetters=0;
	TMap<FString, FWowTextureElement> ButtonTextureRegions;
	FString NormalTexture, PushedTexture, HighlightTexture, DisabledTexture;
	FString CheckedTexture;
	FString NormalTextureName, PushedTextureName, HighlightTextureName, DisabledTextureName;
	FString NormalTextureParentKey, PushedTextureParentKey, HighlightTextureParentKey, DisabledTextureParentKey;
	float MinValue = 0.f, MaxValue = 100.f, DefaultValue = 0.f;
    float ValueStep=.001f;
    uint8 SliderProperties=0; // Explicit XML min/max/default/step attributes.
	FString Orientation; // HORIZONTAL, VERTICAL (for Slider/StatusBar)
};

/** Directive from XML parsing */
struct FWowXmlDirective
{
	enum class EType { Frame, Include, Script, Font, Texture };
	EType Type;

	// For Frame
	FWowFrameDef FrameDef;
	FWowTextureElement TextureDef;
	FString TextureParent;

	// For Include/Script
	FString FilePath;

	// For Font
	FString FontName;
	FString FontInherits;
	float FontHeight = 0.f;
	FString FontFlags;
	TOptional<FLinearColor> FontColor;
};
