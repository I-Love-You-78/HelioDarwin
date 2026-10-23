//! Real AIR kernels and per-dispatch binding/region planning. Local size and
//! synthetic threadgroup scratch come from the compiler's binder, not guesses.
use super::{
    bytes, constants, cstring, invalid, name, report, Failure, Result, ShaderError, StageModule,
};
use metal2vulkan::{
    bind::{
        BindState, BoundBuffer, BoundTexture, Dispatch, DispatchCommands, StageTable, TextureFacts,
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

const COMPUTE_ABI: u32 = 1;
const BUFFER: u32 = 1;
const CONSTANT: u32 = 2;
const TEXTURE: u32 = 3;
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
}
#[repr(C)]
pub struct ComputeTexture {
    bound: u32,
    pixel_format: u32,
}
#[repr(C)]
pub struct ComputeBindings {
    buffers: [ComputeBuffer; 31],
    textures: [ComputeTexture; 128],
    threadgroup_lengths: [u32; 31],
}
#[repr(C)]
pub struct DispatchRequest {
    kind: u32,
    grid: [u32; 3],
    threads_per_threadgroup: [u32; 3],
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
}

fn bindings(p: &ComputePipeline) -> Result<Vec<ComputeBinding>> {
    if p.interface.needs_pointer_translation
        || p.interface.needs_handle_translation
        || p.interface.reaches_command_buffers
        || !p.interface.texture_writing_stages.is_empty()
    {
        return Err(unsupported("Compute address/handle translation, encoded commands or texture writes are unavailable"));
    }
    let mut out = Vec::new();
    for set in &p.layout.sets {
        if !set.flags.is_empty() {
            return Err(unsupported("Compute descriptor set flags"));
        }
        for b in &set.bindings {
            if b.count != 1
                || !b.flags.is_empty()
                || b.immutable_sampler.is_some()
                || b.stages.as_raw() != 32
            {
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
                        || texture.texture_type != metal::TextureType::Type2D
                        || texture.sampled_type != metal2vulkan::pipeline::SampledType::Float
                        || texture.depth_compare
                        || !texture.access.read
                        || texture.access.sample
                        || texture.access.write
                    {
                        return Err(unsupported(
                            "Compute texture requires a storage/sample/format variant",
                        ));
                    }
                    binding.source_kind = TEXTURE;
                    binding.metal_index = slot.index;
                    binding.read = 1;
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
    descriptors: Vec<Descriptor>,
    _entries: Vec<Vec<SpecEntry>>,
    _pushes: Vec<Vec<Push>>,
    regions: Vec<Region>,
}
fn image_view_ok(view: &metal2vulkan::bind::ImageViewDesc, pixel: u32) -> bool {
    let expected = match pixel {
        70 => vk::Format::R8G8B8A8_UNORM,
        80 => vk::Format::B8G8R8A8_UNORM,
        _ => return false,
    };
    view.view_type == vk::ImageViewType::TYPE_2D
        && view.format == expected
        && view.aspect == vk::ImageAspectFlags::COLOR
        && view.components == vk::ComponentMapping::IDENTITY
        && view.level.is_none()
        && !view.twin
        && !view.shader_swizzle
}
fn prepare(
    owner: &OwnedCompute,
    input: &ComputeBindings,
    request: &DispatchRequest,
) -> Result<Box<OwnedDispatch>> {
    if request.threads_per_threadgroup.contains(&0) || request.kind > 1 {
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
                    gpu_address: 0,
                    device_address: None,
                    attribute_stride: None,
                },
            );
        }
    }
    for (index, t) in input.textures.iter().enumerate() {
        if !owner
            .bindings
            .iter()
            .any(|b| b.source_kind == TEXTURE && b.metal_index == index as u32)
        {
            continue;
        }
        if t.bound != 0 {
            if t.pixel_format != 70 && t.pixel_format != 80 {
                return Err(unsupported(
                    "Compute sampled texture needs a format variant",
                ));
            }
            let pixel = metal::PixelFormat::try_from(t.pixel_format)
                .map_err(|_| invalid("Unknown compute texture pixel format"))?;
            let mut facts = TextureFacts::new(metal::TextureType::Type2D, pixel);
            facts.own_format_views = true;
            table.textures.insert(
                index as u32,
                BoundTexture::Texture {
                    facts,
                    resource_id: None,
                },
            );
        }
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
    let plan = owner
        .pipeline
        .plan(&owner.device, &state.shape())
        .map_err(bind_error)?;
    if !plan.variant.is_empty() {
        return Err(unsupported(format!(
            "Compute dispatch needs an unbuilt shader variant: {:?}",
            plan.variant
        )));
    }
    let dispatch = match request.kind {
        0 => Dispatch::Threadgroups {
            groups: request.grid,
            threads_per_group: request.threads_per_threadgroup,
        },
        _ => Dispatch::Threads {
            threads: request.grid,
            threads_per_group: request.threads_per_threadgroup,
        },
    };
    let commands = plan
        .instantiate_dispatch(&state, &dispatch)
        .map_err(bind_error)?;
    if !commands.descriptor_bytes.is_empty()
        || !commands.push_holes.is_empty()
        || commands.root_table.is_some()
        || commands.indirect_clamp.is_some()
    {
        return Err(unsupported("Compute dispatch requires unsupported synthetic tables, addresses or indirect preprocessing"));
    }
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
                if !image_view_ok(view, input.textures[slot.index as usize].pixel_format) {
                    return Err(unsupported(
                        "Compute image view requires an unavailable alias, swizzle or subresource",
                    ));
                }
                descriptor.source_kind = TEXTURE;
                descriptor.metal_index = slot.index;
            }
            WriteSource::ConstantBlob {
                stage: Stage::Kernel,
            } => {
                descriptor.source_kind = CONSTANT;
                descriptor.range = owner
                    .pipeline
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
    let mut entries = Vec::new();
    let mut pushes = Vec::new();
    for region in &commands.regions {
        if region.indirect.is_some() {
            return Err(unsupported("Indirect compute dispatch"));
        }
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
        })
        .collect();
    Ok(Box::new(OwnedDispatch {
        _commands: commands,
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
            private_data: Box::into_raw(dispatch).cast(),
            destroy: Some(destroy_dispatch),
        };
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
