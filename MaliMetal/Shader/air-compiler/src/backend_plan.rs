//! Convert compiler facts into the bounded C execution plan. A compiled shader
//! can remain useful offline even when this first runtime cannot execute it.
use serde_json::Value;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Binding {
    pub set: u32,
    pub binding: u32,
    pub vulkan_type: u32,
    pub vulkan_stages: u32,
    pub metal_stage: u32,
    pub metal_index: u32,
    pub minimum_size: u64,
    pub alignment: u64,
    pub sampler_index: u32,
    pub constant_bytes: *const u8,
    pub constant_size: usize,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Push {
    pub vulkan_stages: u32,
    pub offset: u32,
    pub size: u32,
    pub kind: u32,
    pub metal_index: u32,
    pub value: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VertexBinding {
    pub binding: u32,
    pub metal_index: u32,
    pub stride: u32,
    pub per_instance: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VertexAttribute {
    pub location: u32,
    pub binding: u32,
    pub format: u32,
    pub offset: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Color {
    pub format: u32,
    pub enabled: u32,
    pub src_rgb: u32,
    pub dst_rgb: u32,
    pub rgb_op: u32,
    pub src_alpha: u32,
    pub dst_alpha: u32,
    pub alpha_op: u32,
    pub write_mask: u32,
}
#[repr(C)]
pub struct BackendPlan {
    pub binding_count: u32,
    pub push_count: u32,
    pub vertex_binding_count: u32,
    pub vertex_attribute_count: u32,
    pub push_constant_size: u32,
    pub color_format: u32,
    pub topology: u32,
    pub bindings: [Binding; 64],
    pub pushes: [Push; 64],
    pub vertex_bindings: [VertexBinding; 31],
    pub vertex_attributes: [VertexAttribute; 31],
    pub color_attachment_count: u32,
    pub colors: [Color; 8],
    pub color_outputs: u32,
    pub raster_samples: u32,
    pub depth_format: u32,
    pub stencil_format: u32,
    pub alpha_to_coverage: u32,
    pub alpha_to_one: u32,
    pub rasterizer_discard: u32,
    pub sample_shading: u32,
    pub tile_image_color_reads: u32,
    pub has_sample_mask: u32,
    pub sample_mask: u32,
}
type Result<T> = std::result::Result<T, String>;
fn refuse(name: &str) -> String {
    format!("Execution plan does not support {name}")
}
fn number(v: &Value) -> Result<u32> {
    v.as_u64()
        .and_then(|n| u32::try_from(n).ok())
        .ok_or_else(|| refuse("a non-u32 compiler field"))
}
fn array(v: &Value) -> Result<&Vec<Value>> {
    v.as_array()
        .ok_or_else(|| refuse("a missing compiler array"))
}
fn empty(v: &Value, name: &str) -> Result<()> {
    if array(v)?.is_empty() {
        Ok(())
    } else {
        Err(refuse(name))
    }
}
fn stage(v: &Value) -> Result<(u32, u32)> {
    match v.as_str() {
        Some("Vertex") => Ok((0, 1)),
        Some("Fragment") => Ok((1, 16)),
        _ => Err(refuse("this shader stage")),
    }
}
fn slot(v: &Value) -> Result<(u32, u32, u32)> {
    resource_slot(v, "Buffer", 31)
}
fn resource_slot(v: &Value, class: &str, limit: u32) -> Result<(u32, u32, u32)> {
    let (metal, vulkan) = stage(&v["stage"])?;
    let index = number(&v["index"])?;
    if v["class"] != class || index >= limit {
        return Err(refuse("an unsupported or out-of-range Metal resource slot"));
    }
    Ok((metal, vulkan, index))
}

pub fn build(
    meta: &Value,
    uniform_alignment: u64,
    storage_alignment: u64,
    constant_blobs: &[(u32, &[u8])],
) -> Result<Box<BackendPlan>> {
    // All plan fields are plain integers and fixed arrays of integer structs.
    let mut plan: Box<BackendPlan> = Box::new(unsafe { std::mem::zeroed() });
    let interface = &meta["interface"];
    for name in [
        "needs_pointer_translation",
        "needs_handle_translation",
        "reaches_command_buffers",
    ] {
        if interface[name] != false {
            return Err(refuse(name));
        }
    }
    empty(&interface["threadgroup"], "threadgroup resources")?;
    empty(&interface["texture_writing_stages"], "texture writes")?;
    for samplers in array(&interface["numbered_samplers"])? {
        // These record Metal's observable gpuResourceID allocation order.
        // They are not descriptor bindings; all executable sampler resources
        // are separately checked against the linked layout below.
        for sampler in array(&samplers[1])? {
            if sampler == "Read" || sampler == "Write" {
                continue;
            }
            let words = array(&sampler["Constexpr"])?;
            if words.len() != 2 || words.iter().any(|word| word.as_u64().is_none()) {
                return Err(refuse("this numbered sampler record"));
            }
        }
    }
    let graphics = &meta["graphics"];
    for name in [
        "rasterization_order_attachment_access",
        "vertex_writes_memory",
        "fragment_writes_memory",
        "declares_layer",
        "writes_viewport_index",
        "writes_point_size",
        "reads_barycentric_coord",
        "reads_position_depth",
        "snapshot_reads",
    ] {
        if graphics[name] != false {
            return Err(refuse(name));
        }
    }
    for name in [
        "tessellation",
        "fragment_imageblock",
        "shadow_stencil",
        "mesh_records",
        "emulated_tessellation",
        "shader_tests",
        "color_group",
    ] {
        if !graphics[name].is_null() {
            return Err(refuse(name));
        }
    }
    empty(&graphics["input_attachments"], "framebuffer fetch")?;
    empty(&graphics["storage_attachments"], "storage attachments")?;
    if graphics["max_vertex_amplification_count"] != 1 || graphics["clip_distances"] != 0 {
        return Err(refuse("amplification or clip distances"));
    }
    plan.topology = number(&graphics["topology"])?;
    plan.color_outputs = number(&graphics["color_outputs"])?;
    plan.raster_samples = number(&graphics["rasterization_samples"])?;
    plan.depth_format = if graphics["depth_format"].is_null() {
        0
    } else {
        number(&graphics["depth_format"])?
    };
    plan.stencil_format = if graphics["stencil_format"].is_null() {
        0
    } else {
        number(&graphics["stencil_format"])?
    };
    plan.alpha_to_coverage = u32::from(graphics["alpha_to_coverage"] == true);
    plan.alpha_to_one = u32::from(graphics["alpha_to_one"] == true);
    plan.rasterizer_discard = u32::from(graphics["rasterizer_discard"] == true);
    plan.sample_shading = u32::from(graphics["sample_shading"] == true);
    if !graphics["sample_mask"].is_null() {
        plan.has_sample_mask = 1;
        plan.sample_mask = number(&graphics["sample_mask"])?;
    }
    if let Some(reads) = graphics.get("tile_image_color_reads") {
        for color in array(reads)? {
            let color = number(color)?;
            if color >= 8 {
                return Err(refuse("tile-image color index"));
            }
            plan.tile_image_color_reads |= 1 << color;
        }
    }
    let colors = array(&graphics["color_attachments"])?;
    if colors.is_empty() || colors.len() > 8 || number(&graphics["color_outputs"])? & !0xff != 0 {
        return Err(refuse("this color attachment count"));
    }
    plan.color_attachment_count = colors.len() as u32;
    for (index, value) in colors.iter().enumerate() {
        if value.is_null() {
            continue;
        }
        let color = &mut plan.colors[index];
        color.format = number(&value["format"])?;
        color.write_mask = number(&value["write_mask"])?;
        if color.format == 0 || color.write_mask & !15 != 0 {
            return Err(refuse("this color format or write mask"));
        }
        let blend = &value["blend"];
        if !blend.is_null() {
            color.enabled = 1;
            color.src_rgb = number(&blend["src_color"])?;
            color.dst_rgb = number(&blend["dst_color"])?;
            color.rgb_op = number(&blend["color_op"])?;
            color.src_alpha = number(&blend["src_alpha"])?;
            color.dst_alpha = number(&blend["dst_alpha"])?;
            color.alpha_op = number(&blend["alpha_op"])?;
        }
    }
    plan.color_format = plan.colors[0].format;

    let vertex = &meta["vertex_input"];
    empty(&vertex["pulled"], "shader-pulled vertex attributes")?;
    empty(&vertex["variant_pulled"], "pulled vertex variants")?;
    for binding in array(&vertex["bindings"])? {
        let index = plan.vertex_binding_count as usize;
        if index >= 31 || binding["divisor"] != 1 {
            return Err(refuse("vertex divisors or too many bindings"));
        }
        let input_rate = number(&binding["input_rate"])?;
        if input_rate > 1 {
            return Err(refuse("this vertex input rate"));
        }
        let metal_index = number(&binding["buffer_index"])?;
        if metal_index >= 31 {
            return Err(refuse("this vertex buffer index"));
        }
        plan.vertex_bindings[index] = VertexBinding {
            binding: number(&binding["binding"])?,
            metal_index,
            stride: number(&binding["stride"]["Static"])?,
            per_instance: input_rate,
        };
        plan.vertex_binding_count += 1;
    }
    for attribute in array(&vertex["attributes"])? {
        let index = plan.vertex_attribute_count as usize;
        if index >= 31 {
            return Err(refuse("too many vertex attributes"));
        }
        plan.vertex_attributes[index] = VertexAttribute {
            location: number(&attribute["location"])?,
            binding: number(&attribute["binding"])?,
            format: number(&attribute["format"])?,
            offset: number(&attribute["offset"])?,
        };
        plan.vertex_attribute_count += 1;
    }
    for set in array(&meta["layout"]["sets"])? {
        if set["flags"] != 0 {
            return Err(refuse("descriptor set flags"));
        }
        for binding in array(&set["bindings"])? {
            let index = plan.binding_count as usize;
            if index >= 64 || binding["count"] != 1 || binding["flags"] != 0 {
                return Err(refuse(
                    "descriptor arrays, flags, immutable samplers or too many bindings",
                ));
            }
            let vulkan_type = number(&binding["ty"])?;
            if !binding["source"]["Synthetic"].is_null() {
                let source = &binding["source"]["Synthetic"];
                let (metal_stage, vulkan_stage) = stage(&source["stage"])?;
                if source["binding"] != "ConstantBlob" || vulkan_type != 7 {
                    return Err(refuse("this synthetic shader resource"));
                }
                let bytes = constant_blobs
                    .iter()
                    .find(|(s, _)| *s == vulkan_stage)
                    .map(|(_, b)| *b)
                    .filter(|b| !b.is_empty())
                    .ok_or_else(|| refuse("a missing compiler constant blob"))?;
                plan.bindings[index] = Binding {
                    set: number(&set["set"])?,
                    binding: number(&binding["binding"])?,
                    vulkan_type,
                    vulkan_stages: number(&binding["stages"])?,
                    metal_stage,
                    metal_index: u32::MAX,
                    minimum_size: (bytes.len() as u64 + 15) & !15,
                    alignment: storage_alignment.max(16),
                    sampler_index: u32::MAX,
                    constant_bytes: bytes.as_ptr(),
                    constant_size: bytes.len(),
                };
                plan.binding_count += 1;
                continue;
            }
            if vulkan_type == 1 {
                let combined = &binding["source"]["CombinedTextureSampler"];
                let source = &combined["texture"];
                let (metal_stage, _, metal_index) = resource_slot(source, "Texture", 128)?;
                let sampler = &combined["sampler"];
                let sampler_index = if binding["immutable_sampler"].is_null() {
                    number(&sampler["sampler"]["Slot"])?
                } else {
                    u32::MAX
                };
                if sampler_index != u32::MAX && sampler_index >= 16 {
                    return Err(refuse("this sampler slot"));
                }
                let texture = array(&interface["slots"])?
                    .iter()
                    .find(|s| &s[0] == source)
                    .ok_or_else(|| refuse("a texture without its resource interface"))?;
                let _texture = &texture[1]["Texture"];
                plan.bindings[index] = Binding {
                    set: number(&set["set"])?,
                    binding: number(&binding["binding"])?,
                    vulkan_type,
                    vulkan_stages: number(&binding["stages"])?,
                    metal_stage,
                    metal_index,
                    minimum_size: 0,
                    alignment: 0,
                    sampler_index,
                    constant_bytes: std::ptr::null(),
                    constant_size: 0,
                };
                plan.binding_count += 1;
                continue;
            }
            if vulkan_type == 2 || vulkan_type == 3 {
                let source = &binding["source"]["Texture"];
                let (metal_stage, _, metal_index) = resource_slot(source, "Texture", 128)?;
                plan.bindings[index] = Binding {
                    set: number(&set["set"])?,
                    binding: number(&binding["binding"])?,
                    vulkan_type,
                    vulkan_stages: number(&binding["stages"])?,
                    metal_stage,
                    metal_index,
                    minimum_size: 0,
                    alignment: 0,
                    sampler_index: u32::MAX,
                    constant_bytes: std::ptr::null(),
                    constant_size: 0,
                };
                plan.binding_count += 1;
                continue;
            }
            if vulkan_type == 0 {
                plan.bindings[index] = Binding {
                    set: number(&set["set"])?,
                    binding: number(&binding["binding"])?,
                    vulkan_type,
                    vulkan_stages: number(&binding["stages"])?,
                    metal_stage: 0,
                    metal_index: u32::MAX,
                    minimum_size: 0,
                    alignment: 0,
                    sampler_index: u32::MAX,
                    constant_bytes: std::ptr::null(),
                    constant_size: 0,
                };
                plan.binding_count += 1;
                continue;
            }
            if vulkan_type != 6 && vulkan_type != 7 {
                return Err(refuse("this image/sampler descriptor form"));
            }
            let source = &binding["source"]["Buffer"];
            let (metal_stage, _, metal_index) = slot(source)?;
            let slot = array(&interface["slots"])?
                .iter()
                .find(|s| &s[0] == source)
                .ok_or_else(|| refuse("a descriptor without its resource interface"))?;
            let buffer = &slot[1]["Buffer"];
            if buffer["kind"] != "Buffer" || buffer["form"]["Descriptor"].is_null() {
                return Err(refuse("argument buffers or address-based resources"));
            }
            // The canonical per-draw planner instantiates signed/whole windows
            // against the actual logical allocation and descriptor limits.
            let minimum_size = 0;
            plan.bindings[index] = Binding {
                set: number(&set["set"])?,
                binding: number(&binding["binding"])?,
                vulkan_type,
                vulkan_stages: number(&binding["stages"])?,
                metal_stage,
                metal_index,
                minimum_size,
                alignment: if vulkan_type == 6 {
                    uniform_alignment.max(16)
                } else {
                    storage_alignment.max(16)
                },
                sampler_index: u32::MAX,
                constant_bytes: std::ptr::null(),
                constant_size: 0,
            };
            plan.binding_count += 1;
        }
    }
    // Resource dependencies, specialization and push fields are resolved from
    // the actual BindState by RenderPipeline::plan and instantiate_draw.
    for range in array(&meta["layout"]["push_constants"])? {
        let end = number(&range["offset"])?
            .checked_add(number(&range["size"])?)
            .ok_or_else(|| refuse("push range overflow"))?;
        plan.push_constant_size = plan.push_constant_size.max(end);
    }
    Ok(plan)
}
