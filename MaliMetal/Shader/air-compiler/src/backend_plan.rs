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
    if meta["variant_bits"] != 0 {
        return Err(refuse("pipeline variants"));
    }
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
        "alpha_to_coverage",
        "alpha_to_one",
        "rasterizer_discard",
        "sample_shading",
        "rasterization_order_attachment_access",
        "vertex_writes_memory",
        "fragment_writes_memory",
        "declares_layer",
        "writes_viewport_index",
        "writes_point_size",
        "reads_barycentric_coord",
        "reads_position_depth",
        "snapshot_reads",
        "early_fragment_tests",
        "writes_depth",
    ] {
        if graphics[name] != false {
            return Err(refuse(name));
        }
    }
    for name in [
        "depth_format",
        "stencil_format",
        "tessellation",
        "fragment_imageblock",
        "shadow_stencil",
        "mesh_records",
        "emulated_tessellation",
        "shader_tests",
        "color_group",
        "sample_mask",
    ] {
        if !graphics[name].is_null() {
            return Err(refuse(name));
        }
    }
    empty(&graphics["input_attachments"], "framebuffer fetch")?;
    empty(&graphics["storage_attachments"], "storage attachments")?;
    if graphics["rasterization_samples"] != 1
        || graphics["max_vertex_amplification_count"] != 1
        || graphics["clip_distances"] != 0
        || graphics["color_outputs"] != 1
    {
        return Err(refuse(
            "multisampling, amplification, clip distances or multiple color outputs",
        ));
    }
    plan.topology = number(&graphics["topology"])?;
    if plan.topology != 3 {
        return Err(refuse("non-triangle topology"));
    }
    let colors = array(&graphics["color_attachments"])?;
    if colors.len() != 1 || !colors[0]["blend"].is_null() || colors[0]["write_mask"] != 15 {
        return Err(refuse(
            "multiple attachments, blending or masked color writes",
        ));
    }
    plan.color_format = number(&colors[0]["format"])?;
    if ![37, 43, 44, 50].contains(&plan.color_format) {
        return Err(refuse("this color format"));
    }

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
            if index >= 64
                || binding["count"] != 1
                || binding["flags"] != 0
                || !binding["immutable_sampler"].is_null()
            {
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
                for flag in ["compare", "integer", "cube_gradient", "raw"] {
                    if sampler[flag] != false {
                        return Err(refuse("this combined sampler use"));
                    }
                }
                let sampler_index = number(&sampler["sampler"]["Slot"])?;
                if sampler_index >= 16 {
                    return Err(refuse("this sampler slot"));
                }
                let texture = array(&interface["slots"])?
                    .iter()
                    .find(|s| &s[0] == source)
                    .ok_or_else(|| refuse("a texture without its resource interface"))?;
                let texture = &texture[1]["Texture"];
                if texture["texture_type"] != "Type2D"
                    || texture["sampled_type"] != "Float"
                    || texture["depth_compare"] != false
                    || texture["access"]["sample"] != true
                    || texture["access"]["read"] != false
                    || texture["access"]["write"] != false
                {
                    return Err(refuse("non-float, non-2D or non-sampled texture access"));
                }
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
            let window = &buffer["window"]["Bounded"];
            if window["start"] != 0 {
                return Err(refuse("unbounded or negative buffer windows"));
            }
            let length = window["len"]
                .as_u64()
                .filter(|&n| n != 0)
                .ok_or_else(|| refuse("an empty buffer window"))?;
            let minimum_size = length
                .checked_add(15)
                .ok_or_else(|| refuse("an overflowing buffer window"))?
                & !15;
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
    // These variant facts are all false in the initial sampled path: only
    // linear-UNORM float textures and ordinary normalized samplers may bind.
    // Their sampler-state specialization is pixel-gated and therefore zero.
    for dependency in array(&meta["dependencies"])? {
        let facts = dependency["fact"]
            .as_object()
            .ok_or_else(|| refuse("a missing runtime fact"))?;
        if facts.len() != 1 {
            return Err(refuse("a composite runtime fact"));
        }
        let (name, value) = facts.iter().next().unwrap();
        let (class, limit, mechanism) = match name.as_str() {
            "SamplerCoordinates" => ("Sampler", 16, "Specialization"),
            "SamplerReduction" => ("Sampler", 16, "PlanTimeCheck"),
            "CubicSampler" | "AnisotropicSampler" => ("Sampler", 16, "Variant"),
            "SrgbTexture" | "CrossClassReads" => ("Texture", 128, "Variant"),
            _ => return Err(refuse(&format!("runtime fact {name}"))),
        };
        let (metal_stage, _, resource_index) = resource_slot(&value["slot"], class, limit)?;
        if dependency["mechanism"] != mechanism
            || !plan.bindings[..plan.binding_count as usize]
                .iter()
                .any(|b| {
                    b.vulkan_type == 1
                        && b.metal_stage == metal_stage
                        && (if class == "Sampler" {
                            b.sampler_index
                        } else {
                            b.metal_index
                        }) == resource_index
                })
        {
            return Err(refuse(
                "a runtime fact without its supported sampled resource",
            ));
        }
    }
    for placement in array(&interface["push"])? {
        let (_, vulkan_stages) = stage(&placement["stage"])?;
        if placement["argument_table_size"] != 0 {
            return Err(refuse("synthetic argument tables"));
        }
        for field in array(&placement["fields"])? {
            let index = plan.push_count as usize;
            if index >= 64 || field["size"] != 4 {
                return Err(refuse("this push-constant size"));
            }
            let offset = number(&field["loc"]["Push"]["offset"])?;
            let mut push = Push {
                vulkan_stages,
                offset,
                size: 4,
                kind: 0,
                metal_index: 0,
                value: 0,
            };
            let value = &field["field"];
            if !value["SlotBindOffset"].is_null() {
                let (_, source_stage, metal_index) = slot(&value["SlotBindOffset"])?;
                if source_stage != vulkan_stages
                    || !plan.bindings[..plan.binding_count as usize]
                        .iter()
                        .any(|b| b.metal_index == metal_index && b.vulkan_stages == vulkan_stages)
                {
                    return Err(refuse("an offset field without its direct buffer"));
                }
                push.kind = 1;
                push.metal_index = metal_index;
            } else if !value["ColorWriteRule"].is_null() {
                let color = number(&value["ColorWriteRule"])?;
                if vulkan_stages != 16 || color != 0 {
                    return Err(refuse("this color-write-rule stage or attachment"));
                }
                let rule = array(&graphics["color_write_rules"])?
                    .iter()
                    .find(|r| r[0] == color)
                    .ok_or_else(|| refuse("a missing color-write rule"))?;
                push.kind = 2;
                push.value = number(&rule[1])?;
            } else {
                return Err(refuse("this runtime push-constant field"));
            }
            plan.push_constant_size = plan.push_constant_size.max(
                offset
                    .checked_add(4)
                    .ok_or_else(|| refuse("push offset overflow"))?,
            );
            plan.pushes[index] = push;
            plan.push_count += 1;
        }
    }
    if plan.push_constant_size > 128 {
        return Err(refuse("more than 128 push-constant bytes"));
    }
    for stage in array(&meta["stages"])? {
        if stage["stage_flags"] != 0 || !stage["required_subgroup_size"].is_null() {
            return Err(refuse("subgroup creation flags"));
        }
        for spec in array(&stage["specialization"]["constants"])? {
            // The generic access form remains correct with residual zero. Its
            // RangeAtBindOffset=true form is only a driver optimization.
            if spec["size"] != 4 || spec["default"] != 0 {
                return Err(refuse("this specialization constant"));
            }
            if !spec["key"]["RangeAtBindOffset"].is_null() {
                slot(&spec["key"]["RangeAtBindOffset"])?;
            } else if !spec["key"]["SamplerState"].is_null() {
                let state = &spec["key"]["SamplerState"];
                let (metal_stage, _, sampler_index) = resource_slot(&state["slot"], "Sampler", 16)?;
                if number(&state["fields"])? & 0x80000000 == 0
                    || !plan.bindings[..plan.binding_count as usize]
                        .iter()
                        .any(|b| {
                            b.vulkan_type == 1
                                && b.metal_stage == metal_stage
                                && b.sampler_index == sampler_index
                        })
                {
                    return Err(refuse("a non-pixel-gated sampler specialization"));
                }
            } else {
                return Err(refuse("this specialization key"));
            }
        }
    }
    Ok(plan)
}
