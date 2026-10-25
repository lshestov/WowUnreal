"""Create a cookable WMO material; game textures remain in the user's MPQs."""
import unreal

editing = unreal.MaterialEditingLibrary
path = '/Game/Materials/M_WowWmo'
asset = unreal.load_asset(path)
if asset is None:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_WowWmo', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
editing.delete_all_material_expressions(asset)
asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
asset.set_editor_property('two_sided', True)


def node(kind):
    return editing.create_material_expression(asset, kind)


def connect(source, output, target, input_name):
    editing.connect_material_expressions(source, output, target, input_name)


def scalar(name, value):
    result = node(unreal.MaterialExpressionScalarParameter)
    result.set_editor_property('parameter_name', name)
    result.set_editor_property('default_value', value)
    return result


texture = node(unreal.MaterialExpressionTextureSampleParameter2D)
texture.set_editor_property('parameter_name', 'Layer0Texture')
texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
vertex = node(unreal.MaterialExpressionVertexColor)
white = node(unreal.MaterialExpressionConstant3Vector)
white.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
tint = node(unreal.MaterialExpressionLinearInterpolate)
connect(white, '', tint, 'A')
connect(vertex, 'RGB', tint, 'B')
connect(scalar('BakedLightStrength', 0), '', tint, 'Alpha')
surface = node(unreal.MaterialExpressionMultiply)
connect(texture, 'RGB', surface, 'A')
connect(tint, '', surface, 'B')
editing.connect_material_property(surface, '', unreal.MaterialProperty.MP_BASE_COLOR)
editing.connect_material_property(texture, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
fill = node(unreal.MaterialExpressionMultiply)
connect(surface, '', fill, 'A')
connect(scalar('BakedFill', 0), '', fill, 'B')
editing.connect_material_property(fill, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
editing.connect_material_property(scalar('Roughness', .9), '', unreal.MaterialProperty.MP_ROUGHNESS)
editing.connect_material_property(scalar('Specular', .05), '', unreal.MaterialProperty.MP_SPECULAR)
editing.layout_material_expressions(asset)
editing.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('WORLD_LIGHTING_MATERIAL_SAVED: ' + path)
