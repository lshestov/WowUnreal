-- Run with the production macro runtime and QAMacroCommands loaded from the
-- user's build-12340 ChatFrame.lua. Native unit-state queries are tested separately.
local checks = 0
local units = {player={help=true}, target={harm=true}, focus={help=true}, party1={help=true,party=true}, raid1={help=true,raid=2}}
local state = {form=0,bar=1,bonus=0,spec=1,button='LeftButton',party=0,raid=0}
UnitExists=function(unit) return units[unit] ~= nil end
UnitCanAssist=function(_,unit) return units[unit] and units[unit].help end
UnitCanAttack=function(_,unit) return units[unit] and units[unit].harm end
UnitIsDeadOrGhost=function(unit) return units[unit] and units[unit].dead end
UnitIsUnit=function(a,b) return a==b and units[a]~=nil end
UnitInParty=function(unit) return unit=='player' or units[unit] and units[unit].party end
UnitInRaid=function(unit) return units[unit] and units[unit].raid end
UnitAffectingCombat=function() return state.combat end
IsShiftKeyDown=function() return state.shift end
IsControlKeyDown=function() return state.ctrl end
IsAltKeyDown=function() return state.alt end
IsModifiedClick=function(action) return state[action] end
GetShapeshiftForm=function() return state.form end
UnitName=function(unit) return units[unit] and units[unit].name end
UnitCreatureFamily=function(unit) return units[unit] and units[unit].family end
IsFlyableArea=function() return state.flyable end
_IdleWowMacroButton=function() return state.button end
GetBonusBarOffset=function() return state.bonus end
GetActionBarPage=function() return state.bar end
GetActiveTalentGroup=function() return state.spec end
IsEquippedItemType=function(kind) return kind=='Shields' and state.shield end
UnitChannelInfo=function() return state.channel end
GetSpellInfo=function(id) if id==740 then return 'Tranquility' end end
UnitHasVehicleUI=function(unit) return units[unit] and units[unit].vehicle end
GetCursorInfo=function() return state.cursor end
IsMounted=function() return state.mounted end
IsStealthed=function() return state.stealth end
IsSwimming=function() return state.swimming end
IsFlying=function() return state.flying end
IsIndoors=function() return state.indoors end
IsOutdoors=function() return not state.indoors end
GetNumPartyMembers=function() return state.party end
GetNumRaidMembers=function() return state.raid end

local function expect(options, action, target)
    local actual, unit=SecureCmdOptionParse(options)
    assert(actual==action and unit==target, string.format('%q: got %s/%s, expected %s/%s',options,tostring(actual),tostring(unit),tostring(action),tostring(target)))
    checks=checks+1
end
expect('', '')
expect('[] Heal', 'Heal')
expect('[nomod] Heal', 'Heal')
expect('[harm,nodead] Strike; Heal', 'Strike')
expect('[help] Heal; [harm] Strike', 'Strike')
expect('[@focus,help,nodead][@player] Heal', 'Heal','focus')
expect('[help,nodead,@focus] Heal', 'Heal','focus')
expect('[target=focus,help] Heal', 'Heal','focus')
expect('[@missing,help][@player,help] Heal', 'Heal','player')
expect('[@missing,exists] Hidden; [] Default', 'Default')
expect('[@missing,nohelp] Neutral', 'Neutral','missing')
expect('[@missing,harm] Hidden', nil)
expect('[unknown] Hidden', nil)
expect('[nounknown] Hidden', nil)
expect('[combat] Hidden', nil)
expect('[mod:shift][combat] Hidden', nil)
expect('[nomod,combat] Hidden; [@player] Heal', 'Heal','player')
units.focus.dead=true
expect('[@focus,help,nodead][@player,help] Heal', 'Heal','player')
expect('[@focus,dead] Resurrect', 'Resurrect','focus')
units.focus.dead=nil
expect('[@player,party] OtherPlayer; [@party1,party] Party', 'Party','party1')
expect('[@raid1,raid] Raid', 'Raid','raid1')
expect('[group] Group; Solo', 'Solo')
state.party=2
expect('[group:party/raid] Group', 'Group')
expect('[nogroup:party/raid] Solo', nil)
state.raid=10
expect('[group:party] Party; [group:raid] Raid', 'Raid')
expect('[group:party/raid] Group', 'Group')
state.party=0; state.raid=0
expect('[nogroup:party/raid] Solo', 'Solo')
state.shift=true
expect('[mod:SHIFT/CTRL] Modified', 'Modified')
expect('[nomod:ctrl/alt] Allowed', 'Allowed')
expect('[mod:shift,mod:ctrl] Both', nil)
state.ctrl=true
expect('[mod:shift,mod:ctrl] Both', 'Both')
expect('[nomod:shift/ctrl] Hidden', nil)
state.SELFCAST=true
expect('[mod:SELFCAST] Self', 'Self')
expect('[nomod:SELFCAST] Hidden', nil)
state.shift=false; state.ctrl=false
expect('[nomod] Plain', 'Plain')
state.form=3
expect('[stance:1/3,noform:2] Feral', 'Feral')
state.form=0
expect('[form:0] Humanoid', 'Humanoid')
expect('[nostance] Humanoid', 'Humanoid')
expect('[pet] Pet', nil)
units.pet={name='Snuffles',family='Wolf'}
expect('[pet:Cat/Wolf] Pet', 'Pet')
expect('[pet:snuffles] Pet', 'Pet')
expect('[nopet:Cat/Wolf] Hidden', nil)
state.button='RightButton'
expect('[button:1/2] Click', 'Click')
expect('[nobtn:2] Hidden', nil)
state.bar=2; state.bonus=1; state.spec=2
expect('[actionbar:1/2,bonusbar:1,spec:2] Page', 'Page')
state.channel='Tranquility'
expect('[channeling:740] Busy', 'Busy')
expect('[nochanneling:Tranquility] Hidden', nil)
state.shield=true
expect('[equipped:Daggers/Shields] Shield', 'Shield')
expect('[noworn:Shields] Hidden', nil)
units.player.vehicle=true; units.focus.vehicle=true
expect('[vehicleui,@player] Vehicle', 'Vehicle','player')
expect('[@focus,unithasvehicleui] Passenger', 'Passenger','focus')
state.cursor='item'
expect('[cursor:item] Item', 'Item')
expect('[nocursor] Hidden', nil)
for _,flag in ipairs({'mounted','stealth','swimming','flying','flyable','indoors'}) do
    state[flag]=true
    expect('['..flag..'] Yes', 'Yes')
    expect('[no'..flag..'] Hidden', nil)
    state[flag]=false
    expect('[no'..flag..'] No', 'No')
