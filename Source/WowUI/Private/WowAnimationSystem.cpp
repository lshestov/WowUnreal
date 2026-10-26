#include "WowEventSystem.h"
#include "WowFrameTypes.h"
#include "WowLuaVM.h"
#include "LuaApi/LuaApiRegistry.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
// Per-VM closures retain only playing groups. No addon globals or source overrides.
const char* AnimationRuntime=R"LUA(
local active={}
local function callback(o,event,...)
    local f=o.scripts[event]; if f then f(o,...) end
end
local common={}
function common:GetName() return self.__name end
function common:GetParent() return self.parent end
function common:GetObjectType() return self.kind end
function common:IsObjectType(t) return t==self.kind or t=='Animation' end
function common:SetScript(event,f) self.scripts[event]=f end
function common:GetScript(event) return self.scripts[event] end
function common:HookScript(event,f)
    local old=self.scripts[event]; self.scripts[event]=function(...) if old then old(...) end; f(...) end
end
local anim=setmetatable({}, {__index=common}); anim.__index=anim
function anim:SetDuration(v) self.duration=math.max(0,tonumber(v) or 0) end
function anim:GetDuration() return self.duration end
function anim:SetStartDelay(v) self.startDelay=math.max(0,tonumber(v) or 0) end
function anim:GetStartDelay() return self.startDelay end
function anim:SetEndDelay(v) self.endDelay=math.max(0,tonumber(v) or 0) end
function anim:GetEndDelay() return self.endDelay end
function anim:SetOrder(v) self.order=math.max(1,math.floor(v)) end
function anim:GetOrder() return self.order end
function anim:SetOffset(x,y) self.x=x; self.y=y end
function anim:GetOffset() return self.x,self.y end
function anim:SetChange(v) self.change=v end
function anim:GetChange() return self.change end
function anim:SetSmoothing(v) self.smoothing=v end
function anim:GetSmoothing() return self.smoothing end
function anim:Play() self.parent:Play() end
function anim:Stop() self.parent:Stop() end
function anim:Pause() self.parent:Pause() end
function anim:IsPlaying() return self.parent:IsPlaying() and (self.progress or 0)<1 end
function anim:IsStopped() return self.parent:IsStopped() end
function anim:IsPaused() return self.parent:IsPaused() end
function anim:GetProgress() return self.progress or 0 end
local group=setmetatable({}, {__index=common}); group.__index=group
local function named(o,parent,name,key)
    o.parent=parent
    if name and name~='' then
        o.__name=name:gsub('%$parent',parent:GetName() or '')
        _G[o.__name]=o
    end
    if key and key~='' then parent[key]=o end
end
local function reset(g)
    local p=g.parent
    if g.baseAlpha then p:SetAlpha(g.baseAlpha) end
    if g.points then
        p:ClearAllPoints()
        for _,a in ipairs(g.points) do p:SetPoint(unpack(a)) end
    end
