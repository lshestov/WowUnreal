"""Cookable terrain shader. Only a recipe: original textures stay in the MPQs."""
import unreal

edit = unreal.MaterialEditingLibrary
path = '/Game/Materials/M_WowTerrainBatched'
asset = unreal.load_asset(path)
if asset is None:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_WowTerrainBatched', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(asset)
asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
asset.set_editor_property('two_sided', True)


def node(kind):
    return edit.create_material_expression(asset, kind)


def link(source, output, target, name):
    if not edit.connect_material_expressions(source, output, target, name):
        raise RuntimeError('Cannot connect ' + name)


uvs = []
for channel in range(3):
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    uv.set_editor_property('coordinate_index', channel)
    uvs.append(uv)
atlas_uv = node(unreal.MaterialExpressionCustom)
atlas_uv.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT2)
inputs = []
for name in ['Local', 'Chunk']:
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
atlas_uv.set_editor_property('inputs', inputs)
# Clamp per fragment, not per vertex: exactly the old 64x64 clamp sampler at
# chunk boundaries, including the half-texel constant edge. Atlas has one mip.
atlas_uv.set_editor_property('code',
    'return (Chunk*64.0 + clamp(Local, 0.5/64.0, 63.5/64.0)*64.0)/1024.0;')
link(uvs[0], '', atlas_uv, 'Local')
link(uvs[2], '', atlas_uv, 'Chunk')
alpha = node(unreal.MaterialExpressionTextureSampleParameter2D)
alpha.set_editor_property('parameter_name', 'AlphaAtlas')
alpha.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/Black'))
link(atlas_uv, '', alpha, 'UVs')
layers = []
for i in range(4):
    layer = node(unreal.MaterialExpressionTextureSampleParameter2D)
    layer.set_editor_property('parameter_name', f'Layer{i}Texture')
    layer.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    link(uvs[1], '', layer, 'UVs')
    layers.append(layer)
result = layers[0]
for i, channel in enumerate(['R', 'G', 'B']):
    blend = node(unreal.MaterialExpressionLinearInterpolate)
    link(result, 'RGB' if i == 0 else '', blend, 'A')
    link(layers[i+1], 'RGB', blend, 'B')
    link(alpha, channel, blend, 'Alpha')
    result = blend
if not edit.connect_material_property(result, '', unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError('Cannot connect base colour')
for value, prop in [(1, unreal.MaterialProperty.MP_ROUGHNESS), (0, unreal.MaterialProperty.MP_SPECULAR)]:
    constant = node(unreal.MaterialExpressionConstant)
    constant.set_editor_property('r', value)
    edit.connect_material_property(constant, '', prop)
edit.layout_material_expressions(asset)
edit.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('TERRAIN_BATCHED_MATERIAL_SAVED: ' + path)
