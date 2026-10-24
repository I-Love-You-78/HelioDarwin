//! C adapter to the separately licensed native AIR compiler. No Apple binary or
//! generated shader is embedded. The matching C definitions are MaliMetalShader.h.
#![allow(non_snake_case)]
mod api_reflection;
mod backend_plan;
mod compute;
mod render;

use metal2vulkan::{
    device::{Device, DeviceDescription, DevicePolicy},
    error::Error,
    library::{FunctionConstantValue, FunctionConstantValues, Library},
    metal,
    pipeline::{
        ColorAttachmentDescriptor, FunctionDescriptor, RenderPipeline, RenderPipelineDescriptor,
        StageSource, VertexAttributeDescriptor, VertexBufferLayoutDescriptor, VertexDescriptor,
    },
    Budget,
};
use std::{
    collections::BTreeMap,
    ffi::{c_char, c_void, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr, slice,
    sync::{Arc, Mutex, OnceLock},
    time::{Duration, Instant},
};

const ABI: u32 = 4;
const INVALID_ARGUMENT: i32 = 1;
const INVALID_LIBRARY: i32 = 2;
const UNSUPPORTED: i32 = 7;
const COMPILE_FAILED: i32 = 8;

#[repr(C)]
pub struct ShaderError {
    status: i32,
    offset: u64,
    message: [c_char; 512],
}
#[repr(C)]
pub struct VertexAttribute {
    index: u32,
    format: u32,
    offset: u32,
    buffer_index: u32,
}
#[repr(C)]
pub struct VertexLayout {
    buffer_index: u32,
    stride: u32,
    step_function: u32,
    step_rate: u32,
}
#[repr(C)]
pub struct ConstantValue {
    index: u32,
    data_type: u32,
    bytes: *const c_void,
    size: usize,
}
#[repr(C)]
pub struct CompilerRequest {
    vertex_bytes: *const c_void,
    vertex_size: usize,
    vertex_name: *const c_char,
    fragment_bytes: *const c_void,
    fragment_size: usize,
    fragment_name: *const c_char,
    device_profile_json: *const c_char,
    device_profile_size: usize,
    attributes: *const VertexAttribute,
    attribute_count: usize,
    layouts: *const VertexLayout,
    layout_count: usize,
    vertex_constants: *const ConstantValue,
    vertex_constant_count: usize,
    fragment_constants: *const ConstantValue,
    fragment_constant_count: usize,
    color_formats: [u32; 8],
    depth_format: u32,
    stencil_format: u32,
    sample_count: u32,
    timeout_ms: u32,
    color_states: [backend_plan::Color; 8],
}
#[repr(C)]
pub struct StageModule {
    vulkan_stage: u32,
    words: *const u32,
    word_count: usize,
    entry_point: *const c_char,
}
#[repr(C)]
pub struct RenderOutput {
    abi_version: u32,
    module_count: u32,
    modules: *const StageModule,
    metadata_json: *const c_char,
    metadata_size: usize,
    private_data: *mut c_void,
    destroy: Option<unsafe extern "C" fn(*mut RenderOutput)>,
    backend_plan: *const backend_plan::BackendPlan,
    backend_plan_error: *const c_char,
    release_code: Option<unsafe extern "C" fn(*mut RenderOutput)>,
    prepare_draw: Option<
        unsafe extern "C" fn(
            *const RenderOutput,
            *const render::Bindings,
            *const render::DrawRequest,
            *mut render::DrawPlan,
            *mut ShaderError,
        ) -> i32,
    >,
}
#[repr(C)]
pub struct Compiler {
    abi_version: u32,
    name: *const c_char,
    compile_render:
        unsafe extern "C" fn(*const CompilerRequest, *mut RenderOutput, *mut ShaderError) -> i32,
    reflect: unsafe extern "C" fn(
        *const c_void,
        usize,
        *const c_char,
        *mut *mut c_char,
        *mut usize,
        *mut ShaderError,
    ) -> i32,
    free_string: unsafe extern "C" fn(*mut c_char),
    compile_compute: unsafe extern "C" fn(
        *const compute::ComputeRequest,
        *mut compute::ComputeOutput,
        *mut ShaderError,
    ) -> i32,
    capabilities: u64,
}
// All fields point to immutable static data or functions.
unsafe impl Sync for Compiler {}

#[derive(Debug)]
struct Failure(i32, String);
impl From<Error> for Failure {
    fn from(e: Error) -> Self {
        let code = match &e {
            Error::InvalidInput(_) => INVALID_LIBRARY,
            Error::Unsupported(_) | Error::NeedsInput(_) => UNSUPPORTED,
            Error::InvalidDescriptor(_) => INVALID_ARGUMENT,
            _ => COMPILE_FAILED,
        };
        Failure(code, e.to_string())
    }
}
type Result<T> = std::result::Result<T, Failure>;
fn invalid(s: impl Into<String>) -> Failure {
    Failure(INVALID_ARGUMENT, s.into())
}
unsafe fn bytes<'a>(p: *const c_void, n: usize, max: usize) -> Result<&'a [u8]> {
    if n > max || (n != 0 && p.is_null()) {
        return Err(invalid("Invalid byte span"));
    }
    Ok(if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p.cast(), n)
    })
}
unsafe fn list<'a, T>(p: *const T, n: usize, max: usize) -> Result<&'a [T]> {
    if n > max || (n != 0 && p.is_null()) {
        return Err(invalid("Invalid descriptor array"));
    }
    Ok(if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    })
}
unsafe fn name<'a>(p: *const c_char) -> Result<&'a str> {
    if p.is_null() {
        return Err(invalid("Missing entry point name"));
    }
    CStr::from_ptr(p)
        .to_str()
        .map_err(|_| invalid("Entry point name is not UTF-8"))
}
fn cstring(s: String) -> Result<CString> {
    CString::new(s).map_err(|_| invalid("Unexpected NUL in compiler output"))
}
unsafe fn constants(p: *const ConstantValue, n: usize) -> Result<FunctionConstantValues> {
    let mut out = FunctionConstantValues::defaults();
    for c in list(p, n, 65536)? {
        let ty = metal::DataType::try_from(c.data_type)
            .map_err(|_| invalid("Unknown MTLDataType in function constant"))?;
        let value = FunctionConstantValue::new(ty, bytes(c.bytes, c.size, 128)?.to_vec());
        if out.by_index.insert(c.index, value).is_some() {
            return Err(invalid("Duplicate function constant index"));
        }
    }
    Ok(out)
}
struct OwnedOutput {
    device: Device,
    descriptor: RenderPipelineDescriptor,
    _pipeline: RenderPipeline,
    variants: Mutex<BTreeMap<metal2vulkan::cache::VariantKey, Arc<RenderPipeline>>>,
    _names: Vec<CString>,
    modules: Vec<StageModule>,
    metadata: CString,
    backend_plan: Option<Box<backend_plan::BackendPlan>>,
    backend_plan_error: Option<CString>,
}

