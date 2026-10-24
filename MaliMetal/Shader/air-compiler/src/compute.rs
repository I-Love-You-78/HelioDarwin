//! Real AIR kernels and per-dispatch binding/region planning. Local size and
//! synthetic threadgroup scratch come from the compiler's binder, not guesses.
use super::{
    bytes, constants, cstring, invalid, name, report, Failure, Result, ShaderError, StageModule,
};
use metal2vulkan::{
    bind::{
        indirect_clamp_module, sampler_desc, BindState, BoundBuffer, BoundSampler, BoundTexture,
        Dispatch, DispatchCommands, ImageSource, ImageblockSize, IndirectArgs, IndirectClampModule,
        IndirectRef, SamplerDescriptor, SamplerSource, StageInRegion, StageTable, TextureFacts,
        WriteSource,
    },
    device::{Device, DeviceDescription, DevicePolicy},
    library::Library,
    metal,
    pipeline::{
        BindingSource, BufferForm, BufferKind, ComputePipeline, ComputePipelineDescriptor,
        FunctionDescriptor, SlotInterface, Stage, StageSource, SyntheticBinding,
    },
    vk, Budget,
};
use std::{
    ffi::{c_char, c_void, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    time::{Duration, Instant},
};

const COMPUTE_ABI: u32 = 2;
const BUFFER: u32 = 1;
const CONSTANT: u32 = 2;
const TEXTURE: u32 = 3;
const SAMPLER: u32 = 4;
const TABLE: u32 = 5;
const COMBINED: u32 = 6;
const STATUS: u32 = 7;
fn unsupported(s: impl Into<String>) -> Failure {
    Failure(7, s.into())
}
fn bind_error(e: metal2vulkan::error::BindError) -> Failure {
    unsupported(e.to_string())
}

#[repr(C)]
pub struct ComputeRequest {
    library_bytes: *const c_void,
    library_size: usize,
    entry_name: *const c_char,
    device_profile_json: *const c_char,
    device_profile_size: usize,
    constants: *const super::ConstantValue,
    constant_count: usize,
    max_total_threads_per_threadgroup: u32,
    multiple_of_execution_width: u32,
    required_threads_per_threadgroup: [u32; 3],
    max_call_stack_depth: u32,
    timeout_ms: u32,
}
#[repr(C)]
pub struct ComputeBinding {
    set: u32,
    binding: u32,
    vulkan_type: u32,
    source_kind: u32,
    metal_index: u32,
    read: u32,
    write: u32,
    constant_bytes: *const u8,
    constant_size: usize,
}
#[repr(C)]
pub struct ComputeBuffer {
    length: u64,
    offset: u64,
    bound: u32,
    gpu_address: u64,
    device_address: u64,
    attribute_stride: u32,
    has_attribute_stride: u32,
}
#[repr(C)]
pub struct ComputeTexture {
    bound: u32,
    pixel_format: u32,
    texture_type: u32,
    sample_count: u32,
    usage: u32,
    array_length: u32,
    mipmap_level_count: u32,
    sparse: u32,
    depth_twin: u32,
    swizzle: [u32; 4],
    parent_relative_level: u32,
    parent_base_size: [u32; 3],
    own_format_views: u32,
    resource_id: u64,
}
#[repr(C)]
pub struct ComputeSampler {
    bound: u32,
    min_filter: u32,
    mag_filter: u32,
    mip_filter: u32,
    address: [u32; 3],
    border_color: u32,
    normalized_coordinates: u32,
    lod_average: u32,
    max_anisotropy: u32,
    compare_function: u32,
    reduction_mode: u32,
    lod_min_clamp: f32,
    lod_max_clamp: f32,
}
#[repr(C)]
pub struct ComputeBindings {
    buffers: [ComputeBuffer; 31],
    textures: [ComputeTexture; 128],
    threadgroup_lengths: [u32; 31],
    samplers: [ComputeSampler; 16],
    stage_in_region_bound: u32,
    stage_in_origin: [u32; 3],
    stage_in_size: [u32; 3],
    imageblock_size: [u32; 2],
}
#[repr(C)]
pub struct DispatchRequest {
    kind: u32,
    grid: [u32; 3],
    threads_per_threadgroup: [u32; 3],
    indirect_length: u64,
    indirect_offset: u64,
}
#[repr(C)]
pub struct SpecEntry {
    id: u32,
    offset: u32,
    size: usize,
}
#[repr(C)]
pub struct Descriptor {
    set: u32,
    binding: u32,
    vulkan_type: u32,
    source_kind: u32,
    metal_index: u32,
    offset: u64,
    range: u64,
    view_type: u32,
    view_format: u32,
    view_aspect: u32,
    view_layout: u32,
    view_components: [u32; 4],
    view_level: u32,
    view_has_level: u32,
    view_twin: u32,
    view_shader_swizzle: u32,
    sampler_min_filter: u32,
    sampler_mag_filter: u32,
    sampler_mip_filter: u32,
    sampler_address: [u32; 3],
    sampler_compare_enabled: u32,
    sampler_compare_op: u32,
    sampler_border_color: u32,
    sampler_min_lod: f32,
    sampler_max_lod: f32,
    sampler_lod_bias: f32,
    sampler_max_anisotropy: f32,
    sampler_reduction: u32,
    sampler_has_border_components: u32,
    sampler_border_components: [u32; 4],
    upload_bytes: *const u8,
    upload_size: usize,
}
#[repr(C)]
pub struct Push {
    offset: u32,
    size: u32,
    bytes: *const u8,
}
#[repr(C)]
pub struct Region {
    group_count: [u32; 3],
    specialization: *const SpecEntry,
    specialization_count: u32,
    specialization_data: *const u8,
    specialization_size: usize,
    pushes: *const Push,
    push_count: u32,
    stage_flags: u32,
    required_subgroup_size: u32,
    indirect_kind: u32,
    indirect_offset: u64,
}
#[repr(C)]
pub struct DispatchPlan {
    abi_version: u32,
    descriptor_count: u32,
    region_count: u32,
    descriptors: *const Descriptor,
    regions: *const Region,
    private_data: *mut c_void,
    destroy: Option<unsafe extern "C" fn(*mut DispatchPlan)>,
    stage: StageModule,
    bindings: *const ComputeBinding,
    binding_count: u32,
    push_constant_size: u32,
    indirect_clamp_stage: StageModule,
    indirect_source_offset: u64,
    indirect_source_range: u64,
    indirect_residual: u32,
}
#[repr(C)]
pub struct ComputeOutput {
    abi_version: u32,
    stage: StageModule,
    bindings: *const ComputeBinding,
    binding_count: u32,
    push_constant_size: u32,
    max_total_threads_per_threadgroup: u32,
    thread_execution_width: u32,
    static_threadgroup_memory_length: u32,
    metadata_json: *const c_char,
    metadata_size: usize,
    private_data: *mut c_void,
    destroy: Option<unsafe extern "C" fn(*mut ComputeOutput)>,
    prepare_dispatch: Option<
        unsafe extern "C" fn(
            *const ComputeOutput,
            *const ComputeBindings,
            *const DispatchRequest,
            *mut DispatchPlan,
            *mut ShaderError,
        ) -> i32,
    >,
}
struct OwnedCompute {
    device: Device,
    pipeline: ComputePipeline,
    entry_name: CString,
    metadata: CString,
    bindings: Vec<ComputeBinding>,
    required_threads: [u32; 3],
    descriptor: ComputePipelineDescriptor,
}

fn bindings(p: &ComputePipeline) -> Result<Vec<ComputeBinding>> {
    if p.interface.needs_pointer_translation
        || p.interface.needs_handle_translation
        || p.interface.reaches_command_buffers
    {
        return Err(unsupported("Compute address/handle translation or encoded commands require the resident resource registry"));
    }
    let mut out = Vec::new();
    for set in &p.layout.sets {
        if !set.flags.is_empty() {
            return Err(unsupported("Compute descriptor set flags"));
        }
        for b in &set.bindings {
            if b.count != 1 || !b.flags.is_empty() || b.stages.as_raw() != 32 {
                return Err(unsupported(
                    "Compute descriptor arrays, flags, immutable samplers or mixed stages",
                ));
            }
            let mut binding = ComputeBinding {
                set: set.set,
                binding: b.binding,
                vulkan_type: b.ty.as_raw() as u32,
                source_kind: 0,
                metal_index: u32::MAX,
                read: 0,
                write: 0,
                constant_bytes: ptr::null(),
                constant_size: 0,
            };
            match b.source {
                BindingSource::Buffer(slot) if slot.stage == Stage::Kernel && slot.index < 31 => {
                    let Some(SlotInterface::Buffer(buffer)) = p.interface.slots.get(&slot) else {
                        return Err(unsupported("Compute buffer has no reflected interface"));
                    };
                    if buffer.kind != BufferKind::Buffer
                        || !matches!(buffer.form, BufferForm::Descriptor(_))
                        || (b.ty != vk::DescriptorType::UNIFORM_BUFFER
                            && b.ty != vk::DescriptorType::STORAGE_BUFFER)
                    {
                        return Err(unsupported(
                            "Compute argument/function-table or address-based buffer",
                        ));
                    }
                    binding.source_kind = BUFFER;
                    binding.metal_index = slot.index;
                    binding.read = u32::from(buffer.access.read);
                    binding.write = u32::from(buffer.access.write);
                }
                BindingSource::Texture(slot) if slot.stage == Stage::Kernel && slot.index < 128 => {
                    let Some(SlotInterface::Texture(texture)) = p.interface.slots.get(&slot) else {
                        return Err(unsupported("Compute texture has no reflected interface"));
                    };
                    if b.ty != vk::DescriptorType::SAMPLED_IMAGE
                        && b.ty != vk::DescriptorType::STORAGE_IMAGE
                    {
                        return Err(unsupported(
                            "Compute texel-buffer descriptor requires native texel views",
                        ));
                    }
                    binding.source_kind = TEXTURE;
                    binding.metal_index = slot.index;
                    binding.read = u32::from(texture.access.read || texture.access.sample);
                    binding.write = u32::from(texture.access.write);
                }
                BindingSource::RawTexture(slot)
                | BindingSource::SeparateTexture { texture: slot, .. }
                    if slot.stage == Stage::Kernel && slot.index < 128 =>
                {
                    binding.source_kind = TEXTURE;
                    binding.metal_index = slot.index;
                    binding.read = 1;
                }
                BindingSource::Sampler(slot) if slot.stage == Stage::Kernel && slot.index < 16 => {
                    binding.source_kind = SAMPLER;
                    binding.metal_index = slot.index;
                    binding.read = 1;
                }
                BindingSource::SeparateSampler {
                    stage: Stage::Kernel,
                    ..
                } => {
                    binding.source_kind = SAMPLER;
                    binding.read = 1;
                }
                BindingSource::CombinedTextureSampler { texture, .. }
                    if texture.stage == Stage::Kernel && texture.index < 128 =>
                {
                    binding.source_kind = COMBINED;
                    binding.metal_index = texture.index;
                    binding.read = 1;
                }
                BindingSource::Synthetic {
                    stage: Stage::Kernel,
                    binding:
                        SyntheticBinding::ArgumentTable
                        | SyntheticBinding::HandleTable
                        | SyntheticBinding::SamplerTable,
                } => {
                    binding.source_kind = TABLE;
                    binding.read = 1;
                }
                BindingSource::Synthetic {
                    stage: Stage::Kernel,
                    binding: SyntheticBinding::ExecutionStatus,
                } => {
                    binding.source_kind = STATUS;
                    binding.read = 1;
                    binding.write = 1;
                }
                BindingSource::Synthetic {
                    stage: Stage::Kernel,
                    binding: SyntheticBinding::ConstantBlob,
                } => {
                    let blob = p
                        .stage
                        .constant_blob
                        .as_deref()
                        .filter(|b| !b.is_empty())
                        .ok_or_else(|| unsupported("Missing compute ConstantBlob bytes"))?;
                    binding.source_kind = CONSTANT;
                    binding.read = 1;
                    binding.constant_bytes = blob.as_ptr();
                    binding.constant_size = blob.len();
                }
                _ => {
                    return Err(unsupported(format!(
                        "Compute descriptor source {:?} is unavailable",
                        b.source
                    )))
                }
            }
            out.push(binding);
        }
    }
    if out.len() > 64 {
        return Err(unsupported("More than 64 compute bindings"));
    }
    Ok(out)
}

unsafe fn compile(request: &ComputeRequest) -> Result<Box<OwnedCompute>> {
    let profile = bytes(
        request.device_profile_json.cast(),
        request.device_profile_size,
        4 * 1024 * 1024,
    )?;
    let profile =
        std::str::from_utf8(profile).map_err(|_| invalid("Compute profile is not UTF-8"))?;
    let device = Device::new(
        DeviceDescription::from_khronos_json(profile, None)?,
        DevicePolicy::UNTRUSTED,
    )?;
    let library = Library::parse(bytes(
        request.library_bytes,
        request.library_size,
        64 * 1024 * 1024,
    )?)?;
    let function = library.function(name(request.entry_name)?)?;
    let mut function = FunctionDescriptor::new(function);
    function.constants = constants(request.constants, request.constant_count)?;
    let mut descriptor = ComputePipelineDescriptor::new(StageSource::Function(function));
    if request.max_total_threads_per_threadgroup != 0 {
        descriptor.max_total_threads_per_threadgroup =
            Some(request.max_total_threads_per_threadgroup);
    }
    descriptor.threadgroup_size_is_multiple_of_execution_width =
        request.multiple_of_execution_width != 0;
    if request.required_threads_per_threadgroup != [0; 3] {
        if request.required_threads_per_threadgroup.contains(&0) {
            return Err(invalid("Partial required compute threadgroup size"));
        }
        descriptor.required_threads_per_threadgroup =
            Some(request.required_threads_per_threadgroup);
    }
    descriptor.max_call_stack_depth = request.max_call_stack_depth.max(1);
    let timeout = if request.timeout_ms == 0 {
        20_000
    } else {
        request.timeout_ms.min(20_000)
    };
    let budget =
        Budget::with_deadline(Some(Instant::now() + Duration::from_millis(timeout as u64)));
    let pipeline = device.compile_compute_pipeline(&descriptor, &budget)?;
    let bindings = bindings(&pipeline)?;
    let entry_name = cstring(pipeline.stage.entry_point.clone())?;
    let metadata = serde_json::json!({
        "abi_version": COMPUTE_ABI, "kind": "compute", "profile": pipeline.profile,
        "compiler_revision": "a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b",
        "layout": pipeline.layout, "interface": pipeline.interface,
        "dependencies": pipeline.dependencies, "metal_info": pipeline.metal_info,
        "specialization": pipeline.stage.specialization,
        "stage_flags": pipeline.stage.stage_flags,
        "required_subgroup_size": pipeline.stage.required_subgroup_size,
        "precision_notes": pipeline.stage.precision_notes,
        "requirements": format!("{:?}", pipeline.requirements),
        "constant_blob": pipeline.stage.constant_blob.as_deref(),
    });
    Ok(Box::new(OwnedCompute {
        device,
        pipeline,
        entry_name,
        metadata: cstring(serde_json::to_string(&metadata).map_err(|e| invalid(e.to_string()))?)?,
        bindings,
        required_threads: request.required_threads_per_threadgroup,
        descriptor,
    }))
}

pub unsafe extern "C" fn compile_compute(
    request: *const ComputeRequest,
    output: *mut ComputeOutput,
    error: *mut ShaderError,
) -> i32 {
    if !output.is_null() {
        ptr::write_bytes(output, 0, 1);
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        if request.is_null() || output.is_null() {
            return Err(invalid("Missing compute request or result"));
        }
        let owner = compile(&*request)?;
        let p = &owner.pipeline;
        let push_size = p
            .layout
            .push_constants
            .iter()
            .map(|r| r.offset.saturating_add(r.size))
            .max()
            .unwrap_or(0);
        *output = ComputeOutput {
            abi_version: COMPUTE_ABI,
            stage: StageModule {
                vulkan_stage: 32,
                words: p.stage.spirv.as_ptr(),
                word_count: p.stage.spirv.len(),
                entry_point: owner.entry_name.as_ptr(),
            },
            bindings: owner.bindings.as_ptr(),
            binding_count: owner.bindings.len() as u32,
            push_constant_size: push_size,
            max_total_threads_per_threadgroup: p.metal_info.max_total_threads_per_threadgroup,
            thread_execution_width: p.metal_info.thread_execution_width,
            static_threadgroup_memory_length: p.metal_info.static_threadgroup_memory_length,
            metadata_json: owner.metadata.as_ptr(),
            metadata_size: owner.metadata.as_bytes().len(),
            private_data: Box::into_raw(owner).cast(),
            destroy: Some(destroy_compute),
            prepare_dispatch: Some(prepare_dispatch),
        };
        Ok(())
    }));
    report(error, result)
}
unsafe extern "C" fn destroy_compute(output: *mut ComputeOutput) {
    if let Some(output) = output.as_mut() {
        if !output.private_data.is_null() {
            drop(Box::from_raw(output.private_data.cast::<OwnedCompute>()));
        }
        ptr::write_bytes(output, 0, 1);
    }
}

