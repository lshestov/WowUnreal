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

model = material('M_WowUiModel')
model_texture = node(model, unreal.MaterialExpressionTextureSampleParameter2D)
model_texture.set_editor_property('parameter_name', 'Image')
model_texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
edit.connect_material_property(model_texture, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
model_alpha = node(model, unreal.MaterialExpressionOneMinus)
link(model_texture, model_alpha, 'Input', 'A')
edit.connect_material_property(model_alpha, '', unreal.MaterialProperty.MP_OPACITY)

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

# Shared texture regions retain atlas coordinates and alpha while the GPU
# desaturates their pixels. Corners also support the stock 8-coordinate API.
regions = []
for name, blend_mode in [('M_WowUiRegion', unreal.BlendMode.BLEND_TRANSLUCENT),
                         ('M_WowUiRegionAdd', unreal.BlendMode.BLEND_ADDITIVE)]:
    region = material(name)
    region.set_editor_property('blend_mode', blend_mode)
    uv = node(region, unreal.MaterialExpressionTextureCoordinate)
    axes = []
    for axis in ['r', 'g']:
        mask = node(region, unreal.MaterialExpressionComponentMask)
        mask.set_editor_property(axis, True)
        link(uv, mask, 'Input')
        axes.append(mask)
    corners = []
    for key, x, y in [('UVTL', 0, 0), ('UVBL', 0, 1), ('UVTR', 1, 0), ('UVBR', 1, 1)]:
        vector = node(region, unreal.MaterialExpressionVectorParameter)
        vector.set_editor_property('parameter_name', key)
        vector.set_editor_property('default_value', unreal.LinearColor(x, y, 0, 0))
        mask = node(region, unreal.MaterialExpressionComponentMask)
        mask.set_editor_property('r', True)
        mask.set_editor_property('g', True)
        link(vector, mask, 'Input')
        corners.append(mask)
    sides = []
    for top, bottom in [(0, 1), (2, 3)]:
        side = node(region, unreal.MaterialExpressionLinearInterpolate)
        link(corners[top], side, 'A'); link(corners[bottom], side, 'B'); link(axes[1], side, 'Alpha')
        sides.append(side)
    coordinates = node(region, unreal.MaterialExpressionLinearInterpolate)
    link(sides[0], coordinates, 'A'); link(sides[1], coordinates, 'B'); link(axes[0], coordinates, 'Alpha')
    sample = node(region, unreal.MaterialExpressionTextureSampleParameter2D)
    sample.set_editor_property('parameter_name', 'Image')
    sample.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    link(coordinates, sample, 'UVs')
    desaturation = node(region, unreal.MaterialExpressionDesaturation)
    link(sample, desaturation, 'None', 'RGB')
    link(parameter(region, 'Desaturation', 0), desaturation, 'Fraction')
    edit.connect_material_property(desaturation, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    image_alpha = node(region, unreal.MaterialExpressionLinearInterpolate)
    link(sample, image_alpha, 'A', 'A'); link(number(region, 1), image_alpha, 'B')
    link(parameter(region, 'OpaqueImage', 0), image_alpha, 'Alpha')
    delta = centered_uv(region)
    distance = node(region, unreal.MaterialExpressionDotProduct)
    link(delta, distance, 'A'); link(delta, distance, 'B')
    inside = node(region, unreal.MaterialExpressionSubtract)
    link(number(region, .25), inside, 'A'); link(distance, inside, 'B')
    edge = node(region, unreal.MaterialExpressionMultiply)
    link(inside, edge, 'A'); link(number(region, 256), edge, 'B')
    circle = node(region, unreal.MaterialExpressionSaturate); link(edge, circle, 'Input')
    shape = node(region, unreal.MaterialExpressionLinearInterpolate)
    link(number(region, 1), shape, 'A'); link(circle, shape, 'B'); link(parameter(region, 'Circle', 0), shape, 'Alpha')
    alpha = node(region, unreal.MaterialExpressionMultiply)
    link(image_alpha, alpha, 'A'); link(shape, alpha, 'B')
    edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    regions.append(region)

for asset in ([model] if '-UiModelOnly' in unreal.SystemLibrary.get_command_line() else [image, model, cooldown, additive] + regions):
    edit.layout_material_expressions(asset)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save UI material ' + asset.get_name())
    unreal.log('UI_MATERIAL_SAVED: ' + asset.get_name())
