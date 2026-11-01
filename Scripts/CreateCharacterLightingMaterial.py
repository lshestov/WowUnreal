"""Cookable character material with floor-sampled WMO ambient, no game assets."""
import unreal

edit = unreal.MaterialEditingLibrary
path = '/Game/Materials/M_WowCharacter'
asset = unreal.load_asset(path)
if asset is None:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_WowCharacter', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(asset)
asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
asset.set_editor_property('two_sided', True)
asset.set_editor_property('used_with_skeletal_mesh', True)

def node(kind):
    return edit.create_material_expression(asset, kind)

def link(source, output, target, input_name):
    names = list(edit.get_material_expression_input_names(target))
    actual = names[0] if input_name not in names and len(names) == 1 else input_name
    if not edit.connect_material_expressions(source, output, target, actual):
        raise RuntimeError('Cannot connect ' + target.get_class().get_name() + '.' + input_name)

def prop(source, output, target):
    if not edit.connect_material_property(source, output, target):
        raise RuntimeError('Cannot connect material property ' + str(target))

tex = node(unreal.MaterialExpressionTextureSampleParameter2D)
tex.set_editor_property('parameter_name', 'BaseTexture')
tex.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
prop(tex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
prop(tex, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
ambient = node(unreal.MaterialExpressionVectorParameter)
ambient.set_editor_property('parameter_name', 'WowInteriorAmbient')
ambient.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 1))
emission = node(unreal.MaterialExpressionMultiply)
link(tex, 'RGB', emission, 'A')
link(ambient, '', emission, 'B')
prop(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
for name, value, target in [('Roughness', 1, unreal.MaterialProperty.MP_ROUGHNESS),
                            ('Specular', 0, unreal.MaterialProperty.MP_SPECULAR),
                            ('Metallic', 0, unreal.MaterialProperty.MP_METALLIC)]:
    scalar = node(unreal.MaterialExpressionScalarParameter)
    scalar.set_editor_property('parameter_name', name)
    scalar.set_editor_property('default_value', value)
    prop(scalar, '', target)
edit.layout_material_expressions(asset)
edit.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('CHARACTER_LIGHTING_MATERIAL_SAVED: ' + path)