struct OwnedDispatch {
    _commands: DispatchCommands,
    pipeline: ComputePipeline,
    bindings: Vec<ComputeBinding>,
    entry_name: CString,
    clamp: Option<IndirectClampModule>,
    descriptors: Vec<Descriptor>,
    _entries: Vec<Vec<SpecEntry>>,
    _pushes: Vec<Vec<Push>>,
    regions: Vec<Region>,
}
fn texture_facts(t: &ComputeTexture) -> Result<TextureFacts> {
    if t.sample_count == 0 || t.array_length == 0 || t.mipmap_level_count == 0 {
        return Err(invalid(
            "Missing actual compute texture dimensions/levels/sample facts",
        ));
    }
    let mut f = TextureFacts::new(
        metal::TextureType::try_from(t.texture_type)
            .map_err(|_| invalid("Unknown texture type"))?,
        metal::PixelFormat::try_from(t.pixel_format)
            .map_err(|_| invalid("Unknown texture format"))?,
    );
    f.sample_count = t.sample_count;
    f.usage =
        metal::TextureUsage::try_from(t.usage).map_err(|_| invalid("Unknown texture usage"))?;
    f.array_length = t.array_length;
    f.mipmap_level_count = t.mipmap_level_count;
    f.sparse = t.sparse != 0;
    f.depth_twin = t.depth_twin != 0;
    let swizzle = t.swizzle.map(|v| {
        metal::TextureSwizzle::try_from(v).map_err(|_| invalid("Unknown texture swizzle"))
    });
    let [r, g, b, a] = swizzle;
    f.swizzle = metal::TextureSwizzleChannels::new(r?, g?, b?, a?);
    f.parent_relative_level = t.parent_relative_level;
    f.parent_base_size = t.parent_base_size;
    f.own_format_views = t.own_format_views != 0;
    Ok(f)
}
fn bound_sampler(s: &ComputeSampler) -> Result<BoundSampler> {
    let mut d = SamplerDescriptor::new();
    macro_rules! cv {
        ($ty:ident, $v:expr) => {
            metal::$ty::try_from($v)
                .map_err(|_| invalid(concat!("Unknown sampler ", stringify!($ty))))?
        };
    }
    d.min_filter = cv!(SamplerMinMagFilter, s.min_filter);
    d.mag_filter = cv!(SamplerMinMagFilter, s.mag_filter);
    d.mip_filter = cv!(SamplerMipFilter, s.mip_filter);
    d.s_address_mode = cv!(SamplerAddressMode, s.address[0]);
    d.t_address_mode = cv!(SamplerAddressMode, s.address[1]);
    d.r_address_mode = cv!(SamplerAddressMode, s.address[2]);
    d.border_color = cv!(SamplerBorderColor, s.border_color);
    d.normalized_coordinates = s.normalized_coordinates != 0;
    d.lod_min_clamp = s.lod_min_clamp;
    d.lod_max_clamp = s.lod_max_clamp;
    d.lod_average = s.lod_average != 0;
    d.max_anisotropy = s.max_anisotropy;
    d.compare_function = cv!(CompareFunction, s.compare_function);
    d.reduction_mode = cv!(SamplerReductionMode, s.reduction_mode);
    Ok(BoundSampler::new(d))
}
fn set_view(d: &mut Descriptor, v: &metal2vulkan::bind::ImageViewDesc) {
    d.view_type = v.view_type.as_raw() as u32;
    d.view_format = v.format.as_raw() as u32;
    d.view_aspect = v.aspect.as_raw();
    d.view_layout = v.layout.as_raw() as u32;
    d.view_components = [
        v.components.r,
        v.components.g,
        v.components.b,
        v.components.a,
    ]
    .map(|c| c.as_raw() as u32);
    d.view_level = v.level.unwrap_or(0);
    d.view_has_level = u32::from(v.level.is_some());
    d.view_twin = u32::from(v.twin);
    d.view_shader_swizzle = u32::from(v.shader_swizzle);
}
fn set_sampler(d: &mut Descriptor, s: &vk::SamplerDesc) {
    d.sampler_min_filter = s.min_filter.as_raw() as u32;
    d.sampler_mag_filter = s.mag_filter.as_raw() as u32;
    d.sampler_mip_filter = s.mipmap_mode.as_raw() as u32;
    d.sampler_address =
        [s.address_mode_u, s.address_mode_v, s.address_mode_w].map(|a| a.as_raw() as u32);
    d.sampler_compare_enabled = u32::from(s.compare.is_some());
    d.sampler_compare_op = s.compare.map_or(0, |v| v.as_raw() as u32);
    d.sampler_border_color = s.border_color.as_raw() as u32;
    d.sampler_min_lod = s.min_lod;
    d.sampler_max_lod = s.max_lod;
    d.sampler_lod_bias = s.mip_lod_bias;
    d.sampler_max_anisotropy = s.max_anisotropy.unwrap_or(1.0);
    d.sampler_reduction = s.reduction.as_raw() as u32;
    if let Some(c) = s.border_components {
        d.sampler_has_border_components = 1;
        d.sampler_border_components = [c.r, c.g, c.b, c.a].map(|c| c.as_raw() as u32);
    }
}
fn prepare(
    owner: &OwnedCompute,
    input: &ComputeBindings,
    request: &DispatchRequest,
) -> Result<Box<OwnedDispatch>> {
    if request.threads_per_threadgroup.contains(&0) || request.kind > 2 {
        return Err(invalid(
            "Compute threadgroup dimensions must be positive and dispatch kind known",
        ));
    }
    if owner.required_threads != [0; 3] && owner.required_threads != request.threads_per_threadgroup
    {
        return Err(invalid(
            "Dispatch threadgroup size differs from the required pipeline size",
        ));
    }
    if request.kind == 2
        && (request.indirect_offset % 4 != 0
            || request.indirect_offset > request.indirect_length
            || 12 > request.indirect_length - request.indirect_offset)
    {
        return Err(invalid(
            "Compute indirect arguments exceed the logical source or are not four-byte aligned",
        ));
    }
    let mut state = BindState::default();
    let mut table = StageTable::default();
    for (index, b) in input.buffers.iter().enumerate() {
        if b.bound != 0 {
            if b.length == 0 || b.offset > b.length {
                return Err(invalid(
                    "Compute buffer range exceeds its logical allocation",
                ));
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
    }
    for (index, t) in input.textures.iter().enumerate() {
        if t.bound != 0 {
            table.textures.insert(
                index as u32,
                BoundTexture::Texture {
                    facts: texture_facts(t)?,
                    resource_id: (t.resource_id != 0).then_some(t.resource_id),
                },
            );
        }
    }
    for (index, s) in input.samplers.iter().enumerate() {
        if s.bound != 0 {
            table.samplers.insert(index as u32, bound_sampler(s)?);
        }
    }
    state.imageblock_size = ImageblockSize::new(input.imageblock_size[0], input.imageblock_size[1]);
    if input.stage_in_region_bound != 0 {
        state.stage_in_region = Some(StageInRegion::new(
            input.stage_in_origin,
            input.stage_in_size,
        ));
    }
    for (index, &length) in input.threadgroup_lengths.iter().enumerate() {
        if length != 0 {
            table.threadgroup_lengths.insert(index as u32, length);
        }
    }
    // Every statically used resource must really be bound. Neutral resources,
    // address roots and inline uploads are not fabricated by this adapter.
    for binding in &owner.bindings {
        match binding.source_kind {
            BUFFER if input.buffers[binding.metal_index as usize].bound == 0 => {
                return Err(invalid("A statically used compute buffer is unbound"))
            }
            TEXTURE if input.textures[binding.metal_index as usize].bound == 0 => {
                return Err(invalid("A statically used compute texture is unbound"))
            }
            _ => (),
        }
    }
    state.stages.insert(Stage::Kernel, table);
    let dispatch = match request.kind {
        0 => Dispatch::Threadgroups {
            groups: request.grid,
            threads_per_group: request.threads_per_threadgroup,
        },
        1 => Dispatch::Threads {
            threads: request.grid,
            threads_per_group: request.threads_per_threadgroup,
        },
        _ => Dispatch::ThreadgroupsIndirect {
            indirect: IndirectRef::new(0, request.indirect_offset),
            threads_per_group: request.threads_per_threadgroup,
        },
    };
    // Settle both resource and dispatch variants. An edge region may need emulated
    // simdgroups even when the nominal threadgroup fits the original module.
    let mut pipeline = None;
    let mut current = metal2vulkan::cache::VariantKey::new();
    let mut seen = Vec::new();
    let budget = Budget::with_deadline(Some(Instant::now() + Duration::from_secs(20)));
    let plan = loop {
        let p = pipeline.as_ref().unwrap_or(&owner.pipeline);
        let plan = p.plan(&owner.device, &state.shape()).map_err(bind_error)?;
        let needed = plan.dispatch_variant(&dispatch).map_err(bind_error)?;
        if needed == current {
            break plan;
        }
        if seen.contains(&needed) || seen.len() == 16 {
            return Err(unsupported(
                "Compute shader/resource variant did not converge",
            ));
        }
        seen.push(needed.clone());
        let StageSource::Function(function) = &owner.descriptor.kernel else {
            return Err(unsupported(
                "Compute variant lacks its original AIR function",
            ));
        };
        let mut inputs = needed.lowering_inputs(Stage::Kernel);
        inputs.stage_input = owner.descriptor.stage_input.clone();
        inputs.max_call_stack_depth = (owner.descriptor.max_call_stack_depth > 1)
            .then_some(owner.descriptor.max_call_stack_depth);
        let compiled = owner.device.compile_function(function, &inputs, &budget)?;
        let mut descriptor = owner.descriptor.clone();
        descriptor.kernel = StageSource::Compiled(compiled);
        pipeline = Some(
            owner
                .device
                .compile_compute_pipeline(&descriptor, &budget)?,
        );
        current = needed;
    };
    // The plan owns its pipeline, including a generic module when no variant is needed.
    let pipeline = pipeline.unwrap_or_else(|| owner.pipeline.clone());
    let bindings = bindings(&pipeline)?;
    let entry_name = cstring(pipeline.stage.entry_point.clone())?;
    let commands = plan
        .instantiate_dispatch(&state, &dispatch)
        .map_err(bind_error)?;
    if !commands.push_holes.is_empty() || commands.root_table.is_some() {
        return Err(unsupported(
            "Compute resident root tables and address push holes require native device addresses",
        ));
    }
    let clamp = commands
        .indirect_clamp
        .as_ref()
        .map(|_| indirect_clamp_module(&owner.device))
        .transpose()?;
    let mut descriptors = Vec::new();
    for write in &plan.descriptors {
        if write.array_element != 0 {
            return Err(unsupported("Compute descriptor arrays"));
        }
        let mut descriptor = Descriptor {
            set: write.set,
            binding: write.binding,
            vulkan_type: write.ty.as_raw() as u32,
            source_kind: 0,
            metal_index: u32::MAX,
            offset: 0,
            range: 0,
            ..unsafe { std::mem::zeroed() }
        };
        match &write.source {
            WriteSource::Buffer { slot } if slot.stage == Stage::Kernel => {
                let range = commands
                    .descriptor_ranges
                    .iter()
                    .find(|r| r.set == write.set && r.binding == write.binding)
                    .ok_or_else(|| {
                        unsupported("Compute buffer descriptor has no instantiated range")
                    })?;
                descriptor.source_kind = BUFFER;
                descriptor.metal_index = slot.index;
                descriptor.offset = range.range.offset;
                descriptor.range = range.range.range;
            }
            WriteSource::Texture { slot, view }
                if slot.stage == Stage::Kernel && slot.index < 128 =>
            {
                set_view(&mut descriptor, view);
                descriptor.source_kind = TEXTURE;
                descriptor.metal_index = slot.index;
            }
            WriteSource::Sampler {
                slot,
                compare,
                integer,
                cube_gradient,
                opaque_black,
            } if slot.stage == Stage::Kernel && slot.index < 16 => {
                let sampler = state.stages[&Stage::Kernel]
                    .samplers
                    .get(&slot.index)
                    .ok_or_else(|| invalid("A statically used compute sampler is unbound"))?;
                let use_ = metal::sampler::SamplerUse::new(*compare, *integer)
                    .with_cube_gradient(*cube_gradient);
                set_sampler(
                    &mut descriptor,
                    &sampler_desc(&owner.device, &sampler.desc, use_, *opaque_black, None)
                        .map_err(bind_error)?,
                );
                descriptor.source_kind = SAMPLER;
                descriptor.metal_index = slot.index;
            }
            WriteSource::CombinedImageSampler {
                image: ImageSource::Texture { slot, view },
                sampler,
            } if slot.stage == Stage::Kernel && slot.index < 128 => {
                set_view(&mut descriptor, view);
                let s = match sampler {
                    SamplerSource::Slot {
                        slot,
                        compare,
                        integer,
                        cube_gradient,
                        opaque_black,
                    } => {
                        let sampler = state.stages[&Stage::Kernel]
                            .samplers
                            .get(&slot.index)
                            .ok_or_else(|| {
                                invalid("A statically used compute sampler is unbound")
                            })?;
                        sampler_desc(
                            &owner.device,
                            &sampler.desc,
                            metal::sampler::SamplerUse::new(*compare, *integer)
                                .with_cube_gradient(*cube_gradient),
                            *opaque_black,
                            Some(view.format),
                        )
                        .map_err(bind_error)?
                    }
                    SamplerSource::Immutable => pipeline
                        .layout
                        .sets
                        .iter()
                        .find(|s| s.set == write.set)
                        .and_then(|s| s.bindings.iter().find(|b| b.binding == write.binding))
                        .and_then(|b| b.immutable_sampler)
                        .ok_or_else(|| invalid("Missing constexpr sampler"))?,
                    _ => {
                        return Err(unsupported(
                            "Compute neutral sampler needs a native neutral resource",
                        ))
                    }
                };
                set_sampler(&mut descriptor, &s);
                descriptor.source_kind = COMBINED;
                descriptor.metal_index = slot.index;
            }
            WriteSource::ArgumentTable { .. } | WriteSource::HandleTable { .. } => {
                let upload = commands
                    .descriptor_bytes
                    .iter()
                    .find(|b| b.set == write.set && b.binding == write.binding)
                    .ok_or_else(|| invalid("Missing instantiated compute table bytes"))?;
                descriptor.source_kind = TABLE;
                descriptor.range = upload.bytes.len() as u64;
                descriptor.upload_bytes = upload.bytes.as_ptr();
                descriptor.upload_size = upload.bytes.len();
            }
            WriteSource::ExecutionStatus => {
                descriptor.source_kind = STATUS;
                descriptor.range = 32;
            }
            WriteSource::ConstantBlob {
                stage: Stage::Kernel,
            } => {
                descriptor.source_kind = CONSTANT;
                descriptor.range = pipeline
                    .stage
                    .constant_blob
                    .as_ref()
                    .map_or(0, |b| (b.len() as u64 + 15) & !15);
            }
            other => {
                return Err(unsupported(format!(
                    "Compute descriptor write {:?} is unavailable",
                    other
                )))
            }
        }
        descriptors.push(descriptor);
    }
    // Vulkan's native layout uses ordinary samplers. Materialize a layout's
    // constexpr-only sampler even where the canonical planner needs no write.
    for set in &pipeline.layout.sets {
        for binding in &set.bindings {
            if binding.ty == vk::DescriptorType::SAMPLER
                && binding.immutable_sampler.is_some()
                && !descriptors
                    .iter()
                    .any(|d| d.set == set.set && d.binding == binding.binding)
            {
                let mut d: Descriptor = unsafe { std::mem::zeroed() };
                d.set = set.set;
                d.binding = binding.binding;
                d.vulkan_type = 0;
                d.source_kind = SAMPLER;
                d.metal_index = u32::MAX;
                set_sampler(&mut d, binding.immutable_sampler.as_ref().unwrap());
                descriptors.push(d);
            }
        }
    }
    if descriptors.len() != bindings.len() {
        return Err(unsupported(
            "Compute plan descriptor count differs from its settled native layout",
        ));
    }
    let mut entries = Vec::new();
    let mut pushes = Vec::new();
    for region in &commands.regions {
        entries.push(
            region
                .specialization
                .entries
                .iter()
                .map(|e| SpecEntry {
                    id: e.constant_id,
                    offset: e.offset,
                    size: e.size as usize,
                })
                .collect::<Vec<_>>(),
        );
        pushes.push(
            region
                .push_constants
                .iter()
                .map(|p| {
                    if p.stages.as_raw() != 32 {
                        return Err(unsupported("Non-compute push stage"));
                    }
                    Ok(Push {
                        offset: p.offset,
                        size: p.bytes.len() as u32,
                        bytes: p.bytes.as_ptr(),
                    })
                })
                .collect::<Result<Vec<_>>>()?,
        );
    }
    let regions = commands
        .regions
        .iter()
        .enumerate()
        .map(|(index, region)| Region {
            group_count: region.group_count,
            specialization: entries[index].as_ptr(),
            specialization_count: entries[index].len() as u32,
            specialization_data: region.specialization.data.as_ptr(),
            specialization_size: region.specialization.data.len(),
            pushes: pushes[index].as_ptr(),
            push_count: pushes[index].len() as u32,
            stage_flags: region.stage_flags.as_raw(),
            required_subgroup_size: region.required_subgroup_size.unwrap_or(0),
            indirect_kind: match region.indirect {
                None => 0,
                Some(IndirectArgs::Buffer(_)) => 1,
                Some(IndirectArgs::ClampScratch) => 2,
                _ => unreachable!("Non-replay dispatch cannot produce replay arguments"),
            },
            indirect_offset: match region.indirect {
                Some(IndirectArgs::Buffer(r)) => r.offset,
                _ => 0,
            },
        })
        .collect();
    Ok(Box::new(OwnedDispatch {
        _commands: commands,
        pipeline,
        bindings,
        entry_name,
        clamp,
        descriptors,
        _entries: entries,
        _pushes: pushes,
        regions,
    }))
}

unsafe extern "C" fn prepare_dispatch(
    output: *const ComputeOutput,
    bindings: *const ComputeBindings,
    request: *const DispatchRequest,
    result: *mut DispatchPlan,
    error: *mut ShaderError,
) -> i32 {
    if !result.is_null() {
        ptr::write_bytes(result, 0, 1);
    }
    let status = catch_unwind(AssertUnwindSafe(|| {
        if output.is_null() || bindings.is_null() || request.is_null() || result.is_null() {
            return Err(invalid("Missing compute dispatch input"));
        }
        let owner = (*output)
            .private_data
            .cast::<OwnedCompute>()
            .as_ref()
            .ok_or_else(|| invalid("Missing compiled compute program"))?;
        let dispatch = prepare(owner, &*bindings, &*request)?;
        *result = DispatchPlan {
            abi_version: COMPUTE_ABI,
            descriptor_count: dispatch.descriptors.len() as u32,
            region_count: dispatch.regions.len() as u32,
            descriptors: dispatch.descriptors.as_ptr(),
            regions: dispatch.regions.as_ptr(),
            private_data: (&*dispatch as *const OwnedDispatch).cast_mut().cast(),
            destroy: Some(destroy_dispatch),
            stage: StageModule {
                vulkan_stage: 32,
                words: dispatch.pipeline.stage.spirv.as_ptr(),
                word_count: dispatch.pipeline.stage.spirv.len(),
                entry_point: dispatch.entry_name.as_ptr(),
            },
            bindings: dispatch.bindings.as_ptr(),
            binding_count: dispatch.bindings.len() as u32,
            push_constant_size: dispatch
                .pipeline
                .layout
                .push_constants
                .iter()
                .map(|r| r.offset.saturating_add(r.size))
                .max()
                .unwrap_or(0),
            indirect_clamp_stage: match &dispatch.clamp {
                Some(c) => StageModule {
                    vulkan_stage: 32,
                    words: c.spirv.as_ptr(),
                    word_count: c.spirv.len(),
                    entry_point: b"main\0".as_ptr().cast(),
                },
                None => unsafe { std::mem::zeroed() },
            },
            indirect_source_offset: dispatch
                ._commands
                .indirect_clamp
                .as_ref()
                .map_or(0, |c| c.source_range.offset),
            indirect_source_range: dispatch
                ._commands
                .indirect_clamp
                .as_ref()
                .map_or(0, |c| c.source_range.range),
            indirect_residual: dispatch
                ._commands
                .indirect_clamp
                .as_ref()
                .map_or(0, |c| u32::from_le_bytes(c.push_constants)),
        };
        let _ = Box::into_raw(dispatch);
        Ok(())
    }));
    report(error, status)
}
unsafe extern "C" fn destroy_dispatch(plan: *mut DispatchPlan) {
    if let Some(plan) = plan.as_mut() {
        if !plan.private_data.is_null() {
            drop(Box::from_raw(plan.private_data.cast::<OwnedDispatch>()));
        }
        ptr::write_bytes(plan, 0, 1);
    }
}