end
function group:CreateAnimation(kind,name,template,order)
    assert(kind=='Alpha' or kind=='Translation','Unsupported animation type: '..tostring(kind))
    local a=setmetatable({kind=kind,duration=0,startDelay=0,endDelay=0,order=order or 1,
        change=0,x=0,y=0,smoothing='NONE',scripts={}},anim)
    named(a,self,name); self.children[#self.children+1]=a; return a
end
function group:GetAnimations() return unpack(self.children) end
function group:SetLooping(v) self.looping=v end
function group:GetLooping() return self.looping end
function group:GetDuration() return self.duration or 0 end
function group:IsPlaying() return self.playing==true end
function group:IsStopped() return not self.playing and not self.paused end
function group:IsPaused() return self.paused==true end
function group:Pause()
    if self.playing then self.playing=false; self.paused=true; active[self]=nil; callback(self,'OnPause') end
end
function group:Stop()
    local was=self.playing or self.paused; self.playing=false; self.paused=false; active[self]=nil
    reset(self); if was then for _,a in ipairs(self.children) do if a.started and not a.finished then callback(a,'OnStop') end end; callback(self,'OnStop') end
end
function group:Play()
    if self.playing then return end
    if self.paused then self.paused=false; self.playing=true; active[self]=true; return end
    local p=self.parent; self.baseAlpha=p:GetAlpha(); self.points={}
    for i=1,p:GetNumPoints() do self.points[i]={p:GetPoint(i)} end
    local orders={}; self.duration=0
    for _,a in ipairs(self.children) do
        orders[a.order]=math.max(orders[a.order] or 0,a.startDelay+a.duration+a.endDelay)
        a.progress=0; a.started=false; a.finished=false
    end
    local keys={}; for k in pairs(orders) do keys[#keys+1]=k end; table.sort(keys)
    local offset={}; for _,k in ipairs(keys) do offset[k]=self.duration; self.duration=self.duration+orders[k] end
    for _,a in ipairs(self.children) do a.offset=offset[a.order]+a.startDelay end
    self.elapsed=0; self.reverse=false; self.playing=true; active[self]=true; callback(self,'OnPlay')
end
local function update(g,delta)
    g.elapsed=g.elapsed+math.max(0,delta)
    local done=g.elapsed>=g.duration
    local time=math.min(g.elapsed,g.duration); if g.reverse then time=g.duration-time end
    local alpha,x,y=0,0,0; local hasAlpha=false; local hasTranslation=false
    for _,a in ipairs(g.children) do
        local t=time-a.offset
        local v=t<0 and 0 or (a.duration==0 and 1 or math.min(1,t/a.duration))
        if a.smoothing=='IN' then v=v*v elseif a.smoothing=='OUT' then v=1-(1-v)^2
        elseif a.smoothing=='IN_OUT' then v=v*v*(3-2*v) end
        a.progress=v
        if a.kind=='Alpha' then alpha=alpha+a.change*v; hasAlpha=true
        elseif a.kind=='Translation' then x=x+a.x*v; y=y+a.y*v; hasTranslation=true end
        if t>=0 and not a.started then a.started=true; callback(a,'OnPlay') end
        if t>=0 then callback(a,'OnUpdate',delta) end
        if v>=1 and not a.finished then a.finished=true; callback(a,'OnFinished') end
    end
    if hasAlpha then g.parent:SetAlpha(math.max(0,math.min(1,g.baseAlpha+alpha))) end
    if hasTranslation and #g.points>0 then
        g.parent:ClearAllPoints()
        for _,p in ipairs(g.points) do g.parent:SetPoint(p[1],p[2],p[3],p[4]+x,p[5]+y) end
    end
    callback(g,'OnUpdate',delta)
    if done and g.playing then
        if g.looping=='REPEAT' or g.looping=='BOUNCE' then
            g.elapsed=g.duration>0 and g.elapsed%g.duration or 0
            if g.looping=='BOUNCE' then g.reverse=not g.reverse end
            for _,a in ipairs(g.children) do a.started=false; a.finished=false end
            callback(g,'OnLoop',g.reverse and 'BACKWARD' or 'FORWARD')
        else
            g.playing=false; active[g]=nil; reset(g); callback(g,'OnFinished')
        end
    end
end
local function create(parent,name,key)
    local g=setmetatable({kind='AnimationGroup',children={},scripts={},looping='NONE'},group)
    named(g,parent,name,key); parent.__animationGroups=parent.__animationGroups or {}; table.insert(parent.__animationGroups,g); return g
end
local function tick(delta)
    local snapshot={}; for g in pairs(active) do snapshot[#snapshot+1]=g end
    for _,g in ipairs(snapshot) do if g.playing then update(g,delta) end end
end
return create,tick
)LUA";

void PopulateAnimation(lua_State* L,const FWowAnimationDef& Def)
{
    const int32 Object=lua_gettop(L);
    const auto Number=[&](const char* Key,double Value) { lua_pushnumber(L,Value); lua_setfield(L,Object,Key); };
    const auto String=[&](const char* Key,const FString& Value) { if(!Value.IsEmpty()) { lua_pushstring(L,TCHAR_TO_UTF8(*Value)); lua_setfield(L,Object,Key); } };
    Number("duration",Def.Duration); Number("startDelay",Def.StartDelay); Number("endDelay",Def.EndDelay); Number("order",Def.Order);
    Number("change",Def.Change); Number("x",Def.X); Number("y",Def.Y); String("looping",Def.Looping); String("smoothing",Def.Smoothing);
    lua_getfield(L,Object,"scripts");
    for(const auto& Script:Def.Scripts)
    {
        const FString Argument=Script.Key==TEXT("OnUpdate")?TEXT("elapsed"):Script.Key==TEXT("OnLoop")?TEXT("loopState"):TEXT("requested");
        const FString Code=TEXT("return function(self,")+Argument+TEXT(",...) ")+Script.Value+TEXT(" end"); FTCHARToUTF8 Utf8(*Code);
        if(luaL_loadbuffer(L,Utf8.Get(),Utf8.Length(),"XML animation handler")==0 && lua_pcall(L,0,1,0)==0)
            lua_setfield(L,-2,TCHAR_TO_UTF8(*Script.Key));
        else { UE_LOG(LogTemp,Warning,TEXT("Animation script compile error: %s"),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); }
    }
    lua_pop(L,1);
    for(const auto& Child:Def.Children)
    {
        lua_getfield(L,Object,"CreateAnimation"); lua_pushvalue(L,Object); lua_pushstring(L,TCHAR_TO_UTF8(*Child.Type)); lua_pushstring(L,TCHAR_TO_UTF8(*Child.Name));
        if(lua_pcall(L,3,1,0)==0)
        {
            if(!Child.ParentKey.IsEmpty()) { lua_pushvalue(L,-1); lua_setfield(L,Object,TCHAR_TO_UTF8(*Child.ParentKey)); }
            PopulateAnimation(L,Child); lua_pop(L,1);
        }
        else { UE_LOG(LogTemp,Warning,TEXT("Animation creation error: %s"),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); }
    }
}
}
#endif

bool FWowEventSystem::CreateAnimationGroup(const FWowAnimationDef& Definition)
{
#if __has_include("lua.h")
    if(!LuaVM || !LuaVM->GetState()) return false;
    auto* L=LuaVM->GetState(); const int32 Parent=lua_gettop(L);
    if(AnimationFactoryRef==LUA_NOREF)
    {
        if(luaL_loadbuffer(L,AnimationRuntime,FCStringAnsi::Strlen(AnimationRuntime),"Native UI animation runtime")!=0 || lua_pcall(L,0,2,0)!=0)
        { UE_LOG(LogTemp,Error,TEXT("Animation runtime failed: %s"),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_settop(L,Parent); return false; }
        AnimationTickRef=luaL_ref(L,LUA_REGISTRYINDEX); AnimationFactoryRef=luaL_ref(L,LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L,LUA_REGISTRYINDEX,AnimationFactoryRef); lua_pushvalue(L,Parent);
    lua_pushstring(L,TCHAR_TO_UTF8(*Definition.Name)); lua_pushstring(L,TCHAR_TO_UTF8(*Definition.ParentKey));
    if(lua_pcall(L,3,1,0)!=0) { lua_settop(L,Parent); return false; }
    PopulateAnimation(L,Definition); return true;
#else
    return false;
#endif
}

void FWowEventSystem::TickAnimations(float DeltaTime)
{
#if __has_include("lua.h")
    if(!LuaVM || AnimationTickRef==LUA_NOREF) return;
    auto* L=LuaVM->GetState(); lua_rawgeti(L,LUA_REGISTRYINDEX,AnimationTickRef); lua_pushnumber(L,DeltaTime);
    if(lua_pcall(L,1,0,0)!=0)
    { UE_LOG(LogTemp,Warning,TEXT("Animation update error: %s"),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); }
#endif
}
