"""Create the original-data local-light falloff; contains no game assets."""
import unreal

editing = unreal.MaterialEditingLibrary
path = '/Game/Materials/M_WowM2LightFunction'
asset = unreal.load_asset(path)
if asset is None:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_WowM2LightFunction', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
editing.delete_all_material_expressions(asset)
asset.set_editor_property('material_domain', unreal.MaterialDomain.MD_LIGHT_FUNCTION)
# Absolute world position varies per pixel and must not be baked into the atlas.
asset.set_editor_property('force_compatible_with_light_function_atlas', False)

def node(kind):
    return editing.create_material_expression(asset, kind)

def connect(source, target, name):
    if not editing.connect_material_expressions(source, '', target, name):
        raise RuntimeError('Cannot connect ' + name)

falloff = node(unreal.MaterialExpressionCustom)
falloff.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
names = ['Position', 'Source', 'WorldScale', 'Start', 'End', 'Radius', 'AuthoredRange']
inputs = []
for name in names:
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
falloff.set_editor_property('inputs', inputs)
falloff.set_editor_property('code', '''
float d = length(Position - Source) / max(WorldScale, 0.001);
float authored = saturate((End - d) / max(End - Start, 0.001));
float classic = rcp(max(1.0, 0.7*d + 0.03*d*d));
// Smooth only the final tenth of the finite default rendering radius.
classic *= saturate((Radius - d) / max(Radius * 0.1, 0.001));
return lerp(classic, authored, AuthoredRange);
''')
connect(node(unreal.MaterialExpressionWorldPosition), falloff, 'Position')
source = node(unreal.MaterialExpressionVectorParameter)
source.set_editor_property('parameter_name', 'SourcePosition')
connect(source, falloff, 'Source')
for name, value in [('WorldScale', 100), ('Start', 0), ('End', 10), ('Radius', 10), ('AuthoredRange', 1)]:
    parameter = node(unreal.MaterialExpressionScalarParameter)
    parameter.set_editor_property('parameter_name', name)
    parameter.set_editor_property('default_value', value)
    connect(parameter, falloff, name)
if not editing.connect_material_property(falloff, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('Cannot connect falloff output')
editing.layout_material_expressions(asset)
editing.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('M2_LIGHT_FUNCTION_SAVED: ' + path)
