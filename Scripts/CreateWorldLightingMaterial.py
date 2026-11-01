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
    names = list(editing.get_material_expression_input_names(target))
    actual = names[0] if input_name not in names and len(names) == 1 else input_name
    if not editing.connect_material_expressions(source, output, target, actual):
        raise RuntimeError('Cannot connect ' + target.get_class().get_name() + '.' + input_name + ': ' + str(names))


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
strength = scalar('BakedLightStrength', 0)
outdoor = node(unreal.MaterialExpressionLinearInterpolate)
connect(white, '', outdoor, 'A')
connect(vertex, 'A', outdoor, 'B')
connect(strength, '', outdoor, 'Alpha')
unlit = scalar('Unlit', 0)
lit = node(unreal.MaterialExpressionOneMinus)
connect(unlit, '', lit, 'Input')
weight = node(unreal.MaterialExpressionMultiply)
connect(lit, '', weight, 'A')
connect(outdoor, '', weight, 'B')
tint = node(unreal.MaterialExpressionLinearInterpolate)
connect(white, '', tint, 'A')
connect(vertex, '', tint, 'B')
connect(strength, '', tint, 'Alpha')
surface = node(unreal.MaterialExpressionMultiply)
connect(texture, 'RGB', surface, 'A')
connect(tint, '', surface, 'B')
base = node(unreal.MaterialExpressionMultiply)
connect(surface, '', base, 'A')
connect(weight, '', base, 'B')
editing.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
editing.connect_material_property(texture, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
ambient = node(unreal.MaterialExpressionVectorParameter)
ambient.set_editor_property('parameter_name', 'WmoAmbient')
ambient.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 1))
baked = node(unreal.MaterialExpressionAdd)
connect(vertex, '', baked, 'A')
connect(ambient, '', baked, 'B')
interior = node(unreal.MaterialExpressionOneMinus)
connect(outdoor, '', interior, 'Input')
local = node(unreal.MaterialExpressionMultiply)
connect(baked, '', local, 'A')
connect(interior, '', local, 'B')
emission = node(unreal.MaterialExpressionLinearInterpolate)
connect(local, '', emission, 'A')
connect(white, '', emission, 'B')
connect(unlit, '', emission, 'Alpha')
fill = node(unreal.MaterialExpressionMultiply)
connect(texture, 'RGB', fill, 'A')
connect(emission, '', fill, 'B')
editing.connect_material_property(fill, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
editing.connect_material_property(scalar('Roughness', .9), '', unreal.MaterialProperty.MP_ROUGHNESS)
editing.connect_material_property(scalar('Specular', .05), '', unreal.MaterialProperty.MP_SPECULAR)
editing.layout_material_expressions(asset)
editing.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save ' + path)
unreal.log('WORLD_LIGHTING_MATERIAL_SAVED: ' + path)
