# Stock video controls

`extShadowQuality` controls Unreal's shadow scalability (0 through 4), and
`ffxGlow` independently controls the renderer's bloom switch. GetCVar reads the
live native values. Saved UI settings reapply the selected values on world entry
and ReloadUI; native quality changes preserve the separate glow preference.

The XML parser retains slider/status-bar orientation, including horizontal
orientation inherited from a virtual template. The options sliders therefore
respond to horizontal mouse movement and paint their thumbs on the same axis.
Runtime orientation overrides and the default vertical slider still work.

GetCVarMin/GetCVarMax expose known native limits. Other settings return nil so
the original FrameXML can use its own range. A universal 0..1 range is invalid
for view distance, player textures, shadows and most effects sliders.

Effects CVars have bounded initial values and defaults. Invalid saved values
from the old placeholder implementation are clamped before display/application,
avoiding spurious setting changes and restart prompts when merely opening the
panel. Character texture level defaults to 9, matching the renderer's 512x512
Wrath atlas. The current shadow default is Unreal quality 3. The other numeric
defaults follow the Wrath CVar registrations and FrameXML ranges.

The original current-category Defaults action resets all effects preferences;
Apply, Okay and Cancel keep their original FrameXML semantics. This does not
establish renderer support for every effects CVar: particle/ground-effect
density, distant-object detail, filtering, weather and several texture options
still need their own engine implementations. Their stored values and valid UI
ranges must not be confused with completed visual features.

Regression coverage includes actual renderer CVars, native quality values,
independent glow, defaults, invalid old preferences and inherited horizontal
pointer input. Rendered verification uses the unmodified stock options window.

Verified on Windows: Editor and packaged builds; 148 passing regression checks
(four existing warning cases); a 46-step disposable-account route through the
original options window. Five FScreenshotRequest captures were validated and
visually inspected. The route covers both range endpoints, glow on/off, Apply,
Okay, Cancel, current-category Defaults and persistence across ReloadUI. The
test client exits and its owned account is removed after verification.
