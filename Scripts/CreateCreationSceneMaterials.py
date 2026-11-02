"""Depth-writing parents for authored Glue M2 geometry; no game assets."""
import unreal

edit = unreal.MaterialEditingLibrary
for name, blend in [('M_WowM2Opaque', unreal.BlendMode.BLEND_OPAQUE),
                    ('M_WowM2Masked', unreal.BlendMode.BLEND_MASKED)]:
    path = '/Game/Materials/' + name
    asset = unreal.load_asset(path)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('blend_mode', blend)
    asset.set_editor_property('two_sided', True)
    texture = edit.create_material_expression(asset, unreal.MaterialExpressionTextureSampleParameter2D)
    texture.set_editor_property('parameter_name', 'ParticleTexture')
    texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    vertex = edit.create_material_expression(asset, unreal.MaterialExpressionVertexColor)
    color = edit.create_material_expression(asset, unreal.MaterialExpressionMultiply)
    if not edit.connect_material_expressions(texture, 'RGB', color, 'A') or not edit.connect_material_expressions(vertex, '', color, 'B'):
        raise RuntimeError('Cannot connect authored colour: ' + path)
    if not edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Cannot connect emissive: ' + path)
    if blend == unreal.BlendMode.BLEND_MASKED:
        asset.set_editor_property('opacity_mask_clip_value', 128 / 255)
        if not edit.connect_material_property(texture, 'A', unreal.MaterialProperty.MP_OPACITY_MASK):
            raise RuntimeError('Cannot connect alpha key: ' + path)
    edit.layout_material_expressions(asset)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save ' + path)
    unreal.log('CREATION_SCENE_MATERIAL_SAVED: ' + path)