fn parse_library(input: &[u8]) -> Result<Library> {
    static LIBRARIES: OnceLock<Mutex<Vec<Library>>> = OnceLock::new();
    let mut libraries = LIBRARIES
        .get_or_init(|| Mutex::new(Vec::new()))
        .lock()
        .map_err(|_| invalid("Library cache lock is poisoned"))?;
    if let Some(library) = libraries.iter().find(|library| library.bytes() == input) {
        return Ok(library.clone());
    }
    let library = Library::parse(input)?;
    if libraries.len() == 8 {
        libraries.remove(0);
    }
    libraries.push(library.clone());
    Ok(library)
}

unsafe fn compile(r: &CompilerRequest) -> Result<Box<OwnedOutput>> {
    let profile = bytes(
        r.device_profile_json.cast(),
        r.device_profile_size,
        4 * 1024 * 1024,
    )?;
    let profile =
        std::str::from_utf8(profile).map_err(|_| invalid("Device profile is not UTF-8"))?;
    let description = DeviceDescription::from_khronos_json(profile, None)?;
    // UNTRUSTED includes resource containment. No device features are fabricated.
    let device = Device::new(description, DevicePolicy::UNTRUSTED)?;
    let vertex = parse_library(bytes(r.vertex_bytes, r.vertex_size, 64 * 1024 * 1024)?)?;
    let mut vf = FunctionDescriptor::new(vertex.function(name(r.vertex_name)?)?);
    vf.constants = constants(r.vertex_constants, r.vertex_constant_count)?;
    let mut descriptor = RenderPipelineDescriptor::new(StageSource::Function(vf));
    if !r.fragment_name.is_null() {
        let fragment = parse_library(bytes(r.fragment_bytes, r.fragment_size, 64 * 1024 * 1024)?)?;
        let mut ff = FunctionDescriptor::new(fragment.function(name(r.fragment_name)?)?);
        ff.constants = constants(r.fragment_constants, r.fragment_constant_count)?;
        descriptor.fragment = Some(StageSource::Function(ff));
    }
    let attributes = list(r.attributes, r.attribute_count, 31)?;
    let layouts = list(r.layouts, r.layout_count, 31)?;
    if !attributes.is_empty() || !layouts.is_empty() {
        let mut vertex = VertexDescriptor::new();
        for a in attributes {
            if a.index >= 31 || a.buffer_index >= 31 {
                return Err(invalid("Vertex attribute or buffer index exceeds 30"));
            }
            let format = metal::VertexFormat::try_from(a.format)
                .map_err(|_| invalid("Unknown MTLVertexFormat"))?;
            if vertex
                .attributes
                .insert(
                    a.index,
                    VertexAttributeDescriptor::new(format, a.offset, a.buffer_index),
                )
                .is_some()
            {
                return Err(invalid("Duplicate vertex attribute"));
            }
        }
        for l in layouts {
            if l.buffer_index >= 31 {
                return Err(invalid("Vertex layout index exceeds 30"));
            }
            let mut layout = VertexBufferLayoutDescriptor::new(Some(l.stride));
            layout.step_function = metal::VertexStepFunction::try_from(l.step_function)
                .map_err(|_| invalid("Unknown MTLVertexStepFunction"))?;
            layout.step_rate = l.step_rate;
            if vertex.layouts.insert(l.buffer_index, layout).is_some() {
                return Err(invalid("Duplicate vertex layout"));
            }
        }
        descriptor.vertex_descriptor = Some(vertex);
    }
    for (index, &raw) in r.color_formats.iter().enumerate() {
        if raw != 0 {
            let format = metal::PixelFormat::try_from(raw)
                .map_err(|_| invalid("Unknown color MTLPixelFormat"))?;
            let state = &r.color_states[index];
            let mut color = ColorAttachmentDescriptor::new(format);
            color.blending_enabled = state.enabled != 0;
            color.source_rgb_blend_factor = metal::BlendFactor::try_from(state.src_rgb)
                .map_err(|_| invalid("Unknown source RGB blend factor"))?;
            color.destination_rgb_blend_factor = metal::BlendFactor::try_from(state.dst_rgb)
                .map_err(|_| invalid("Unknown destination RGB blend factor"))?;
            color.rgb_blend_operation = metal::BlendOperation::try_from(state.rgb_op)
                .map_err(|_| invalid("Unknown RGB blend operation"))?;
            color.source_alpha_blend_factor = metal::BlendFactor::try_from(state.src_alpha)
                .map_err(|_| invalid("Unknown source alpha blend factor"))?;
            color.destination_alpha_blend_factor = metal::BlendFactor::try_from(state.dst_alpha)
                .map_err(|_| invalid("Unknown destination alpha blend factor"))?;
            color.alpha_blend_operation = metal::BlendOperation::try_from(state.alpha_op)
                .map_err(|_| invalid("Unknown alpha blend operation"))?;
            color.write_mask = metal::ColorWriteMask::try_from(state.write_mask)
                .map_err(|_| invalid("Unknown color write mask"))?;
            descriptor.color_attachments[index] = Some(color);
        }
    }
    if r.depth_format != 0 {
        descriptor.depth_format = Some(
            metal::PixelFormat::try_from(r.depth_format)
                .map_err(|_| invalid("Unknown depth MTLPixelFormat"))?,
        );
    }
    if r.stencil_format != 0 {
        descriptor.stencil_format = Some(
            metal::PixelFormat::try_from(r.stencil_format)
                .map_err(|_| invalid("Unknown stencil MTLPixelFormat"))?,
        );
    }
    descriptor.raster_sample_count = if r.sample_count == 0 {
        1
    } else {
        r.sample_count
    };
    let timeout = if r.timeout_ms == 0 {
        20_000
    } else {
        r.timeout_ms.min(20_000)
    };
    let budget =
        Budget::with_deadline(Some(Instant::now() + Duration::from_millis(timeout as u64)));
    let pipeline = device.compile_render_pipeline(&descriptor, &budget)?;
    let mut stages = Vec::new();
    for stage in &pipeline.stages {
        stages.push(serde_json::json!({
            "stage": stage.stage, "entry_point": stage.entry_point,
            "specialization": stage.specialization, "required_subgroup_size": stage.required_subgroup_size,
            "stage_flags": stage.stage_flags, "reconvergence": stage.reconvergence,
            "precision_notes": stage.precision_notes, "constant_blob": stage.constant_blob.as_deref(),
            "specialized_notes": stage.specialized_notes, "private_bytes": stage.private_bytes,
            "rounded_colors": stage.rounded_colors, "half_bit_colors": stage.half_bit_colors,
            "own_pixel_textures": stage.own_pixel_textures, "snapshot_textures": stage.snapshot_textures,
        }));
    }
    let req = &pipeline.requirements;
    let spirv_extensions: Vec<_> = req
        .spirv_extensions
        .iter()
        .map(|x| format!("{x:?}"))
        .collect();
    let properties: Vec<_> = req.properties.iter().map(|x| format!("{x:?}")).collect();
    let limits: std::collections::BTreeMap<_, _> = req
        .limits
        .iter()
        .map(|(k, v)| (format!("{k:?}"), *v))
        .collect();
    let formats: Vec<_> = req
        .formats
        .iter()
        .map(|(f, u)| (f.as_raw(), format!("{u:?}")))
        .collect();
    let stage_flags: Vec<_> = req.stage_flags.iter().map(|x| format!("{x:?}")).collect();
    let requirements = serde_json::json!({
        "api_version": req.api_version, "spirv_capabilities": req.spirv_capabilities,
        "spirv_extensions": spirv_extensions, "features": req.features,
        "extensions": req.extensions, "properties": properties, "limits": limits,
        "formats": formats, "stage_flags": stage_flags,
        "denorm_independence": req.denorm_independence, "rounding_independence": req.rounding_independence,
    });
    let spirv = pipeline
        .stages
        .iter()
        .filter_map(|s| s.spirv.get(1))
        .copied()
        .max()
        .unwrap_or(0);
    let metadata = serde_json::json!({
        "abi_version": ABI, "compiler_revision": "a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b",
        "compiler_effective_tree": option_env!("MALIMETAL_COMPILER_EFFECTIVE_TREE").unwrap_or(""),
        "compiler_source_digest": metal2vulkan::cache::SOURCE_DIGEST,
        "kind": "render", "profile": pipeline.profile, "spirv_version": format!("{}.{}",(spirv>>16)&255,(spirv>>8)&255),
        "layout": pipeline.layout, "interface": pipeline.interface,
        "vertex_input": pipeline.vertex_input, "graphics": pipeline.graphics,
        "requirements": requirements, "dependencies": pipeline.dependencies,
        "variant_bits": pipeline.variant_bits, "stages": stages,
    });
    let limits = device.profile().limits();
    let constant_blobs: Vec<_> = pipeline
        .stages
        .iter()
        .filter_map(|stage| {
            stage
                .constant_blob
                .as_deref()
                .map(|bytes| (stage.stage.as_raw(), bytes))
        })
        .collect();
    let (backend_plan, backend_plan_error) = match backend_plan::build(
        &metadata,
        limits.min_uniform_buffer_offset_alignment,
        limits.min_storage_buffer_offset_alignment,
        &constant_blobs,
    ) {
        Ok(plan) => (Some(plan), None),
        Err(reason) => (None, Some(cstring(reason)?)),
    };
    let metadata = cstring(serde_json::to_string(&metadata).map_err(|e| invalid(e.to_string()))?)?;
    let names = pipeline
        .stages
        .iter()
        .map(|s| cstring(s.entry_point.clone()))
        .collect::<Result<Vec<_>>>()?;
    let modules = pipeline
        .stages
        .iter()
        .zip(&names)
        .map(|(stage, name)| StageModule {
            vulkan_stage: stage.stage.as_raw(),
            words: stage.spirv.as_ptr(),
            word_count: stage.spirv.len(),
            entry_point: name.as_ptr(),
        })
        .collect();
    Ok(Box::new(OwnedOutput {
        device,
        descriptor,
        _pipeline: pipeline,
        variants: Mutex::new(BTreeMap::new()),
        _names: names,
        modules,
        metadata,
        backend_plan,
        backend_plan_error,
    }))
}

