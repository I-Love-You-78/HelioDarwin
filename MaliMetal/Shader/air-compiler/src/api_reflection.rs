//! Numeric Metal reflection bridge. Uses the pinned compiler's typed AIR APIs;
//! no binding layout is reconstructed from shader names or SPIR-V descriptors.
use super::*;
use metal2vulkan::{bind::{ArgumentBufferLayout, ArgumentType}, reflection::*};
use serde_json::{json, Value};

fn attributes(a: Option<&[Attribute]>) -> Value {
    match a {
        None => Value::Null,
        Some(a) => Value::Array(a.iter().map(|a| json!({
            "name":a.name(), "attributeIndex":a.attribute_index(),
            "attributeType":a.attribute_type() as u32, "active":a.is_active(),
            "patchData":a.is_patch_data(), "patchControlPointData":a.is_patch_control_point_data()
        })).collect())
    }
}
fn structure(s: Option<&StructType>) -> Value {
    s.map(|s| json!({"dataType":1,"members":s.members().iter().map(|m| json!({
        "name":m.name(), "offset":m.offset(), "dataType":m.data_type() as u32,
        "argumentIndex":m.argument_index(), "structType":structure(m.struct_type()),
        "arrayType":array(m.array_type()), "pointerType":pointer(m.pointer_type()),
        "textureReferenceType":texture_reference(m.texture_reference_type())
    })).collect::<Vec<_>>() })).unwrap_or(Value::Null)
}
fn array(a: Option<&ArrayType>) -> Value {
    a.map(|a| json!({"dataType":2,"elementType":a.element_type() as u32,
        "arrayLength":a.array_length(), "stride":a.stride(),
        "argumentIndexStride":a.argument_index_stride(),
        "elementStructType":structure(a.element_struct_type()),
        "elementArrayType":array(a.element_array_type()),
        "elementPointerType":pointer(a.element_pointer_type()),
        "elementTextureReferenceType":texture_reference(a.element_texture_reference_type())
    })).unwrap_or(Value::Null)
}
fn pointer(p: Option<&PointerType>) -> Value {
    p.map(|p| json!({"dataType":60,"elementType":p.element_type() as u32,
        "access":p.access() as u32,"alignment":p.alignment(),"dataSize":p.data_size(),
        "elementIsArgumentBuffer":p.element_is_argument_buffer(),
        "elementStructType":structure(p.element_struct_type()),
        "elementArrayType":array(p.element_array_type())
    })).unwrap_or(Value::Null)
}
fn texture_reference(t: Option<&TextureReferenceType>) -> Value {
    t.map(|t| json!({"dataType":58,"textureDataType":t.texture_data_type() as u32,
        "textureType":t.texture_type() as u32,"access":t.access() as u32,
        "depthTexture":t.is_depth_texture()
    })).unwrap_or(Value::Null)
}
fn binding(b: &Binding) -> Value {
    let mut v = json!({"name":b.name(), "type":b.binding_type() as u32,
        "access":b.access() as u32, "index":b.index(), "active":b.is_used(),
        "argument":b.is_argument(), "arrayLength":1});
    if let Some(t)=b.buffer() {
        v["bufferAlignment"]=json!(t.buffer_alignment());
        v["bufferDataSize"]=json!(t.buffer_data_size());
        v["bufferDataType"]=json!(t.buffer_data_type() as u32);
        v["bufferStructType"]=structure(t.buffer_struct_type());
        v["bufferPointerType"]=pointer(t.buffer_pointer_type());
    }
    if let Some(t)=b.texture() {
        v["textureType"]=json!(t.texture_type() as u32);
        v["textureDataType"]=json!(t.texture_data_type() as u32);
        v["depthTexture"]=json!(t.is_depth_texture());
        v["arrayLength"]=json!(t.array_length());
    }
    if let Some(t)=b.threadgroup() {
        v["threadgroupMemoryAlignment"]=json!(t.threadgroup_memory_alignment());
        v["threadgroupMemoryDataSize"]=json!(t.threadgroup_memory_data_size());
    }
    if let Some(t)=b.object_payload() {
        v["objectPayloadAlignment"]=json!(t.object_payload_alignment());
        v["objectPayloadDataSize"]=json!(t.object_payload_data_size());
    }
    v
}
fn bindings(b: &[Binding]) -> Value { Value::Array(b.iter().map(binding).collect()) }

