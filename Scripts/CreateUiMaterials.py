"""Cookable UI image masks and cooldown swipe; no game textures are embedded."""
import math
import unreal

edit = unreal.MaterialEditingLibrary


def material(name):
    asset = unreal.load_asset('/Game/Materials/' + name)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property('two_sided', True)
    return asset


def node(asset, kind):
    return edit.create_material_expression(asset, kind)


def link(source, target, pin, output=''):
    names = list(edit.get_material_expression_input_names(target))
    aliases = {'AGreaterThanB': 'A > B', 'AEqualsB': 'A == B', 'ALessThanB': 'A < B'}
    actual = aliases.get(pin, pin)
    if actual not in names and len(names) == 1:
        actual = names[0]
    if not edit.connect_material_expressions(source, output, target, actual):
        raise RuntimeError('Cannot connect ' + target.get_class().get_name() + '.' + pin + ': ' + str(names))


def number(asset, value):
    result = node(asset, unreal.MaterialExpressionConstant)
    result.set_editor_property('r', value)
    return result


def parameter(asset, name, value):
    result = node(asset, unreal.MaterialExpressionScalarParameter)
    result.set_editor_property('parameter_name', name)
    result.set_editor_property('default_value', value)
    return result


def centered_uv(asset):
    uv = node(asset, unreal.MaterialExpressionTextureCoordinate)
    delta = node(asset, unreal.MaterialExpressionSubtract)
    link(uv, delta, 'A')
    link(number(asset, .5), delta, 'B')
    return delta


image = material('M_WowUiImage')
texture = node(image, unreal.MaterialExpressionTextureSampleParameter2D)
texture.set_editor_property('parameter_name', 'Image')
texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
edit.connect_material_property(texture, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
delta = centered_uv(image)
distance = node(image, unreal.MaterialExpressionDotProduct)
link(delta, distance, 'A')
link(delta, distance, 'B')
inside = node(image, unreal.MaterialExpressionSubtract)
link(number(image, .25), inside, 'A')
link(distance, inside, 'B')
edge = node(image, unreal.MaterialExpressionMultiply)
link(inside, edge, 'A')
link(number(image, 256), edge, 'B')
mask = node(image, unreal.MaterialExpressionSaturate)
link(edge, mask, 'Input')
opacity = node(image, unreal.MaterialExpressionLinearInterpolate)
link(number(image, 1), opacity, 'A')
link(mask, opacity, 'B')
link(parameter(image, 'Circle', 1), opacity, 'Alpha')
rgb_sum = node(image, unreal.MaterialExpressionDotProduct)
link(texture, rgb_sum, 'A', 'RGB')
white = node(image, unreal.MaterialExpressionConstant3Vector)
white.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
link(white, rgb_sum, 'B')
brightness = node(image, unreal.MaterialExpressionSaturate)
link(rgb_sum, brightness, 'Input')
blend = node(image, unreal.MaterialExpressionLinearInterpolate)
link(number(image, 1), blend, 'A')
link(brightness, blend, 'B')
link(parameter(image, 'Additive', 0), blend, 'Alpha')
alpha = node(image, unreal.MaterialExpressionMultiply)
link(opacity, alpha, 'A')
link(blend, alpha, 'B')
edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)

cooldown = material('M_WowUiCooldown')
delta = centered_uv(cooldown)
x = node(cooldown, unreal.MaterialExpressionComponentMask)
x.set_editor_property('r', True)
link(delta, x, 'Input')
y = node(cooldown, unreal.MaterialExpressionComponentMask)
y.set_editor_property('g', True)
link(delta, y, 'Input')
negative_y = node(cooldown, unreal.MaterialExpressionMultiply)
link(y, negative_y, 'A')
link(number(cooldown, -1), negative_y, 'B')
angle = node(cooldown, unreal.MaterialExpressionArctangent2Fast)
link(x, angle, 'Y')
link(negative_y, angle, 'X')
turn = node(cooldown, unreal.MaterialExpressionMultiply)
link(angle, turn, 'A')
link(number(cooldown, 1 / (2 * math.pi)), turn, 'B')
positive_turn = node(cooldown, unreal.MaterialExpressionAdd)
link(turn, positive_turn, 'A')
link(number(cooldown, 1), positive_turn, 'B')
fraction = node(cooldown, unreal.MaterialExpressionFrac)
link(positive_turn, fraction, 'Input')
remaining = node(cooldown, unreal.MaterialExpressionIf)
link(fraction, remaining, 'A')
link(parameter(cooldown, 'Progress', 0), remaining, 'B')
link(number(cooldown, .6), remaining, 'AGreaterThanB')
link(number(cooldown, .6), remaining, 'AEqualsB')
link(number(cooldown, 0), remaining, 'ALessThanB')
edit.connect_material_property(number(cooldown, 0), '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.connect_material_property(remaining, '', unreal.MaterialProperty.MP_OPACITY)

additive = material('M_WowUiAdditive')
additive.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
additive_texture = node(additive, unreal.MaterialExpressionTextureSampleParameter2D)
additive_texture.set_editor_property('parameter_name', 'Image')
additive_texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
edit.connect_material_property(additive_texture, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.connect_material_property(additive_texture, 'A', unreal.MaterialProperty.MP_OPACITY)

for asset in [image, cooldown, additive]:
    edit.layout_material_expressions(asset)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save UI material ' + asset.get_name())
    unreal.log('UI_MATERIAL_SAVED: ' + asset.get_name())