end
expect('[outdoors] Outside', 'Outside')

-- These are the actual unmodified stock handlers, with only their native
-- request boundary recorded. A failed request must not try the next clause.
assert(type(QAMacroCommands)=='table', 'Load build-12340 ChatFrame handlers first')
strmatch=string.match
local requests={}
local function record(kind, action, target)
    requests[#requests+1]={kind,action,target}
    return false -- emulate a rejected request, without changing parser selection
end
CastSpellByName=function(action,target) return record('cast',action,target) end
GetItemInfo=function(name) if name=='Hearthstone' then return name end end
GetInventoryItemLink=function(_,slot) return 'Inventory'..slot end
UseItemByName=function(action,target) return record('item',action,target) end
UseInventoryItem=function(slot,target) return record('inventory',tonumber(slot),target) end
AssistUnit=function(target) record('assist',nil,target) end
FocusUnit=function(target) record('focus',nil,target) end
SpellStopCasting=function() record('stopcasting') end
local function command(name, text, kind, action, target)
    local before=#requests
    QAMacroCommands[name](text)
    assert(#requests==before+(kind and 1 or 0), name..': unexpected dispatch count')
    if kind then
        local row=requests[#requests]
        assert(row[1]==kind and row[2]==action and row[3]==target, name..': wrong native request/target')
    end
    checks=checks+1
end
command('CAST','[@focus,help,nodead][@player] Heal','cast','Heal','focus')
command('CAST','[nomod] Heal','cast','Heal')
command('CAST','[help] Heal; [harm] Strike','cast','Strike')
command('CAST','[@missing,exists] Hidden',nil)
command('CAST','[@missing,help][@focus,harm] Hidden',nil)
command('CAST','[@player] Rejected; Fallback','cast','Rejected','player')
command('USE','[@player] Hearthstone','item','Hearthstone','player')
command('USE','[nomod] Hearthstone','item','Hearthstone')
command('USE','[@focus,help] 13','inventory',13,'focus')
command('USE','[@missing,exists] Hearthstone',nil)
state.combat=true
command('ASSIST','[combat] Tank','assist',nil,'Tank')
command('ASSIST','[@focus,help] Tank','assist',nil,'focus')
command('FOCUS','[@party1,help]','focus',nil,'party1')
command('STOPCASTING','[combat]','stopcasting')
state.combat=false
command('STOPCASTING','[combat]',nil)
command('STOPCASTING','[]','stopcasting')

local trace=0
_IdleWowExecuteMacroLine=function(line)
    local name,text=line:match('^/(%S+)%s*(.*)$')
    if name=='run' then trace=trace+1 else QAMacroCommands[name:upper()](text) end
end
RunMacroText('/run trace\n/stopmacro [combat]\n/run trace')
assert(trace==2, 'false stopmacro must continue')
state.combat=true
RunMacroText('/run trace\n/stopmacro [combat]\n/run trace')
assert(trace==3 and GetRunningMacro()==nil, 'true stopmacro must stop and unwind')
checks=checks+2
print('MacroConditions: '..checks..' checks passed with original ChatFrame handlers')