unsafe fn report(error: *mut ShaderError, result: std::thread::Result<Result<()>>) -> i32 {
    let failure = match result {
        Ok(Ok(())) => return 0,
        Ok(Err(f)) => f,
        Err(_) => Failure(
            COMPILE_FAILED,
            "AIR compiler panicked; no shader artifact was returned".into(),
        ),
    };
    if let Some(e) = error.as_mut() {
        e.status = failure.0;
        e.offset = 0;
        e.message.fill(0);
        let count = failure.1.len().min(e.message.len() - 1);
        for (dst, src) in e.message[..count].iter_mut().zip(failure.1.as_bytes()) {
            *dst = *src as c_char;
        }
    }
    failure.0
}
unsafe extern "C" fn compile_render(
    request: *const CompilerRequest,
    output: *mut RenderOutput,
    error: *mut ShaderError,
) -> i32 {
    if !output.is_null() {
        ptr::write_bytes(output, 0, 1);
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        if request.is_null() || output.is_null() {
            return Err(invalid("Missing compiler request or result"));
        }
        let owner = compile(&*request)?;
        *output = RenderOutput {
            abi_version: ABI,
            module_count: owner.modules.len() as u32,
            modules: owner.modules.as_ptr(),
            metadata_json: owner.metadata.as_ptr(),
            metadata_size: owner.metadata.as_bytes().len(),
            backend_plan: owner.backend_plan.as_deref().map_or(ptr::null(), |p| p),
            backend_plan_error: owner
                .backend_plan_error
                .as_ref()
                .map_or(ptr::null(), |s| s.as_ptr()),
            private_data: Box::into_raw(owner).cast(),
            destroy: Some(destroy),
            release_code: Some(release_code),
            prepare_draw: Some(render::prepare_draw),
        };
        Ok(())
    }));
    report(error, result)
}
unsafe extern "C" fn destroy(output: *mut RenderOutput) {
    if let Some(output) = output.as_mut() {
        if !output.private_data.is_null() {
            drop(Box::from_raw(output.private_data.cast::<OwnedOutput>()));
        }
        ptr::write_bytes(output, 0, 1);
    }
}
unsafe extern "C" fn release_code(output: *mut RenderOutput) {
    // A render output owns the modules used by its per-draw variants. The
    // pipeline and every recorded draw retain that output until completion.
    let _ = output;
}
unsafe extern "C" fn reflect(
    input: *const c_void,
    size: usize,
    entry: *const c_char,
    output: *mut *mut c_char,
    output_size: *mut usize,
    error: *mut ShaderError,
) -> i32 {
    if !output.is_null() {
        *output = ptr::null_mut();
    }
    if !output_size.is_null() {
        *output_size = 0;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        if output.is_null() || output_size.is_null() {
            return Err(invalid("Missing reflection result"));
        }
        let library = parse_library(bytes(input, size, 64 * 1024 * 1024)?)?;
        let function = library.function(name(entry)?)?;
        let reflection = function.reflection(&Budget::standard())?;
        let value =
            cstring(serde_json::to_string(&reflection).map_err(|e| invalid(e.to_string()))?)?;
        *output_size = value.as_bytes().len();
        *output = value.into_raw();
        Ok(())
    }));
    report(error, result)
}
unsafe extern "C" fn free_string(value: *mut c_char) {
    if !value.is_null() {
        drop(CString::from_raw(value));
    }
}

static COMPILER: Compiler = Compiler {
    abi_version: ABI,
    name: b"metal2vulkan AIR -> SPIR-V / Vulkan\0".as_ptr().cast(),
    compile_render,
    reflect,
    free_string,
    compile_compute: compute::compile_compute,
    capabilities: render::CAPABILITIES,
};
#[no_mangle]
pub extern "C" fn MMAIRCompilerV4() -> *const Compiler {
    &COMPILER
}
