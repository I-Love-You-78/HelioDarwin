//! Per-draw bindings come from the compiler's canonical render planner.
use super::{
    backend_plan, cstring, invalid, report, Failure, OwnedOutput, RenderOutput, Result,
    ShaderError, StageModule,
};
use metal2vulkan::{
    bind::{
        BindState, BoundBuffer, BoundSampler, BoundTexture, DrawCall, DrawCommands, ImageSource,
        ImageViewDesc, IndexBufferRef, NeutralResource, SamplerDescriptor, SamplerSource,
        StageTable, TextureFacts, WriteSource,
    },
    cache::VariantKey,
    error::BindError,
    metal,
    pipeline::{RenderPipeline, Stage},
    vk, Budget,
};
use std::{
    ffi::{c_void, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    sync::Arc,
    time::{Duration, Instant},
};

const DRAW_ABI: u32 = 1;
pub const CAPABILITIES: u64 = if cfg!(feature = "shader-tile-image") {
    1
} else {
    0
};
#[cfg(feature = "shader-tile-image")]
fn tile_reads(p: &RenderPipeline) -> &[u32] {
    &p.graphics.tile_image_color_reads
}
#[cfg(not(feature = "shader-tile-image"))]
fn tile_reads(_: &RenderPipeline) -> &[u32] {
    &[]
}
fn unsupported(message: impl Into<String>) -> Failure {
    Failure(super::UNSUPPORTED, message.into())
}
fn bind_error(error: BindError) -> Failure {
    unsupported(error.to_string())
}
#[repr(C)]
pub struct Buffer {
    length: u64,
    offset: u64,
    gpu_address: u64,
    device_address: u64,
    bound: u32,
    attribute_stride: u32,
    has_attribute_stride: u32,
}
#[repr(C)]
pub struct Texture {
    bound: u32,
    texture_type: u32,
    pixel_format: u32,
    usage: u32,
    width: u32,
    height: u32,
    depth: u32,
    array_length: u32,
    mipmap_level_count: u32,
    sample_count: u32,
    sparse: u32,
    own_format_views: u32,
    parent_relative_level: u32,
    parent_base_size: [u32; 3],
    swizzle: [u32; 4],
    resource_id: u64,
}
#[repr(C)]
pub struct Sampler {
    bound: u32,
    min_filter: u32,
    mag_filter: u32,
    mip_filter: u32,
    s_address_mode: u32,
    t_address_mode: u32,
    r_address_mode: u32,
    border_color: u32,
    normalized_coordinates: u32,
    lod_average: u32,
    max_anisotropy: u32,
    compare_function: u32,
    reduction_mode: u32,
    lod_min_clamp: f32,
    lod_max_clamp: f32,
    resource_id: u64,
}
#[repr(C)]
pub struct StageBindings {
    buffers: [Buffer; 31],
    textures: [Texture; 128],
    samplers: [Sampler; 16],
}
#[repr(C)]
pub struct Bindings {
    vertex: StageBindings,
    fragment: StageBindings,
    render_width: u32,
    render_height: u32,
    render_target_array_length: u32,
    viewport_count: u32,
    color_formats: [u32; 8],
    blend_color: [f32; 4],
}
#[repr(C)]
pub struct DrawRequest {
    kind: u32,
    primitive_type: u32,
    start: u32,
    count: u32,
    instance_count: u32,
    base_instance: u32,
    base_vertex: i32,
    index_type: u32,
    index_length: u64,
    index_offset: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ImageView {
    view_type: u32,
    format: u32,
    aspect: u32,
    layout: u32,
    components: [u32; 4],
    level: u32,
    flags: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct NativeSampler {
    min_filter: u32,
    mag_filter: u32,
    mipmap_mode: u32,
    address_u: u32,
    address_v: u32,
    address_w: u32,
    compare_enabled: u32,
    compare_op: u32,
    border_color: u32,
    reduction_mode: u32,
    border_components_enabled: u32,
    border_components: [u32; 4],
    min_lod: f32,
    max_lod: f32,
    lod_bias: f32,
    max_anisotropy: f32,
}
#[repr(C)]
pub struct Descriptor {
    set: u32,
    binding: u32,
    array_element: u32,
    vulkan_type: u32,
    source_kind: u32,
    metal_stage: u32,
    metal_index: u32,
    sampler_stage: u32,
    sampler_index: u32,
    view: ImageView,
    sampler: NativeSampler,
    offset: u64,
    range: u64,
    bytes: *const c_void,
    byte_count: usize,
}
#[repr(C)]
pub struct Push {
    vulkan_stages: u32,
    offset: u32,
    size: u32,
    bytes: *const c_void,
}
#[repr(C)]
pub struct Specialization {
    id: u32,
    offset: u32,
    size: usize,
}
#[repr(C)]
pub struct VertexBuffer {
    binding: u32,
    metal_index: u32,
    stride: u32,
    offset: u64,
}
#[repr(C)]
pub struct DrawPlan {
    abi_version: u32,
    descriptor_count: u32,
    descriptors: *const Descriptor,
    push_count: u32,
    pushes: *const Push,
    specialization_count: u32,
    specialization: *const Specialization,
    specialization_data: *const c_void,
    specialization_size: usize,
    vertex_buffer_count: u32,
    vertex_buffers: *const VertexBuffer,
    module_count: u32,
    modules: *const StageModule,
    backend_plan: *const backend_plan::BackendPlan,
    pipeline_key: [u8; 32],
    draw_kind: u32,
    draw_arguments: [u32; 5],
    index_type: u32,
    index_offset: u64,
    private_data: *mut c_void,
    destroy: Option<unsafe extern "C" fn(*mut DrawPlan)>,
}

fn stage_index(stage: Stage) -> Result<u32> {
    match stage {
        Stage::Vertex => Ok(0),
        Stage::Fragment => Ok(1),
        _ => Err(unsupported("Non-render resource stage")),
    }
}
fn table(input: &StageBindings) -> Result<StageTable> {
    let mut table = StageTable::default();
    for (index, b) in input.buffers.iter().enumerate() {
        if b.bound == 0 {
            continue;
        }
        if b.offset > b.length {
            return Err(invalid("Buffer bind offset exceeds its logical length"));
        }
        table.buffers.insert(
            index as u32,
            BoundBuffer::Buffer {
                length: b.length,
                offset: b.offset,
                gpu_address: b.gpu_address,
                device_address: (b.device_address != 0).then_some(b.device_address),
                attribute_stride: (b.has_attribute_stride != 0).then_some(b.attribute_stride),
            },
        );
    }
    for (index, t) in input.textures.iter().enumerate() {
        if t.bound == 0 {
            continue;
        }
        let mut facts = TextureFacts::new(
            metal::TextureType::try_from(t.texture_type)?,
            metal::PixelFormat::try_from(t.pixel_format)?,
        );
        if t.mipmap_level_count == 0 || t.array_length == 0 || t.sample_count == 0 {
            return Err(invalid(
                "Texture bind facts have no levels, layers or samples",
            ));
        }
        facts.usage = metal::TextureUsage::try_from(t.usage)?;
        facts.sample_count = t.sample_count;
        facts.array_length = t.array_length;
        facts.mipmap_level_count = t.mipmap_level_count;
        facts.sparse = t.sparse != 0;
        facts.own_format_views = t.own_format_views != 0;
        facts.parent_relative_level = t.parent_relative_level;
        facts.parent_base_size = t.parent_base_size;
        facts.swizzle = metal::TextureSwizzleChannels::new(
            metal::TextureSwizzle::try_from(t.swizzle[0])?,
            metal::TextureSwizzle::try_from(t.swizzle[1])?,
            metal::TextureSwizzle::try_from(t.swizzle[2])?,
            metal::TextureSwizzle::try_from(t.swizzle[3])?,
        );
        table.textures.insert(
            index as u32,
            BoundTexture::Texture {
                facts,
                resource_id: (t.resource_id != 0).then_some(t.resource_id),
            },
        );
    }
    for (index, s) in input.samplers.iter().enumerate() {
        if s.bound == 0 {
            continue;
        }
        let mut desc = SamplerDescriptor::new();
        desc.min_filter = metal::SamplerMinMagFilter::try_from(s.min_filter)?;
        desc.mag_filter = metal::SamplerMinMagFilter::try_from(s.mag_filter)?;
        desc.mip_filter = metal::SamplerMipFilter::try_from(s.mip_filter)?;
        desc.s_address_mode = metal::SamplerAddressMode::try_from(s.s_address_mode)?;
        desc.t_address_mode = metal::SamplerAddressMode::try_from(s.t_address_mode)?;
        desc.r_address_mode = metal::SamplerAddressMode::try_from(s.r_address_mode)?;
        desc.border_color = metal::SamplerBorderColor::try_from(s.border_color)?;
        desc.normalized_coordinates = s.normalized_coordinates != 0;
        desc.lod_average = s.lod_average != 0;
        desc.max_anisotropy = s.max_anisotropy;
        desc.lod_min_clamp = s.lod_min_clamp;
        desc.lod_max_clamp = s.lod_max_clamp;
        desc.compare_function = metal::CompareFunction::try_from(s.compare_function)?;
        desc.reduction_mode = metal::SamplerReductionMode::try_from(s.reduction_mode)?;
        let mut bound = BoundSampler::new(desc);
        bound.resource_id = (s.resource_id != 0).then_some(s.resource_id);
        table.samplers.insert(index as u32, bound);
    }
    Ok(table)
}
fn image_view(view: &ImageViewDesc) -> ImageView {
    ImageView {
        view_type: view.view_type.as_raw() as u32,
        format: view.format.as_raw() as u32,
        aspect: view.aspect.as_raw(),
        layout: view.layout.as_raw() as u32,
        components: [
            view.components.r.as_raw() as u32,
            view.components.g.as_raw() as u32,
            view.components.b.as_raw() as u32,
            view.components.a.as_raw() as u32,
        ],
        level: view.level.unwrap_or(u32::MAX),
        flags: u32::from(view.twin) | (u32::from(view.shader_swizzle) << 1),
    }
}
fn native_sampler(s: &vk::SamplerDesc) -> NativeSampler {
    let mut out = NativeSampler {
        min_filter: s.min_filter.as_raw() as u32,
        mag_filter: s.mag_filter.as_raw() as u32,
        mipmap_mode: s.mipmap_mode.as_raw() as u32,
        address_u: s.address_mode_u.as_raw() as u32,
        address_v: s.address_mode_v.as_raw() as u32,
        address_w: s.address_mode_w.as_raw() as u32,
        compare_enabled: u32::from(s.compare.is_some()),
        compare_op: s.compare.map_or(0, |v| v.as_raw() as u32),
        border_color: s.border_color.as_raw() as u32,
        reduction_mode: s.reduction.as_raw() as u32,
        min_lod: s.min_lod,
        max_lod: s.max_lod,
        lod_bias: s.mip_lod_bias,
        max_anisotropy: s.max_anisotropy.unwrap_or(1.0),
        ..NativeSampler::default()
    };
    if let Some(c) = s.border_components {
        out.border_components_enabled = 1;
        out.border_components = [
            c.r.as_raw() as u32,
            c.g.as_raw() as u32,
            c.b.as_raw() as u32,
            c.a.as_raw() as u32,
        ];
    }
    out
}
fn metadata(p: &RenderPipeline) -> serde_json::Value {
    let stages: Vec<_> = p
        .stages
        .iter()
        .map(|s| {
            serde_json::json!({"stage":s.stage,"stage_flags":s.stage_flags,
        "required_subgroup_size":s.required_subgroup_size,"specialization":s.specialization})
        })
        .collect();
    let mut graphics = serde_json::to_value(&p.graphics).expect("GraphicsState serialization");
    graphics["tile_image_color_reads"] = serde_json::json!(tile_reads(p));
    serde_json::json!({"variant_bits":p.variant_bits,"interface":p.interface,"graphics":graphics,
        "vertex_input":p.vertex_input,"layout":p.layout,"dependencies":p.dependencies,"stages":stages})
}
fn execution_plan(
    p: &RenderPipeline,
    owner: &OwnedOutput,
) -> Result<Box<backend_plan::BackendPlan>> {
    let blobs: Vec<_> = p
        .stages
        .iter()
        .filter_map(|s| s.constant_blob.as_deref().map(|b| (s.stage.as_raw(), b)))
        .collect();
    let limits = owner.device.profile().limits();
    backend_plan::build(
        &metadata(p),
        limits.min_uniform_buffer_offset_alignment,
        limits.min_storage_buffer_offset_alignment,
        &blobs,
    )
    .map_err(unsupported)
}
struct OwnedDraw {
    _variant: Option<Arc<RenderPipeline>>,
    _commands: DrawCommands,
    descriptors: Vec<Descriptor>,
    pushes: Vec<Push>,
    specialization: Vec<Specialization>,
    specialization_data: Vec<u8>,
    vertex_buffers: Vec<VertexBuffer>,
    _names: Vec<CString>,
    modules: Vec<StageModule>,
    backend_plan: Box<backend_plan::BackendPlan>,
    pipeline_key: [u8; 32],
    draw_kind: u32,
    draw_arguments: [u32; 5],
    index_type: u32,
    index_offset: u64,
}
fn prepare(owner: &OwnedOutput, input: &Bindings, request: &DrawRequest) -> Result<Box<OwnedDraw>> {
    let mut state = BindState::default();
    state.stages.insert(Stage::Vertex, table(&input.vertex)?);
    state
        .stages
        .insert(Stage::Fragment, table(&input.fragment)?);
    state.render_target_array_length = input.render_target_array_length;
    state.viewport_count = input.viewport_count;
    state.render_area = [input.render_width, input.render_height];
    state.blend_color = input.blend_color;
    state.view_base_alignment = BindState::view_base_alignment_of(owner.device.profile().limits());
    state.primitive_type = Some(metal::PrimitiveType::try_from(request.primitive_type)?);
    for (index, &format) in input.color_formats.iter().enumerate() {
        match (owner.descriptor.color_attachments[index].is_some(), format) {
            (false, format) if format != 0 => {
                state
                    .zeroed_colors
                    .insert(index as u32, metal::PixelFormat::try_from(format)?);
            }
            (true, 0) => {
                state.dropped_colors.insert(index as u32);
            }
            _ => (),
        }
    }
    let draw = match request.kind {
        0 => DrawCall::Primitives {
            vertex_start: request.start,
            vertex_count: request.count,
            instance_count: request.instance_count,
            base_instance: request.base_instance,
        },
        1 => DrawCall::Indexed {
            index_type: vk::IndexType::from_raw(request.index_type as i32),
            index_buffer: IndexBufferRef::new(0, request.index_length),
            index_count: request.count,
            index_offset: request.index_offset,
            instance_count: request.instance_count,
            base_vertex: request.base_vertex,
            base_instance: request.base_instance,
        },
        _ => return Err(unsupported("Unknown render draw kind")),
    };
    let budget = Budget::with_deadline(Some(Instant::now() + Duration::from_secs(20)));
    let mut active: Option<Arc<RenderPipeline>> = None;
    let mut current = VariantKey::new();
    let mut seen = Vec::new();
    let (plan, commands) = loop {
        let p = active.as_deref().unwrap_or(&owner._pipeline);
        let plan = p.plan(&owner.device, &state.shape()).map_err(bind_error)?;
        let needed = if plan.variant != current {
            Some(plan.variant.clone())
        } else {
            match plan.instantiate_draw(&state, &draw) {
                Ok(commands) => break (plan, commands),
                Err(BindError::NeedsVariant { variant }) => Some(*variant),
                Err(e) => return Err(bind_error(e)),
            }
        };
        let needed = needed.ok_or_else(|| invalid("Render variant selection has no key"))?;
        if seen.contains(&needed) || seen.len() == 16 {
            return Err(unsupported("Render resource/draw variant did not converge"));
        }
        seen.push(needed.clone());
        let mut cache = owner
            .variants
            .lock()
            .map_err(|_| invalid("Render variant cache lock is poisoned"))?;
        let variant = if let Some(p) = cache.get(&needed) {
            Arc::clone(p)
        } else {
            let p = Arc::new(owner.device.compile_render_variant(
                &owner.descriptor,
                &needed,
                &budget,
            )?);
            if cache.len() == 64 {
                if let Some(key) = cache.keys().next().cloned() {
                    cache.remove(&key);
                }
            }
            cache.insert(needed.clone(), Arc::clone(&p));
            p
        };
        active = Some(variant);
        current = needed;
    };
    if !commands.push_holes.is_empty()
        || !commands.root_tables.is_empty()
        || !commands.descriptor_bytes.is_empty()
        || commands.mesh_dispatch.is_some()
        || commands.tessellation_dispatch.is_some()
        || commands.index_bounds_pass
        || commands.index_overrun.is_some()
        || !commands.amplifications.is_empty()
    {
        return Err(unsupported(
            "Render draw requires synthetic tables, addresses, amplification or preprocessing",
        ));
    }
    let p = active.as_deref().unwrap_or(&owner._pipeline);
    let mut descriptors = Vec::new();
    for write in &plan.descriptors {
        let mut d = Descriptor {
            set: write.set,
            binding: write.binding,
            array_element: write.array_element,
            vulkan_type: write.ty.as_raw() as u32,
            source_kind: 0,
            metal_stage: 0,
            metal_index: u32::MAX,
            sampler_stage: 0,
            sampler_index: u32::MAX,
            view: ImageView::default(),
            sampler: NativeSampler::default(),
            offset: 0,
            range: 0,
            bytes: ptr::null(),
            byte_count: 0,
        };
        let range = commands
            .descriptor_ranges
            .iter()
            .find(|r| r.set == write.set && r.binding == write.binding);
        match &write.source {
            WriteSource::Buffer { slot } => {
                let range =
                    range.ok_or_else(|| invalid("Canonical buffer descriptor has no range"))?;
                d.source_kind = 1;
                d.metal_stage = stage_index(slot.stage)?;
                d.metal_index = slot.index;
                d.offset = range.range.offset;
                d.range = range.range.range;
            }
            WriteSource::Texture { slot, view } => {
                d.source_kind = 2;
                d.metal_stage = stage_index(slot.stage)?;
                d.metal_index = slot.index;
                d.view = image_view(view);
            }
            WriteSource::CombinedImageSampler { image, sampler } => {
                d.source_kind = 4;
                let view = match image {
                    ImageSource::Texture { slot, view } => {
                        d.metal_stage = stage_index(slot.stage)?;
                        d.metal_index = slot.index;
                        d.view = image_view(view);
                        view
                    }
                    other => {
                        return Err(unsupported(format!(
                            "Render sampled image source {other:?}"
                        )))
                    }
                };
                match sampler {
                    SamplerSource::Slot {
                        slot,
                        compare,
                        integer,
                        cube_gradient,
                        opaque_black,
                    } => {
                        let table = state
                            .stages
                            .get(&slot.stage)
                            .ok_or_else(|| invalid("Sampler stage is absent"))?;
                        let bound = table
                            .samplers
                            .get(&slot.index)
                            .ok_or_else(|| invalid("Sampler slot is absent"))?;
                        let use_ = metal::sampler::SamplerUse::new(*compare, *integer)
                            .with_cube_gradient(*cube_gradient);
                        d.sampler = native_sampler(
                            &metal2vulkan::bind::sampler_desc(
                                &owner.device,
                                &bound.desc,
                                use_,
                                *opaque_black,
                                Some(view.format),
                            )
                            .map_err(bind_error)?,
                        );
                        d.sampler_stage = stage_index(slot.stage)?;
                        d.sampler_index = slot.index;
                    }
                    SamplerSource::Immutable => {
                        let sampler = p
                            .layout
                            .sets
                            .iter()
                            .find(|s| s.set == write.set)
                            .and_then(|s| s.bindings.iter().find(|b| b.binding == write.binding))
                            .and_then(|b| b.immutable_sampler.as_ref())
                            .ok_or_else(|| {
                                invalid("Canonical immutable sampler is absent from its layout")
                            })?;
                        d.sampler = native_sampler(sampler);
                        d.sampler_stage = u32::MAX;
                    }
                    other => {
                        return Err(unsupported(format!(
                            "Render sampled sampler source {other:?}"
                        )))
                    }
                }
            }
            WriteSource::Sampler {
                slot,
                compare,
                integer,
                cube_gradient,
                opaque_black,
            } => {
                let table = state
                    .stages
                    .get(&slot.stage)
                    .ok_or_else(|| invalid("Sampler stage is absent"))?;
                let bound = table
                    .samplers
                    .get(&slot.index)
                    .ok_or_else(|| invalid("Sampler slot is absent"))?;
                let use_ = metal::sampler::SamplerUse::new(*compare, *integer)
                    .with_cube_gradient(*cube_gradient);
                d.source_kind = 3;
                d.sampler_stage = stage_index(slot.stage)?;
                d.sampler_index = slot.index;
                d.sampler = native_sampler(
                    &metal2vulkan::bind::sampler_desc(
                        &owner.device,
                        &bound.desc,
                        use_,
                        *opaque_black,
                        None,
                    )
                    .map_err(bind_error)?,
                );
            }
            WriteSource::ConstantBlob { stage } => {
                let vk_stage = match stage {
                    Stage::Vertex => 1,
                    Stage::Fragment => 16,
                    _ => return Err(unsupported("Constant blob of a non-render stage")),
                };
                let blob = p
                    .stages
                    .iter()
                    .find(|s| s.stage.as_raw() == vk_stage)
                    .and_then(|s| s.constant_blob.as_deref())
                    .ok_or_else(|| invalid("Render constant blob is absent"))?;
                d.source_kind = 5;
                d.bytes = blob.as_ptr().cast();
                d.byte_count = blob.len();
                d.range = (blob.len() as u64 + 15) & !15;
            }
            WriteSource::ColorWriteTable => {
                if !matches!(d.vulkan_type, 6 | 7) || d.array_element != 0 {
                    return Err(invalid("Invalid canonical color-table descriptor"));
                }
                let bytes = metal2vulkan::bind::color_write_table();
                d.source_kind = 6;
                d.bytes = bytes.as_ptr().cast();
                d.byte_count = bytes.len();
                d.range = bytes.len() as u64;
            }
            WriteSource::Neutral(NeutralResource::ZeroBuffer { min_bytes }) => {
                d.source_kind = 7;
                d.range = u64::from(*min_bytes);
            }
            other => return Err(unsupported(format!("Render descriptor source {other:?}"))),
        }
        descriptors.push(d);
    }
    let pushes = commands
        .push_constants
        .iter()
        .map(|p| Push {
            vulkan_stages: p.stages.as_raw(),
            offset: p.offset,
            size: p.bytes.len() as u32,
            bytes: p.bytes.as_ptr().cast(),
        })
        .collect();
    let specialization = plan
        .specialization
        .entries
        .iter()
        .map(|e| Specialization {
            id: e.constant_id,
            offset: e.offset,
            size: e.size as usize,
        })
        .collect();
    let mut vertex_buffers = Vec::new();
    for &(binding, offset) in &commands.buffer_offsets {
        let source = plan
            .vertex_buffers
            .iter()
            .find(|v| v.binding == binding)
            .ok_or_else(|| invalid("Vertex buffer has no canonical slot"))?;
        let stride = commands
            .vertex_strides
            .iter()
            .find(|(b, _)| *b == binding)
            .map_or(0, |(_, s)| *s);
        vertex_buffers.push(VertexBuffer {
            binding,
            metal_index: source.buffer_index,
            stride,
            offset,
        });
    }
    let (draw_kind, draw_arguments, index_offset) = match commands.draw {
        vk::DrawCommand::Draw {
            vertex_count,
            instance_count,
            first_vertex,
            first_instance,
        } => (
            0,
            [
                first_vertex,
                vertex_count,
                instance_count,
                first_instance,
                0,
            ],
            0,
        ),
        vk::DrawCommand::DrawIndexed {
            index_buffer_offset,
            index_count,
            instance_count,
            first_index,
            vertex_offset,
            first_instance,
            ..
        } => (
            1,
            [
                index_count,
                instance_count,
                first_index,
                vertex_offset as u32,
                first_instance,
            ],
            index_buffer_offset,
        ),
        _ => {
            return Err(unsupported(
                "Canonical draw requires an indirect or mesh execution path",
            ))
        }
    };
    let names: Vec<_> = p
        .stages
        .iter()
        .map(|s| cstring(s.entry_point.clone()))
        .collect::<Result<_>>()?;
    let modules = p
        .stages
        .iter()
        .zip(&names)
        .map(|(s, name)| StageModule {
            vulkan_stage: s.stage.as_raw(),
            words: s.spirv.as_ptr(),
            word_count: s.spirv.len(),
            entry_point: name.as_ptr(),
        })
        .collect();
    let backend_plan = execution_plan(p, owner)?;
    let pipeline_key = *p.key.digest();
    Ok(Box::new(OwnedDraw {
        _variant: active,
        _commands: commands,
        descriptors,
        pushes,
        specialization,
        specialization_data: plan.specialization.data,
        vertex_buffers,
        _names: names,
        modules,
        backend_plan,
        pipeline_key,
        draw_kind,
        draw_arguments,
        index_type: request.index_type,
        index_offset,
    }))
}
pub unsafe extern "C" fn prepare_draw(
    output: *const RenderOutput,
    bindings: *const Bindings,
    request: *const DrawRequest,
    result: *mut DrawPlan,
    error: *mut ShaderError,
) -> i32 {
    if !result.is_null() {
        ptr::write_bytes(result, 0, 1);
    }
    report(
        error,
        catch_unwind(AssertUnwindSafe(|| {
            if output.is_null() || bindings.is_null() || request.is_null() || result.is_null() {
                return Err(invalid("Missing render draw planning input"));
            }
            let owner = (*output)
                .private_data
                .cast::<OwnedOutput>()
                .as_ref()
                .ok_or_else(|| invalid("Render output has no compiler owner"))?;
            let plan = prepare(owner, &*bindings, &*request)?;
            *result = DrawPlan {
                abi_version: DRAW_ABI,
                descriptor_count: plan.descriptors.len() as u32,
                descriptors: plan.descriptors.as_ptr(),
                push_count: plan.pushes.len() as u32,
                pushes: plan.pushes.as_ptr(),
                specialization_count: plan.specialization.len() as u32,
                specialization: plan.specialization.as_ptr(),
                specialization_data: plan.specialization_data.as_ptr().cast(),
                specialization_size: plan.specialization_data.len(),
                vertex_buffer_count: plan.vertex_buffers.len() as u32,
                vertex_buffers: plan.vertex_buffers.as_ptr(),
                module_count: plan.modules.len() as u32,
                modules: plan.modules.as_ptr(),
                backend_plan: &*plan.backend_plan,
                pipeline_key: plan.pipeline_key,
                draw_kind: plan.draw_kind,
                draw_arguments: plan.draw_arguments,
                index_type: plan.index_type,
                index_offset: plan.index_offset,
                private_data: Box::into_raw(plan).cast(),
                destroy: Some(destroy),
            };
            Ok(())
        })),
    )
}
unsafe extern "C" fn destroy(plan: *mut DrawPlan) {
    if let Some(plan) = plan.as_mut() {
        if !plan.private_data.is_null() {
            drop(Box::from_raw(plan.private_data.cast::<OwnedDraw>()));
        }
        ptr::write_bytes(plan, 0, 1);
    }
}
