"""Cookable unlit particle parents; game sprites stay in the user's MPQs."""
import unreal

edit = unreal.MaterialEditingLibrary
for name, blend in [('M_WowParticleAlpha', unreal.BlendMode.BLEND_TRANSLUCENT),
                    ('M_WowParticleAdd', unreal.BlendMode.BLEND_ADDITIVE)]:
    path = '/Game/Materials/' + name
    asset = unreal.load_asset(path)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('blend_mode', blend)
    asset.set_editor_property('two_sided', True)

    def node(kind):
        return edit.create_material_expression(asset, kind)

    def link(source, output, target, pin):
        if not edit.connect_material_expressions(source, output, target, pin):
            raise RuntimeError('Cannot connect ' + name + ': ' + pin)

    texture = node(unreal.MaterialExpressionTextureSampleParameter2D)
    texture.set_editor_property('parameter_name', 'ParticleTexture')
    texture.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    vertex = node(unreal.MaterialExpressionVertexColor)
    color = node(unreal.MaterialExpressionMultiply)
    link(texture, 'RGB', color, 'A')
    link(vertex, '', color, 'B')
    edit.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opaque = node(unreal.MaterialExpressionConstant)
    opaque.set_editor_property('r', 1)
    ignore = node(unreal.MaterialExpressionScalarParameter)
    ignore.set_editor_property('parameter_name', 'IgnoreAlpha')
    ignore.set_editor_property('default_value', 0)
    texture_alpha = node(unreal.MaterialExpressionLinearInterpolate)
    link(texture, 'A', texture_alpha, 'A')
    link(opaque, '', texture_alpha, 'B')
    link(ignore, '', texture_alpha, 'Alpha')
    opacity = node(unreal.MaterialExpressionMultiply)
    link(texture_alpha, '', opacity, 'A')
    link(vertex, 'A', opacity, 'B')
    edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(asset)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Cannot save ' + path)
    unreal.log('PARTICLE_MATERIAL_SAVED: ' + path)