unsafe fn output_json<F>(output:*mut *mut c_char, size:*mut usize,
                         error:*mut ShaderError, body:F) -> i32
where F:FnOnce()->Result<Value> {
    if !output.is_null() { *output=ptr::null_mut(); }
    if !size.is_null() { *size=0; }
    let result=catch_unwind(AssertUnwindSafe(|| {
        if output.is_null() || size.is_null() { return Err(invalid("Missing reflection output")); }
        let value=cstring(serde_json::to_string(&body()?).map_err(|e|invalid(e.to_string()))?)?;
        *size=value.as_bytes().len(); *output=value.into_raw(); Ok(())
    }));
    report(error,result)
}

#[no_mangle]
pub unsafe extern "C" fn MMMetalReflectFunction(input:*const c_void,size:usize,entry:*const c_char,
    values:*const ConstantValue,count:usize,mode:u32,output:*mut *mut c_char,
    output_size:*mut usize,error:*mut ShaderError)->i32 {
    output_json(output,output_size,error,|| {
        let library=Library::parse(bytes(input,size,64*1024*1024)?)?;
        let f=library.function(name(entry)?)?;
        let budget=Budget::standard();
        match mode {
            0|1 => {
                let p=if mode==1 {f.specialized_properties(&constants(values,count)?,&budget)?}
                      else {f.properties(&budget)?};
                let mut c=serde_json::Map::new();
                if mode==0 {
                    for (name,v) in f.function_constants_dictionary()? {
                        c.insert(name,json!({"name":v.name(),"type":v.data_type() as u32,
                            "index":v.index(),"required":v.is_required()}));
                    }
                }
                Ok(json!({"functionType":p.function_type() as u32,
                    "patchType":p.patch_type() as u32,
                    "patchControlPointCount":p.patch_control_point_count().map(i64::from).unwrap_or(-1),
                    "vertexAttributes":attributes(p.vertex_attributes()),
                    "stageInputAttributes":attributes(p.stage_input_attributes()),"constants":c}))
            },
            2 => Ok(match f.reflection(&budget)? {
                None=>Value::Null, Some(r)=>json!({"bindings":bindings(r.bindings()),
                    "userAnnotation":r.user_annotation()})
            }),
            3 => {
                let mut fd=FunctionDescriptor::new(f);
                fd.constants=constants(values,count)?;
                let d=metal2vulkan::pipeline::ComputePipelineDescriptor::new(StageSource::Function(fd));
                let r=d.reflection(&budget)?;
                Ok(json!({"arguments":bindings(r.bindings())}))
            },
            _ => Err(invalid("Unknown reflection mode"))
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn MMMetalReflectRender(request:*const CompilerRequest,
    output:*mut *mut c_char,size:*mut usize,error:*mut ShaderError)->i32 {
    output_json(output,size,error,|| {
        let r=request.as_ref().ok_or_else(||invalid("Missing render reflection descriptor"))?;
        let library=Library::parse(bytes(r.vertex_bytes,r.vertex_size,64*1024*1024)?)?;
        let mut vf=FunctionDescriptor::new(library.function(name(r.vertex_name)?)?);
        vf.constants=constants(r.vertex_constants,r.vertex_constant_count)?;
        let mut d=RenderPipelineDescriptor::new(StageSource::Function(vf));
        if !r.fragment_name.is_null() {
            let library=Library::parse(bytes(r.fragment_bytes,r.fragment_size,64*1024*1024)?)?;
            let mut ff=FunctionDescriptor::new(library.function(name(r.fragment_name)?)?);
            ff.constants=constants(r.fragment_constants,r.fragment_constant_count)?;
            d.fragment=Some(StageSource::Function(ff));
        }
        let a=list(r.attributes,r.attribute_count,31)?;
        let l=list(r.layouts,r.layout_count,31)?;
        if !a.is_empty() || !l.is_empty() {
            let mut vd=VertexDescriptor::new();
            for a in a {
                let format=metal::VertexFormat::try_from(a.format).map_err(|_|invalid("Unknown vertex format"))?;
                if vd.attributes.insert(a.index,VertexAttributeDescriptor::new(format,a.offset,a.buffer_index)).is_some() {
                    return Err(invalid("Duplicate vertex attribute"));
                }
            }
            for l in l {
                let mut layout=VertexBufferLayoutDescriptor::new(Some(l.stride));
                layout.step_function=metal::VertexStepFunction::try_from(l.step_function).map_err(|_|invalid("Unknown vertex step function"))?;
                layout.step_rate=l.step_rate;
                if vd.layouts.insert(l.buffer_index,layout).is_some() {return Err(invalid("Duplicate vertex layout"));}
            }
            d.vertex_descriptor=Some(vd);
        }
        let r=d.reflection(&Budget::standard())?;
        Ok(json!({"vertexArguments":bindings(r.vertex_bindings()),
            "fragmentArguments":bindings(r.fragment_bindings()),"tileArguments":bindings(r.tile_bindings())}))
    })
}

fn argument_layout(l:&ArgumentBufferLayout)->Result<Value> {
    let mut members=Vec::new();
    for m in &l.members {
        let kind=match m.ty {ArgumentType::Constant=>0,ArgumentType::Buffer=>1,
            ArgumentType::Texture=>2,ArgumentType::Sampler=>3,ArgumentType::Struct=>4,
            _=>return Err(Failure(UNSUPPORTED,"Advanced argument resource kind is unsupported".into()))};
        members.push(json!({"id":m.id,"count":m.count,"idsPerElement":m.ids_per_element,
            "offset":m.offset,"stride":m.stride,"size":m.size,"kind":kind,
            "layout":match &m.layout {Some(l)=>argument_layout(l)?,None=>Value::Null}}));
    }
    Ok(json!({"encodedLength":l.encoded_length,"alignment":l.alignment,"members":members}))
}

#[no_mangle]
pub unsafe extern "C" fn MMMetalFunctionArgumentLayout(input:*const c_void,size:usize,entry:*const c_char,
    values:*const ConstantValue,count:usize,index:u32,output:*mut *mut c_char,
    output_size:*mut usize,error:*mut ShaderError)->i32 {
    output_json(output,output_size,error,|| {
        let library=Library::parse(bytes(input,size,64*1024*1024)?)?;
        let f=library.function(name(entry)?)?;
        let l=f.argument_buffer_layout(index,&constants(values,count)?,&Budget::standard())
            .map_err(|e|Failure(UNSUPPORTED,format!("Argument layout: {e:?}")))?;
        argument_layout(&l)
    })
}

/* The C metallib parser validates the container, wrappers and hashes. This
 * separate source-frontend gate decodes actual LLVM/AIR rather than treating
 * public metadata or an opaque compiler reply as a compiled AIR module. */
#[no_mangle]
pub unsafe extern "C" fn MMMetalValidateAIRFunction(input:*const c_void,size:usize,
    entry:*const c_char,stage:u32,error:*mut ShaderError)->i32 {
    let result=catch_unwind(AssertUnwindSafe(|| {
        if stage>255 {return Err(invalid("Invalid source function stage"));}
        let library=Library::from_air(bytes(input,size,64*1024*1024)?)?;
        let f=library.function(name(entry)?)?;
        if f.info().stage!=metal2vulkan::library::FunctionStage::from_type_tag(stage as u8) {
            return Err(invalid("Source metallib function stage differs from decoded AIR"));
        }
        Ok(())
    }));
    report(error,result)
}
