import lib
import lib.std
import lib.lua.stl


def bind_std_vector(gen, value_conv, bound_name):
    if gen.get_language() != 'Lua':
        raise RuntimeError('sdf-generator bindings only target Lua in this project')

    sequence_type = f'LuaTableOf{value_conv.bound_name}'
    gen.bind_type(lib.lua.stl.LuaTableToStdVectorConverter(sequence_type, value_conv))

    vector_conv = gen.begin_class(
        f'std::vector<{value_conv.ctype}>',
        bound_name=bound_name,
        features={'sequence': lib.std.VectorSequenceFeature(value_conv)})
    gen.bind_constructor(vector_conv, [f'?{sequence_type} sequence'])
    gen.bind_method(vector_conv, 'size', 'size_t', [])
    gen.bind_method(vector_conv, 'push_back', 'void', [f'{value_conv.ctype} v'])
    gen.bind_method(vector_conv, 'at', repr(value_conv.ctype), ['size_t idx'])
    gen.end_class(vector_conv)

    return vector_conv


def bind(gen):
    # Keep the exported entry point as luaopen_sdf so Lua can resolve
    # require("sdf-generator") through its ignore-mark rule for '-'.
    gen.start('sdf')

    lib.bind_defaults(gen)
    gen.add_include('vector', is_system=True)
    gen.add_include('sdf/lua_api.h')

    gen.bind_named_enum('sdf::CsgOp', ['Add', 'Subtract'], prefix='CsgOp')
    gen.bind_named_enum(
        'sdf::ModifierMask',
        ['All', 'Top', 'Bottom', 'Edges', 'TopEdges'],
        prefix='ModifierMask')
    gen.bind_named_enum(
        'sdf::MeshingMode',
        ['MarchingTetrahedra', 'DualContouring', 'AdaptiveDualContouring'],
        prefix='MeshingMode')

    vec2 = gen.begin_class('sdf::Vec2', bound_name='Vec2')
    gen.bind_constructor(vec2, [])
    gen.bind_members(vec2, ['float x', 'float y'])
    gen.end_class(vec2)

    vec3 = gen.begin_class('sdf::Vec3', bound_name='Vec3')
    gen.bind_constructor(vec3, [])
    gen.bind_members(vec3, ['float x', 'float y', 'float z'])
    gen.end_class(vec3)

    aabb = gen.begin_class('sdf::Aabb', bound_name='Aabb')
    gen.bind_constructor(aabb, [])
    gen.bind_members(aabb, ['sdf::Vec3 min', 'sdf::Vec3 max'])
    gen.end_class(aabb)

    transform = gen.begin_class('sdf::Transform', bound_name='Transform')
    gen.bind_constructor(transform, [])
    gen.bind_members(transform, ['sdf::Vec3 translation'])
    gen.end_class(transform)

    sdf_box = gen.begin_class('sdf::SdfBox', bound_name='SdfBox')
    gen.bind_constructor(sdf_box, [])
    gen.bind_members(
        sdf_box,
        [
            'std::string name',
            'sdf::Transform transform',
            'sdf::Vec3 half_size',
            'uint32_t material_id',
            'sdf::CsgOp op',
        ])
    gen.end_class(sdf_box)

    noise_modifier = gen.begin_class(
        'sdf::NoiseDisplaceMaskedModifier',
        bound_name='NoiseDisplaceMaskedModifier')
    gen.bind_constructor(noise_modifier, [])
    gen.bind_members(
        noise_modifier,
        [
            'std::string name',
            'std::string target_box_name',
            'float amplitude',
            'float frequency',
            'uint32_t seed',
            'uint32_t octaves',
            'sdf::ModifierMask mask',
            'float mask_width',
        ])
    gen.end_class(noise_modifier)

    box_cut_modifier = gen.begin_class('sdf::BoxCutModifier', bound_name='BoxCutModifier')
    gen.bind_constructor(box_cut_modifier, [])
    gen.bind_members(
        box_cut_modifier,
        [
            'std::string name',
            'std::string target_box_name',
            'sdf::Vec3 translation',
            'sdf::Vec3 half_size',
        ])
    gen.end_class(box_cut_modifier)

    bind_std_vector(gen, sdf_box, 'SdfBoxList')
    bind_std_vector(gen, noise_modifier, 'NoiseDisplaceMaskedModifierList')
    bind_std_vector(gen, box_cut_modifier, 'BoxCutModifierList')

    scene_document = gen.begin_class('sdf::SceneDocument', bound_name='SceneDocument')
    gen.bind_constructor(scene_document, [])
    gen.bind_members(
        scene_document,
        [
            'std::string name',
            'std::vector<sdf::SdfBox> boxes',
            'std::vector<sdf::NoiseDisplaceMaskedModifier> noise_modifiers',
            'std::vector<sdf::BoxCutModifier> box_cut_modifiers',
        ])
    gen.end_class(scene_document)

    build_settings = gen.begin_class('sdf::BuildSettings', bound_name='BuildSettings')
    gen.bind_constructor(build_settings, [])
    gen.bind_members(
        build_settings,
        [
            'sdf::Aabb bounds',
            'float cell_size',
            'sdf::MeshingMode meshing_mode',
            'float adaptive_normal_dot_threshold',
            'float adaptive_plane_error_ratio',
            'float adaptive_min_plane_error_in_cells',
        ])
    gen.end_class(build_settings)

    scene_file = gen.begin_class('sdf::SceneFile', bound_name='SceneFile')
    gen.bind_constructor(scene_file, [])
    gen.bind_members(scene_file, ['sdf::SceneDocument scene', 'sdf::BuildSettings build_settings'])
    gen.end_class(scene_file)

    uv_unwrap_settings = gen.begin_class('sdf::UvUnwrapSettings', bound_name='UvUnwrapSettings')
    gen.bind_constructor(uv_unwrap_settings, [])
    gen.bind_members(
        uv_unwrap_settings,
        [
            'uint32_t resolution',
            'uint32_t padding',
            'uint32_t max_chart_size',
            'uint32_t chart_max_iterations',
            'float texels_per_unit',
            'float epsilon',
            'float chart_max_cost',
            'float normal_deviation_weight',
            'float roundness_weight',
            'float straightness_weight',
            'float normal_seam_weight',
            'float texture_seam_weight',
            'bool brute_force',
            'bool bilinear',
            'bool block_align',
            'bool rotate_charts',
            'bool rotate_charts_to_axis',
        ])
    gen.end_class(uv_unwrap_settings)

    uv_unwrap_result = gen.begin_class('sdf::UvUnwrapResult', bound_name='UvUnwrapResult')
    gen.bind_constructor(uv_unwrap_result, [])
    gen.bind_members(
        uv_unwrap_result,
        [
            'uint32_t atlas_width',
            'uint32_t atlas_height',
            'uint32_t atlas_count',
            'uint32_t chart_count',
            'size_t vertex_count',
            'size_t triangle_count',
            'size_t min_chart_triangle_count',
            'size_t max_chart_triangle_count',
            'size_t single_triangle_chart_count',
            'size_t chart_texel_count',
            'size_t padding_texel_count',
            'size_t min_chart_texel_count',
            'size_t max_chart_texel_count',
            'float average_chart_triangle_count',
            'float average_chart_texel_count',
            'float texels_per_unit',
            'float utilization',
        ])
    gen.end_class(uv_unwrap_result)

    build_request = gen.begin_class('sdf::lua_api::BuildRequest', bound_name='BuildRequest')
    gen.bind_constructor(build_request, [])
    gen.bind_members(
        build_request,
        [
            'bool export_obj',
            'std::string output_obj_path',
            'bool unwrap_uvs',
            'sdf::UvUnwrapSettings uv_unwrap_settings',
        ])
    gen.end_class(build_request)

    build_result = gen.begin_class('sdf::lua_api::BuildResult', bound_name='BuildResult')
    gen.bind_constructor(build_result, [])
    gen.bind_members(
        build_result,
        [
            'bool obj_written',
            'std::string output_obj_path',
            'size_t sampled_cells',
            'size_t occupied_cells',
            'size_t generated_vertex_count',
            'size_t generated_triangle_count',
            'size_t export_vertex_count',
            'size_t export_triangle_count',
            'bool uv_unwrap_performed',
            'sdf::UvUnwrapResult uv_unwrap_result',
        ])
    gen.end_class(build_result)

    gen.bind_function('sdf::make_frame_006_blockout_scene', 'sdf::SceneDocument', [], bound_name='make_frame_006_blockout_scene')
    gen.bind_function('sdf::make_frame_006_build_settings', 'sdf::BuildSettings', [], bound_name='make_frame_006_build_settings')
    gen.bind_function('sdf::meshing_mode_name', 'const char *', ['sdf::MeshingMode mode'], bound_name='meshing_mode_name')

    gen.bind_function(
        'sdf::lua_api::load_scene_file',
        'bool',
        ['const std::string &input_path', 'sdf::SceneFile &scene_file', 'std::string &error_message'],
        {'arg_out': ['scene_file', 'error_message']},
        bound_name='load_scene_file')
    gen.bind_function(
        'sdf::lua_api::save_scene_file',
        'bool',
        ['const sdf::SceneFile &scene_file', 'const std::string &output_path', 'std::string &error_message'],
        {'arg_out': ['error_message']},
        bound_name='save_scene_file')
    gen.bind_function(
        'sdf::lua_api::build_scene_file',
        'bool',
        ['const sdf::SceneFile &scene_file', 'const sdf::lua_api::BuildRequest &request', 'sdf::lua_api::BuildResult &build_result', 'std::string &error_message'],
        {'arg_out': ['build_result', 'error_message']},
        bound_name='build_scene_file')

    gen.finalize()
