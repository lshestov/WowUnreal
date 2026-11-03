"""Cookable original M2 two-texture combiners; no Blizzard data is embedded."""
import unreal

edit = unreal.MaterialEditingLibrary
variants = [('Opaque', unreal.BlendMode.BLEND_OPAQUE),
                      ('Masked', unreal.BlendMode.BLEND_MASKED),
                      ('Alpha', unreal.BlendMode.BLEND_TRANSLUCENT),
                      ('Add', unreal.BlendMode.BLEND_ADDITIVE)]
for suffix, blend, two_sided, no_depth in [(s, b, t, False) for s, b in variants for t in (False, True)] + [('Add', unreal.BlendMode.BLEND_ADDITIVE, True, True)]:
    name = 'M_WowGlue' + suffix + ('TwoSided' if two_sided else '') + ('NoDepth' if no_depth else '')
    asset = unreal.load_asset('/Game/Materials/' + name)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('blend_mode', blend)
    asset.set_editor_property('two_sided', two_sided)
    if suffix in ('Alpha', 'Add'):
        asset.set_editor_property('disable_depth_test', no_depth)
    def node(cls): return edit.create_material_expression(asset, cls)
    def link(a, out, b, pin):
        if not edit.connect_material_expressions(a, out, b, pin):
            raise RuntimeError(name + ': ' + pin)
    textures = []
    for i in range(2):
        uv = node(unreal.MaterialExpressionTextureCoordinate)
        uv.set_editor_property('coordinate_index', i)
        tex = node(unreal.MaterialExpressionTextureSampleParameter2D)
        tex.set_editor_property('parameter_name', 'ParticleTexture' if i == 0 else 'SecondTexture')
        tex.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
        link(uv, '', tex, 'UVs')
        textures.append(tex)
    vertex = node(unreal.MaterialExpressionVertexColor)
    inputs = [('T0', textures[0], 'RGB'), ('T1', textures[1], 'RGB'), ('V', vertex, ''),
              ('A0', textures[0], 'A'), ('A1', textures[1], 'A'), ('VA', vertex, 'A')]
    for param in ['Combiner', 'SecondCombiner', 'UseSecond']:
        value = node(unreal.MaterialExpressionScalarParameter)
        value.set_editor_property('parameter_name', param)
        value.set_editor_property('default_value', 0)
        inputs.append((param, value, ''))
    custom = node(unreal.MaterialExpressionCustom)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('code', '''
float4 C = float4(T0.rgb * V.rgb, VA * (Combiner < .5 ? 1 : A0));
if (UseSecond > .5) {
    if (SecondCombiner < .5) C = float4(T1.rgb * V.rgb, VA);
    else if (SecondCombiner < 1.5) C *= float4(T1, A1);
    else if (SecondCombiner < 2.5) C.rgb = lerp(C.rgb, T1.rgb, A1);
    else if (SecondCombiner < 3.5) C.rgb += T1.rgb;
    else if (SecondCombiner < 4.5) C *= float4(2*T1.rgb, A1);
    else if (SecondCombiner < 6.5) C.rgb *= 2*T1.rgb;
    else C.rgb += T1.rgb;
}
return C;
''')
    custom_inputs = []
    for key, _, _ in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', key)
        custom_inputs.append(entry)
    custom.set_editor_property('inputs', custom_inputs)
    for key, value, output in inputs: link(value, output, custom, key)
    if not edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Cannot connect emissive')
    alpha = node(unreal.MaterialExpressionComponentMask)
    alpha.set_editor_property('r', False); alpha.set_editor_property('g', False)
    alpha.set_editor_property('b', False); alpha.set_editor_property('a', True)
    link(custom, '', alpha, '')
    if suffix == 'Masked':
        asset.set_editor_property('opacity_mask_clip_value', 128/255)
        edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    elif suffix in ('Alpha', 'Add'):
        edit.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    edit.layout_material_expressions(asset);edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset): raise RuntimeError('Cannot save ' + name)
    unreal.log('LOGIN_SCENE_MATERIAL_SAVED: ' + name)

# The original EffectGlow is Box4 -> horizontal/vertical Gauss4 -> FFXGlow.
# Coefficients and offsets come from the user's original FFX .bls shaders and
# PassBox4/PassGauss4; the original shaders themselves are never packaged here.
for name, code, ui in [
    ('M_WowGlueBox4', '''
float3 C = Texture2DSample(Source,SourceSampler,UV+float2(-1.5,-1.5)*Step.xy).rgb;
C += Texture2DSample(Source,SourceSampler,UV+float2(.5,-1.5)*Step.xy).rgb;
C += Texture2DSample(Source,SourceSampler,UV+float2(.5,.5)*Step.xy).rgb;
C += Texture2DSample(Source,SourceSampler,UV+float2(-1.5,.5)*Step.xy).rgb;
return C*.25;
''', False),
    ('M_WowGlueGauss4', '''
return Texture2DSample(Source,SourceSampler,UV-2.5*Step.xy).rgb*.125
     + Texture2DSample(Source,SourceSampler,UV-.5*Step.xy).rgb*.375
     + Texture2DSample(Source,SourceSampler,UV+.5*Step.xy).rgb*.375
     + Texture2DSample(Source,SourceSampler,UV+2.5*Step.xy).rgb*.125;
''', False),
    ('M_WowGlueComposite', '''
float3 Base = Texture2DSample(Source,SourceSampler,UV).rgb;
float3 B = Texture2DSample(Blur,BlurSampler,UV).rgb;
float3 C = saturate(Base+B*B*Glow);
// Legacy M2/FFX blends in display space. Convert only once for Slate, whose
// output transfer restores those authored display values.
return lerp(C/12.92,pow((C+.055)/1.055,2.4),step(.04045,C));
''', True)]:
    asset = unreal.load_asset('/Game/Materials/'+name)
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(asset)
    asset.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI if ui else unreal.MaterialDomain.MD_SURFACE)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    if not ui: asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    def node(cls): return edit.create_material_expression(asset, cls)
    custom = node(unreal.MaterialExpressionCustom)
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    sources = [('UV', node(unreal.MaterialExpressionTextureCoordinate))]
    for param in (['Source', 'Blur'] if ui else ['Source']):
        tex = node(unreal.MaterialExpressionTextureObjectParameter)
        tex.set_editor_property('parameter_name', param)
        tex.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
        sources.append((param, tex))
    if ui:
        gain = node(unreal.MaterialExpressionScalarParameter)
        gain.set_editor_property('parameter_name', 'Glow');gain.set_editor_property('default_value', 20/255)
        sources.append(('Glow', gain))
    else:
        step = node(unreal.MaterialExpressionVectorParameter)
        step.set_editor_property('parameter_name', 'Step')
        step.set_editor_property('default_value', unreal.LinearColor(1/1024,1/768,0,0))
        sources.append(('Step', step))
    inputs = []
    for key, _ in sources:
        inp = unreal.CustomInput();inp.set_editor_property('input_name', key);inputs.append(inp)
    custom.set_editor_property('inputs', inputs)
    for key, source in sources:
        if not edit.connect_material_expressions(source, '', custom, key): raise RuntimeError(name+': '+key)
    if not edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR): raise RuntimeError(name)
    edit.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset): raise RuntimeError('Cannot save '+name)
    unreal.log('LOGIN_FFX_MATERIAL_SAVED: '+name)
