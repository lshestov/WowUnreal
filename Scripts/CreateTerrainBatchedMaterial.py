"""Cookable terrain shader. Only a recipe: original textures stay in the MPQs."""
import unreal

edit = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
collection_name = 'MPC_WowTerrainLight'
collection_path = '/Game/Materials/' + collection_name
if unreal.EditorAssetLibrary.does_asset_exist(collection_path):
    collection = unreal.load_asset(collection_path)
else:
    collection = assets.create_asset(collection_name, '/Game/Materials', unreal.MaterialParameterCollection,
                                     unreal.MaterialParameterCollectionFactoryNew())
shadow_name = 'TerrainUseBakedShadows'
if shadow_name not in [str(name) for name in collection.get_scalar_parameter_names()]:
    shadow_parameter = unreal.CollectionScalarParameter()
    shadow_parameter.set_editor_property('parameter_name', shadow_name)
    shadow_parameter.set_editor_property('default_value', 0.0)
    collection.set_editor_property('scalar_parameters', [shadow_parameter])
    if not unreal.EditorAssetLibrary.save_loaded_asset(collection):
        raise RuntimeError('Cannot save ' + collection_path)

path = '/Game/Materials/M_WowTerrainBatched'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    asset = unreal.load_asset(path)
else:
    asset = assets.create_asset(
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
baked = node(unreal.MaterialExpressionCollectionParameter)
baked.set_editor_property('collection', collection)
baked.set_editor_property('parameter_name', shadow_name)
ground = node(unreal.MaterialExpressionCustom)
ground.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
ground.set_editor_property('description', 'Blend terrain textures and baked shadows')
ground.set_editor_property('include_file_paths', ['/Engine/Private/GammaCorrectionCommon.ush'])
pins = [(f'Layer{i}', layer, 'RGB') for i, layer in enumerate(layers)]
pins.extend([('Weights', alpha, 'RGB'), ('Visibility', alpha, 'A'), ('UseBaked', baked, '')])
inputs = []
for name, source, output in pins:
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
ground.set_editor_property('inputs', inputs)
# Terrain1 blends BLP samples and the MCSH mask in display RGB. Terrain3's
# environmental shadow mode uses its shadow maps in place of the baked mask.
ground.set_editor_property('code', '''
float3 Color = lerp(LinearToSrgb(Layer0), LinearToSrgb(Layer1), Weights.r);
Color = lerp(Color, LinearToSrgb(Layer2), Weights.g);
Color = lerp(Color, LinearToSrgb(Layer3), Weights.b);
return sRGBToLinear(Color * (1.0 - 0.3 * UseBaked * (1.0 - Visibility)));
''')
for name, source, output in pins:
    link(source, output, ground, name)
if not edit.connect_material_property(ground, '', unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError('Cannot connect base colour')
# A UE shadow map only attenuates direct sunlight. The stock terrain shadow
# also attenuates the ambient band. Use the ADT canopy mask for local sky
# visibility when dynamic shadows are active; the low mode already applies
# the mask to the whole surface above.
sky = node(unreal.MaterialExpressionCustom)
sky.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
sky_inputs = []
for name in ('Visibility', 'UseBaked'):
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    sky_inputs.append(entry)
sky.set_editor_property('inputs', sky_inputs)
sky.set_editor_property('include_file_paths', ['/Engine/Private/GammaCorrectionCommon.ush'])
sky.set_editor_property('code', 'return sRGBToLinear(0.7 + 0.3 * Visibility).r * (1.0 - UseBaked) + UseBaked;')
link(alpha, 'A', sky, 'Visibility')
link(baked, '', sky, 'UseBaked')
if not edit.connect_material_property(sky, '', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION):
    raise RuntimeError('Cannot connect terrain sky visibility')
for value, prop in [(1, unreal.MaterialProperty.MP_ROUGHNESS), (0, unreal.MaterialProperty.MP_SPECULAR)]:
    constant = node(unreal.MaterialExpressionConstant)
    constant.set_editor_property('r', value)
    edit.connect_material_property(constant, '', prop)
edit.layout_material_expressions(asset)
edit.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('TERRAIN_BATCHED_MATERIAL_SAVED: ' + path)
