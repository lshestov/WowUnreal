"""Create the cookable environment materials used by WowSkyManager.

Run UnrealEditor-Cmd with -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
-run=pythonscript -script=<absolute path to this file>.
These graphs contain no game textures or other Blizzard assets.
"""
import unreal

editing = unreal.MaterialEditingLibrary


def material(name, blend):
    path = '/Game/Materials/' + name
    asset = unreal.load_asset(path)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    editing.delete_all_material_expressions(asset)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('blend_mode', blend)
    asset.set_editor_property('two_sided', True)
    return asset


def vector(asset, name, color):
    node = editing.create_material_expression(asset, unreal.MaterialExpressionVectorParameter)
    node.set_editor_property('parameter_name', name)
    node.set_editor_property('default_value', unreal.LinearColor(*color))
    return node


def blend(asset, a, b, alpha):
    node = editing.create_material_expression(asset, unreal.MaterialExpressionLinearInterpolate)
    editing.connect_material_expressions(a, '', node, 'A')
    editing.connect_material_expressions(b, '', node, 'B')
    editing.connect_material_expressions(alpha, 'R', node, 'Alpha')
    return node


sky = material('M_WowSky', unreal.BlendMode.BLEND_OPAQUE)
sky.set_editor_property('is_sky', True)
top = vector(sky, 'SkyTopColor', (0.2, 0.4, 0.8, 1.0))
middle = vector(sky, 'SkyMiddleColor', (0.4, 0.6, 1.0, 1.0))
horizon = vector(sky, 'SkyHorizonColor', (0.7, 0.75, 0.85, 1.0))
band = vector(sky, 'SkyBand1Color', (0.5, 0.6, 0.9, 1.0))
smog = vector(sky, 'SkySmogColor', (0.6, 0.65, 0.7, 1.0))
height = editing.create_material_expression(sky, unreal.MaterialExpressionVertexColor)
lower = blend(sky, horizon, band, height)
upper = blend(sky, middle, top, height)
gradient = blend(sky, lower, upper, height)
editing.connect_material_property(gradient, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

cloud = material('M_WowCloud', unreal.BlendMode.BLEND_TRANSLUCENT)
color = vector(cloud, 'CloudColor', (0.9, 0.92, 0.95, 1.0))
opacity = editing.create_material_expression(cloud, unreal.MaterialExpressionScalarParameter)
opacity.set_editor_property('parameter_name', 'CloudOpacity')
opacity.set_editor_property('default_value', 0.3)
editing.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
editing.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)

disc = material('M_WowCelestialDisc', unreal.BlendMode.BLEND_ADDITIVE)
color = vector(disc, 'DiscColor', (1.0, 1.0, 1.0, 1.0))
editing.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

distant = material('M_WowDistantTerrain', unreal.BlendMode.BLEND_OPAQUE)
distant.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
color = vector(distant, 'BaseColor', (0.3, 0.4, 0.2, 1.0))
editing.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
roughness = editing.create_material_expression(distant, unreal.MaterialExpressionConstant)
roughness.set_editor_property('r', 1.0)
editing.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)

liquids = []
for name, masked, glow in [('M_WowWater', True, 0.0), ('M_WowLava', False, 2.0), ('M_WowSlime', False, 0.25)]:
    liquid = material(name, unreal.BlendMode.BLEND_MASKED if masked else unreal.BlendMode.BLEND_OPAQUE)
    liquid.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    liquid.set_editor_property('opacity_mask_clip_value', 0.1)
    tint = editing.create_material_expression(liquid, unreal.MaterialExpressionVertexColor)
    uv = editing.create_material_expression(liquid, unreal.MaterialExpressionTextureCoordinate)
    panner = editing.create_material_expression(liquid, unreal.MaterialExpressionPanner)
    panner.set_editor_property('speed_x', 0.02)
    panner.set_editor_property('speed_y', 0.015)
    editing.connect_material_expressions(uv, '', panner, 'Coordinate')
    noise = editing.create_material_expression(liquid, unreal.MaterialExpressionNoise)
    noise.set_editor_property('scale', 8.0)
    noise.set_editor_property('levels', 2)
    editing.connect_material_expressions(panner, '', noise, 'Position')
    variation = editing.create_material_expression(liquid, unreal.MaterialExpressionMultiply)
    variation.set_editor_property('const_b', 0.08)
    editing.connect_material_expressions(noise, '', variation, 'A')
    color = editing.create_material_expression(liquid, unreal.MaterialExpressionAdd)
    editing.connect_material_expressions(tint, '', color, 'A')
    editing.connect_material_expressions(variation, '', color, 'B')
    editing.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    if masked:
        editing.connect_material_property(tint, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    roughness = editing.create_material_expression(liquid, unreal.MaterialExpressionConstant)
    roughness.set_editor_property('r', 0.15 if masked else 0.5)
    editing.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    if glow:
        emissive = editing.create_material_expression(liquid, unreal.MaterialExpressionMultiply)
        emissive.set_editor_property('const_b', glow)
        editing.connect_material_expressions(tint, '', emissive, 'A')
        editing.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    liquids.append(liquid)

for asset in (sky, cloud, disc, distant, *liquids):
    editing.layout_material_expressions(asset)
    editing.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save ' + asset.get_path_name())
    unreal.log('WORLD_ENVIRONMENT_MATERIAL_SAVED: ' + asset.get_path_name())
