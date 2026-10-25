# Stock 3.3.5a interface reference

Target: the original Wrath client, build 12340. Modern Retail and Wrath Classic
screenshots are not layout specifications for this client.

## Internet screenshot references

- [Default WoW UI in Vadim Vaitenko's portfolio](https://vaitenko.com/wow/),
  [full-size image](https://vaitenko.com/wow/images/defaultUI-1680.webp).
  The author explicitly labels this image as the default UI. Use the upper-left
  player/target frames, round minimap, bottom gryphon bar, micro menu and bag
  positions as a visual comparison. The screenshot does not identify its exact
  client build; do not derive patch-specific API behavior from it.
- [BlizzMove 3.3.5 screenshot](https://dlcfun.com/en/world-of-warcraft/blizzmove-335-ikonki-interfeisa-dlia-upravleniia-dopolneniiami),
  [image](https://storage.dlcfun.com/dlcfun-public/images/addons/3b0a5fb9b94d3adc9471969391a53e5c.jpg).
  The old character pane has two statistic columns and Character/Pets/Reputation/
  Skills/Currency tabs; the spellbook has a rank checkbox and Spellbook/Glyphs
  tabs. These are useful artwork/content references. BlizzMove changes window
  positions, so this image is not a positioning reference.
- [Quest Log Off, published July 23, 2010 for 3.3.5](https://www.wowinterface.com/downloads/info17466-QuestLogOff.html).
  This is a dated screenshot source, but the author's unit frames and minimap
  are modified. Do not reproduce those replacements as stock Wrath UI.

Search results frequently mix patches and addons. Check the actual image,
publication context and visible controls rather than trusting its search title.
No reference images or Blizzard artwork are distributed in the repository.

## Exact layout and behavior

Read the original FrameXML/Lua from the user's MPQ files at runtime. Its anchors,
dimensions and scripts take precedence over measurements inferred from a web
screenshot. In particular:

- FrameXML owns each frame's Show/Hide state. `IsShown` describes that frame;
  `IsVisible` also considers its ancestors.
- Showing a parent preserves explicitly hidden children and hidden branches.
  Layout refresh and bootstrap reconciliation must preserve those states.
- Action and bonus buttons are shown by the original action-bar scripts.
  Additional bars follow their options, and empty slots follow grid visibility.
- Texture names such as `Flash` do not imply a fixed visibility state.
- An explicit XML `hidden="false"` overrides a hidden inherited template.
- Runtime `SetPoint` calls may omit `relativePoint` while specifying a relative
  frame and offsets. `$parent` in a region's anchor names its owning frame.
- A FontString's explicit height does not disable wrapping at its width.

`WowUnreal.Hud.StockFrameVisibility` covers the hierarchy, stock frame names,
flash regions and explicit template overrides. Native probes additionally check
the original action-bar option controls, window clicks and rendered screenshots.
`WowUnreal.Hud.StockOptionsAnchors` covers option-row placement, runtime parent
references and constrained description text.
