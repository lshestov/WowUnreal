-- 3.3.5a addon listener. Run on a disposable QA character alongside Blizzard_CombatLog.
-- Preserve nil holes and the exact tuple length: #row is not an argument count.
if IdleCombatProbe then IdleCombatProbe:Stop() end
local probe = {all={}, filtered={}, errors={}}
IdleCombatProbe = probe
local function pack(...) return {n=select('#', ...), ...} end
local function same(a,b)
    assert(a.n==b.n, 'combat tuple length differs')
    for i=1,a.n do
        assert(a[i]==b[i] and type(a[i])==type(b[i]), 'combat tuple field '..i..' differs')
    end
end
local frame=CreateFrame('Frame')
probe.frame=frame
for _,event in ipairs({'COMBAT_LOG_EVENT_UNFILTERED','COMBAT_LOG_EVENT','UI_ERROR_MESSAGE'}) do frame:RegisterEvent(event) end
frame:SetScript('OnEvent',function(self,event,...)
    if event=='UI_ERROR_MESSAGE' then table.insert(probe.errors,pack(...)); return end
    local row=pack(...)
    same(row,pack(CombatLogGetCurrentEventInfo()))
    assert(type(row[1])=='number' and type(row[2])=='string')
    assert(type(row[3])=='string' and row[3]:match('^0x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x$'))
    assert(type(row[6])=='string' and row[6]:match('^0x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x$'))
    assert(type(row[5])=='number' and type(row[8])=='number')
    table.insert(event=='COMBAT_LOG_EVENT_UNFILTERED' and probe.all or probe.filtered,row)
end)
function probe:Begin(events,spell,source,destination)
    self.all={}; self.filtered={}; self.errors={}
    CombatLogResetFilter()
    if events~=nil then CombatLogAddFilter(events,source or UnitGUID('player'),destination,spell) end
end
function probe:Count(event,spell,filtered)
    local n=0
    for _,row in ipairs(filtered and self.filtered or self.all) do
        if row[2]==event and (spell==nil or row[9]==spell) then n=n+1 end
    end
    return n
end
function probe:Expect(event,spell,count,filtered)
    assert(self:Count(event,spell,filtered)==count, event..' unexpected count')
end
function probe:CheckHistory()
    -- History can include older rows; compare the suffix delivered to this listener.
    assert(CombatLogGetNumEntries(true)>=#self.all)
    if #self.all==0 then return end
    assert(CombatLogSetCurrentEntry(0,true))
    for i=#self.all,1,-1 do
        same(self.all[i],pack(CombatLogGetCurrentEntry()))
        if i>1 then assert(CombatLogAdvanceEntry(-1,true)) end
    end
end
function probe:Stop()
    self.frame:UnregisterAllEvents()
    self.frame:SetScript('OnEvent',nil)
    CombatLogResetFilter()
end
