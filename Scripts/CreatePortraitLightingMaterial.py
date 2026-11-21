"""Generate the 3.3.5a unit-portrait light for isolated character captures."""
import unreal

edit = unreal.MaterialEditingLibrary
name = 'M_WowPortrait'
asset = unreal.load_asset('/Game/Materials/' + name)
if not asset:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
edit.delete_all_material_expressions(asset)
asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
asset.set_editor_property('two_sided', True)
asset.set_editor_property('used_with_skeletal_mesh', True)


def node(kind):
    return edit.create_material_expression(asset, kind)


def expression(code, output_type, inputs):
    result = node(unreal.MaterialExpressionCustom)
    result.set_editor_property('code', code)
    result.set_editor_property('output_type', output_type)
    pins = []
    for key, _, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', key)
        pins.append(pin)
    result.set_editor_property('inputs', pins)
    for key, source, output in inputs:
        if not edit.connect_material_expressions(source, output, result, key):
            raise RuntimeError('Cannot connect portrait material input ' + key)
    return result


normal = node(unreal.MaterialExpressionVertexNormalWS)
direction = node(unreal.MaterialExpressionVectorParameter)
direction.set_editor_property('parameter_name', 'TowardLight')
direction.set_editor_property('default_value', unreal.LinearColor(2**-.5, 0, 2**-.5, 1))
# PortraitLightingCallback supplies ambient 0.45 and one white light ray
# (-1,0,-1). CM2Lighting::SetupSunlight folds that ray's length into the two
# coefficients before the vertex shader lights the original skinned normals.
light = expression('''
const float strength = sqrt(2.0);
const float ambient = 0.45 + 0.25 * (1.0 - strength);
const float diffuse = 1.25 * strength - 0.25;
return saturate(ambient + diffuse * saturate(dot(SurfaceNormal, LightDirection)));
''', unreal.CustomMaterialOutputType.CMOT_FLOAT1, [
    ('SurfaceNormal', normal, ''), ('LightDirection', direction, '')])
shade = node(unreal.MaterialExpressionVertexInterpolator)
if not edit.connect_material_expressions(light, '', shade, edit.get_material_expression_input_names(shade)[0]):
    raise RuntimeError('Cannot interpolate vertex lighting')
texture = node(unreal.MaterialExpressionTextureSampleParameter2D)
texture.set_editor_property('parameter_name', 'BaseTexture')
texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
color = expression('''
float3 linearColor = saturate(Albedo);
float3 displayColor = lerp(12.92 * linearColor, 1.055 * pow(linearColor, 1.0/2.4) - 0.055,
                           step(0.0031308, linearColor));
float3 lit = saturate(displayColor * Shade);
return lerp(lit / 12.92, pow((lit + 0.055) / 1.055, 2.4), step(0.04045, lit));
''', unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                   [('Albedo', texture, 'RGB'), ('Shade', shade, '')])
if not edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('Cannot connect shaded portrait color')
if not edit.connect_material_property(texture, 'A', unreal.MaterialProperty.MP_OPACITY_MASK):
    raise RuntimeError('Cannot connect original cutout mask')
edit.layout_material_expressions(asset)
edit.recompile_material(asset)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Cannot save portrait material')
unreal.log('PORTRAIT_MATERIAL_SAVED')
