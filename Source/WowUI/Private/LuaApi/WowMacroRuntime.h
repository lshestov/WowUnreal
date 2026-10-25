#pragma once
namespace WowMacroRuntime
{
inline constexpr const char* Source=R"LUA(
local function store(character)
    local key=character and 'IdleWowCharacterMacros' or 'IdleWowAccountMacros'
    if type(_G[key])~='table' then _G[key]={rows={}} end
    if type(_G[key].rows)~='table' then _G[key].rows={} end
    return _G[key]
end
local function row(id)
    if type(id)=='string' then id=GetMacroIndexByName(id) end
    if type(id)~='number' or id<1 or id>54 or id~=math.floor(id) then return end
    return store(id>36).rows[id>36 and id-36 or id],id
end
function _IdleWowMacroByWire(uid)
    for _,character in ipairs({false,true}) do
        for i,value in ipairs(store(character).rows) do
            if value.uid==uid then return value,i+(character and 36 or 0) end
        end
    end
end
function GetNumMacros() return #store(false).rows,#store(true).rows end
function GetMacroIndexByName(name)
    for _,character in ipairs({false,true}) do
        for i,value in ipairs(store(character).rows) do if value.name==name then return i+(character and 36 or 0) end end
    end
    return 0
end
local function validate(name,body)
    if type(name)~='string' or #name==0 or _IdleWowTextLength(name)>16 then error('Macro name must contain 1 to 16 characters',3) end
    if type(body)~='string' or _IdleWowTextLength(body)>255 then error('Macro body exceeds 255 characters',3) end
end
local function icon(value)
    if type(value)=='number' then return GetMacroIconInfo(value) or GetMacroIconInfo(1) end
    if type(value)=='string' and #value>0 then return value end
    return GetMacroIconInfo(1)
end
function CreateMacro(name,image,body,character)
    character=character~=nil and character~=false and character~=0
    body=body or ''; validate(name,body)
    local rows=store(character).rows
    if #rows>=(character and 18 or 36) then error('Macro slots are full',2) end
    local account=store(false)
    local uid=math.max(4096,tonumber(account.nextUid) or 4096)
    if uid>=16777216 then error('Macro identifier space is exhausted',2) end
    account.nextUid=uid+1
    rows[#rows+1]={name=name,icon=icon(image),body=body,uid=uid}
    _IdleWowMacrosChanged()
    return #rows+(character and 36 or 0)
end
function EditMacro(id,name,image,body,character)
    local value,index=row(id); if not value then return end
    name=name or value.name; body=body or value.body; validate(name,body)
    if character~=nil then
        character=character~=false and character~=0
        if character~=(index>36) then
            local destination=store(character).rows
            if #destination>=(character and 18 or 36) then error('Macro slots are full',2) end
            table.remove(store(index>36).rows,index>36 and index-36 or index)
            destination[#destination+1]=value; index=#destination+(character and 36 or 0)
        end
    end
    value.name=name; value.body=body; if image~=nil then value.icon=icon(image) end
    _IdleWowMacrosChanged(); return index
end
function DeleteMacro(id)
    local value,index=row(id); if not value then return end
    table.remove(store(index>36).rows,index>36 and index-36 or index)
    _IdleWowMacrosChanged(value.uid)
end
function GetMacroBody(id) local value=row(id); return value and value.body end
function GetMacroSpell(id)
    local value=row(id); if not value then return end
    if value.spell then local name,rank=GetSpellInfo(value.spell); return name,rank,value.spell end
    for line in value.body:gmatch('[^\r\n]+') do
        local action=line:match('^%s*/cast%s+(.+)') or line:match('^%s*#showtooltip%s+(.+)')
        if action then
            action=SecureCmdOptionParse(action)
            local spell=action and _IdleWowResolveMacroSpell(action)
            if spell then local name,rank=GetSpellInfo(spell); return name,rank,spell end
        end
    end
end
function GetMacroItem(id)
    local value=row(id); if not value then return end
    local item=value.item
    if not item then for line in value.body:gmatch('[^\r\n]+') do
        local action=line:match('^%s*/use%s+(.+)')
        if action then item=SecureCmdOptionParse(action); if item then break end end
    end end
    if item then local id=_IdleWowResolveMacroItem(tostring(item)); if id then local name,link=GetItemInfo(id); return name,link end end
end
function SetMacroSpell(id,spell) local value=row(id); if value then value.spell=spell and _IdleWowResolveMacroSpell(tostring(spell)); _IdleWowMacrosChanged() end end
function SetMacroItem(id,item) local value=row(id); if value then value.item=item; _IdleWowMacrosChanged() end end
function GetMacroInfo(id)
    local value=row(id); if not value then return end
    local texture=value.icon
    if texture==GetMacroIconInfo(1) then
        local _,_,spell=GetMacroSpell(id)
        if spell then texture=select(3,GetSpellInfo(spell)) or texture
        else local item=GetMacroItem(id); if item then texture=select(10,GetItemInfo(item)) or texture end end
    end
    return value.name,texture,value.body
end
function PickupMacro(id) local value=row(id); if value then _IdleWowPickupMacro(value.uid) end end
function SaveMacros() _IdleWowSaveMacros() end
function GetMacroIconIndex(id) local value=row(id); if value then for i=1,GetNumMacroIcons() do if GetMacroIconInfo(i)==value.icon then return i end end end end

local function truth(value) return value~=nil and value~=false and value~=0 end
local function condition(text,target)
    local key,arg=text:match('^([^:]+):?(.*)$'); if not key then return false end
    if key:sub(1,1)=='@' then return true end
    if key=='target' then return true end
    local negate=key:sub(1,2)=='no'; if negate then key=key:sub(3) end
    local result
    if key=='exists' then result=UnitExists(target)
    elseif key=='help' then result=UnitCanAssist('player',target)
    elseif key=='harm' then result=UnitCanAttack('player',target)
    elseif key=='dead' then result=UnitIsDeadOrGhost(target)
    elseif key=='combat' then result=UnitAffectingCombat('player')
    elseif key=='mod' or key=='modifier' then
        if arg=='' then result=truth(IsShiftKeyDown()) or truth(IsControlKeyDown()) or truth(IsAltKeyDown())
        else for value in arg:gmatch('[^/]+') do if value=='shift' and truth(IsShiftKeyDown()) or value=='ctrl' and truth(IsControlKeyDown()) or value=='alt' and truth(IsAltKeyDown()) then result=true end end end
    elseif key=='stance' or key=='form' then
        local form=GetShapeshiftForm()
        if arg=='' then result=form>0 else for value in arg:gmatch('[^/]+') do if tonumber(value)==form then result=true end end end
    elseif key=='pet' then if arg~='' then return false end; result=UnitExists('pet')
    elseif key=='mounted' then result=IsMounted()
    elseif key=='stealth' then result=IsStealthed()
    elseif key=='swimming' then result=IsSwimming()
    elseif key=='flying' then result=IsFlying()
    elseif key=='indoors' then result=IsIndoors()
    elseif key=='outdoors' then result=IsOutdoors()
    elseif key=='group' then
        if arg=='raid' then result=GetNumRaidMembers()>0
        elseif arg=='party' then result=GetNumPartyMembers()>0 and GetNumRaidMembers()==0
        elseif arg=='' then result=GetNumPartyMembers()>0 or GetNumRaidMembers()>0
        else return false end
    else return false end
    result=truth(result); return negate and not result or not negate and result
end
function SecureCmdOptionParse(options)
    options=tostring(options or '')
    for clause in (options..';'):gmatch('(.-);') do
        clause=clause:match('^%s*(.-)%s*$')
        if clause:sub(1,1)~='[' then return clause end
        local groups={}
        while clause:sub(1,1)=='[' do
            local group,rest=clause:match('^%[(.-)%](.*)$')
            if not group then return end
            groups[#groups+1]=group; clause=rest:match('^%s*(.-)%s*$')
        end
        local action=clause
        for _,group in ipairs(groups) do
            local target=group:match('@([^,%s]+)') or group:match('target=([^,%s]+)') or 'target'
            local valid=true
            for value in group:gmatch('[^,]+') do
                value=value:match('^%s*(.-)%s*$')
                if not value:match('^target=') and not condition(value,target) then valid=false; break end
            end
            if valid then return action:match('^%s*(.-)%s*$'),target end
        end
    end
end

local running={}
function StopMacro() if running[#running] then running[#running].stop=true end end
function GetRunningMacro() return running[#running] and running[#running].index end
function RunScript(script)
    local fn,message=loadstring(script,'=MacroScript'); if not fn then error(message,2) end
    return fn()
end
local function execute(body,index)
    if type(body)~='string' then return end
    if #running>=8 then error('Macro recursion limit exceeded',2) end
    local current={index=index}; running[#running+1]=current
    local ok,message=pcall(function()
        for line in body:gmatch('[^\r\n]+') do
            if current.stop then break end
            line=line:match('^%s*(.-)%s*$')
            if line:sub(1,1)=='/' then _IdleWowExecuteMacroLine(line) end
        end
    end)
    running[#running]=nil
    if not ok then error(message,2) end
end
function RunMacro(id) local value,index=row(id); if value then execute(value.body,index) end end
function RunMacroText(body)
    if type(body)~='string' or _IdleWowTextLength(body)>1023 then error('Macro text exceeds 1023 characters',2) end
    execute(body)
end
function _IdleWowRunMacroWire(uid) local value,index=_IdleWowMacroByWire(uid); if not value then return false end; execute(value.body,index); return true end

local oldInfo,oldTexture,oldText,oldHas,oldCooldown,oldCount,oldUsable,oldCursor=GetActionInfo,GetActionTexture,GetActionText,HasAction,GetActionCooldown,GetActionCount,IsUsableAction,GetCursorInfo
local function actionMacro(slot)
    local kind,uid=oldInfo(slot)
    if kind=='macro' then local value,index=_IdleWowMacroByWire(uid); return value,index,true end
end
function GetActionInfo(slot)
    local value,index,macro=actionMacro(slot)
    if macro then if value then return 'macro',index end; return end
    return oldInfo(slot)
end
function HasAction(slot) local value,_,macro=actionMacro(slot); if macro then return value~=nil end; return oldHas(slot) end
function GetActionTexture(slot) local value,index,macro=actionMacro(slot); if macro then return value and select(2,GetMacroInfo(index)) end; return oldTexture(slot) end
function GetActionText(slot) local value,_,macro=actionMacro(slot); if macro then return value and value.name or '' end; return oldText(slot) end
function GetActionCooldown(slot)
    local value,index,macro=actionMacro(slot)
    if macro then local _,_,spell=GetMacroSpell(index); if spell then return GetSpellCooldown(spell) end; return 0,0,1 end
    return oldCooldown(slot)
end
function GetActionCount(slot) local value,index,macro=actionMacro(slot); if macro then local item=GetMacroItem(index); return item and GetItemCount(item) or 0 end; return oldCount(slot) end
function IsUsableAction(slot) local value,index,macro=actionMacro(slot); if macro then local _,_,spell=GetMacroSpell(index); if spell then return IsUsableSpell(spell) end; return value~=nil,false end; return oldUsable(slot) end
function GetCursorInfo() local kind,id,detail=oldCursor(); if kind=='macro' then local _,index=_IdleWowMacroByWire(id); return kind,index end; return kind,id,detail end
)LUA";
}
