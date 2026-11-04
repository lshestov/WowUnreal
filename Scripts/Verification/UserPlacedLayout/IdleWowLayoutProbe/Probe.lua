-- Runs unchanged in the 3.3.5a reference client. All state below is transient;
-- restoration must come from the engine's layout-local.txt, not SavedVariables.
LayoutProbeDragCount = 0
local function make(name, x, y, w, h, template)
    local f = CreateFrame('Frame', name, UIParent, template)
    f:SetSize(w, h)
    f:SetPoint('TOPLEFT', UIParent, 'TOPLEFT', x, -y)
    f:SetMovable(true)
    f:SetResizable(true)
    f:SetFrameStrata('DIALOG')
    f:EnableMouse(true)
    f:RegisterForDrag('LeftButton', 'RightButton')
    f:SetBackdrop({bgFile='Interface\\Tooltips\\UI-Tooltip-Background', edgeFile='Interface\\Tooltips\\UI-Tooltip-Border', tile=true, tileSize=16, edgeSize=12})
    f:SetBackdropColor(.08, .2, .35, 1)
    local label = f:CreateFontString(nil, 'OVERLAY', 'GameFontNormalSmall')
    label:SetPoint('CENTER')
    label:SetText(name or 'Anonymous (not saved)')
    f:SetScript('OnDragStart', function(self, button)
        if button == 'RightButton' then self:StartSizing('BOTTOMRIGHT') else self:StartMoving() end
    end)
    f:SetScript('OnDragStop', function(self)
        self:StopMovingOrSizing()
        LayoutProbeDragCount = LayoutProbeDragCount + 1
    end)
    return f
end
LayoutProbeWindow = make('LayoutProbeWindow', 80, 60, 160, 80)
LayoutProbeNoSave = make('LayoutProbeNoSave', 300, 60, 80, 60, 'LayoutProbeNoSaveTemplate')
LayoutProbeAnonymous = make(nil, 80, 200, 100, 60)

function LayoutProbeNear(actual, expected)
    assert(type(actual)=='number' and math.abs(actual-expected)<.05, tostring(actual)..' expected '..expected)
end
function LayoutProbeExpect(f, x, y, w, h, placed)
    local scale = f:GetEffectiveScale() / UIParent:GetEffectiveScale()
    LayoutProbeNear(f:GetLeft()*scale, x)
    LayoutProbeNear(UIParent:GetHeight()-f:GetTop()*scale, y)
    LayoutProbeNear(f:GetWidth(), w)
    LayoutProbeNear(f:GetHeight(), h)
    assert((not not f:IsUserPlaced())==placed, 'IsUserPlaced differs')
    print(string.format('LAYOUT PASS %s: left=%.3f top=%.3f size=%.3fx%.3f placed=%s', f:GetName() or '<anonymous>', x,y,w,h,tostring(placed)))
end
function LayoutProbeReset()
    LayoutProbeWindow:SetUserPlaced(false)
    LayoutProbeWindow:ClearAllPoints()
    LayoutProbeWindow:SetPoint('TOPLEFT', UIParent, 'TOPLEFT', 80, -60)
    LayoutProbeWindow:SetSize(160,80)
end
