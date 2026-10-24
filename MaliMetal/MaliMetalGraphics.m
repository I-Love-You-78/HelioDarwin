/* SPDX-License-Identifier: MIT */
#import "MaliMetalGraphics.h"
#include "Vulkan/MaliVulkanResources.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Private declarations only; the system Metal framework owns its descriptors. */
@protocol MaliMetalStencilMasks
- (uint32_t)readMask;
- (uint32_t)writeMask;
@end
@interface NSObject (MaliMetalGraphicsDescriptors)
- (NSUInteger)depthCompareFunction;
- (BOOL)isDepthWriteEnabled;
- (id)frontFaceStencil;
- (id)backFaceStencil;
- (NSUInteger)stencilCompareFunction;
- (NSUInteger)stencilFailureOperation;
- (NSUInteger)depthFailureOperation;
- (NSUInteger)depthStencilPassOperation;
- (NSString *)label;
- (NSUInteger)rasterSampleCount;
- (BOOL)isAlphaToCoverageEnabled;
- (BOOL)isAlphaToOneEnabled;
- (BOOL)isRasterizationEnabled;
- (NSUInteger)depthAttachmentPixelFormat;
- (NSUInteger)stencilAttachmentPixelFormat;
- (id)colorAttachments;
- (id)objectAtIndexedSubscript:(NSUInteger)slot;
- (NSUInteger)pixelFormat;
- (id)depthAttachment;
- (id)stencilAttachment;
- (id)texture;
- (id)resolveTexture;
- (NSUInteger)level;
- (NSUInteger)slice;
- (NSUInteger)depthPlane;
- (NSUInteger)resolveLevel;
- (NSUInteger)resolveSlice;
- (NSUInteger)resolveDepthPlane;
- (NSUInteger)loadAction;
- (NSUInteger)storeAction;
- (MTLClearColor)clearColor;
- (double)clearDepth;
- (uint32_t)clearStencil;
- (NSUInteger)renderTargetArrayLength;
- (NSUInteger)renderTargetWidth;
- (NSUInteger)renderTargetHeight;
- (NSUInteger)storeActionOptions;
- (NSUInteger)depthResolveFilter;
- (NSUInteger)stencilResolveFilter;
- (NSUInteger)getSamplePositions:(void *)positions count:(NSUInteger)count;
- (MMVKImage *)maliImage;
@end
static MMVKResult graphics_error(MMVKError *e,MMVKResult code,const char *message)
{
    if(e){e->code=code;e->native_code=0;snprintf(e->message,sizeof(e->message),"%s",message);}return code;
}
static MMVKResult decode_stencil(id descriptor,MMVKStencilFace *out,MMVKError *error)
{
    *out=(MMVKStencilFace){0,0,0,7,UINT32_MAX,UINT32_MAX};
    if(!descriptor)return MMVK_OK;
    NSUInteger compare=[descriptor stencilCompareFunction],fail=[descriptor stencilFailureOperation],depthFail=[descriptor depthFailureOperation],pass=[descriptor depthStencilPassOperation];
    /* MTLStencilOperation and the Vulkan core operations have the same values. */
    static const uint32_t operations[]={0,1,2,3,4,5,6,7};
    if(compare>7||fail>7||depthFail>7||pass>7)return graphics_error(error,MMVK_ERROR_ARGUMENT,"Invalid depth/stencil descriptor operation");
    id<MaliMetalStencilMasks> masks=descriptor;
    *out=(MMVKStencilFace){operations[fail],operations[pass],operations[depthFail],(uint32_t)compare,[masks readMask],[masks writeMask]};
    return MMVK_OK;
}
@implementation MaliMetalDepthStencilState
- (instancetype)initWithDevice:(id)device descriptor:(id)descriptor error:(MMVKError *)error
{
    self=[super init];if(!self)return nil;
    if(!device||!descriptor){graphics_error(error,MMVK_ERROR_ARGUMENT,"Missing depth/stencil descriptor or device");[self release];return nil;}
    mmvk_graphics_state_default(&_state);
    NSUInteger compare=[descriptor depthCompareFunction];
    if(compare>7){graphics_error(error,MMVK_ERROR_ARGUMENT,"Invalid depth compare function");[self release];return nil;}
    _state.depth_compare=(uint32_t)compare;_state.depth_write=!![descriptor isDepthWriteEnabled];
    _state.depth_test=_state.depth_write||compare!=7;
    id front=[descriptor frontFaceStencil],back=[descriptor backFaceStencil];
    if(decode_stencil(front,&_state.front,error)||decode_stencil(back,&_state.back,error)){[self release];return nil;}
    _state.stencil_test=!!(front||back);_device=[device retain];_label=[[descriptor label] copy];
    return self;
}
- (void)dealloc {[_device release];[_label release];[super dealloc];}
- (id)device {return _device;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)label {NSString *next=[label copy];[_label release];_label=next;}
- (const MMVKGraphicsState *)maliGraphicsDepthStencilState {return &_state;}
@end
MMVKResult MaliMetalGraphicsTopology(NSUInteger primitive,uint32_t *topology,MMVKError *e)
{
    if(!topology||primitive>4)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid Metal primitive topology");
    *topology=(uint32_t)primitive;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsCullMode(NSUInteger mode,MMVKGraphicsState *s,MMVKError *e)
{
    if(!s||mode>2)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid Metal cull mode");
    s->cull_mode=(uint32_t)mode;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsWinding(NSUInteger winding,MMVKGraphicsState *s,MMVKError *e)
{
    if(!s||winding>1)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid Metal front winding");
    s->front_face=winding?0:1;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsFillMode(NSUInteger mode,MMVKGraphicsState *s,MMVKError *e)
{
    if(!s||mode>1)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid Metal triangle fill mode");
    s->polygon_mode=(uint32_t)mode;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsDepthClipMode(NSUInteger mode,MMVKGraphicsState *s,MMVKError *e)
{
    if(!s||mode>1)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid Metal depth clip mode");
    s->depth_clamp=(uint32_t)mode;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsApplyDepthStencil(id state,id device,MMVKGraphicsState *out,MMVKError *e)
{
    if(!out)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Missing graphics state");
    MMVKGraphicsState defaults;mmvk_graphics_state_default(&defaults);
    const MMVKGraphicsState *source=&defaults;
    if(state){
        if(![state isKindOfClass:[MaliMetalDepthStencilState class]]||[(MaliMetalDepthStencilState *)state device]!=device)
            return graphics_error(e,MMVK_ERROR_ARGUMENT,"Depth/stencil state belongs to another device");
        source=[(MaliMetalDepthStencilState *)state maliGraphicsDepthStencilState];
    }
    out->depth_test=source->depth_test;out->depth_write=source->depth_write;out->depth_compare=source->depth_compare;
    out->stencil_test=source->stencil_test;out->front=source->front;out->back=source->back;return MMVK_OK;
}
MMVKResult MaliMetalGraphicsPipelineDescription(id descriptor,MMVKGraphicsState *state,MMVKPassLayout *layout,MMVKError *e)
{
    if(!descriptor||!state||!layout)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Missing graphics pipeline description");
    NSUInteger samples=[descriptor rasterSampleCount],depth=[descriptor depthAttachmentPixelFormat],stencil=[descriptor stencilAttachmentPixelFormat];
    if(depth>UINT32_MAX||stencil>UINT32_MAX)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid depth/stencil pixel format");
    if(!samples||samples>64||(samples&(samples-1)))return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid raster sample count");
    if(depth&&stencil&&depth!=stencil)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Separate depth and stencil formats require an attachment-compatible packed texture view");
    memset(layout,0,sizeof(*layout));layout->sample_count=(uint32_t)samples;
    if(depth||stencil){layout->depth_stencil_format=mmvk_format_from_metal((uint32_t)(depth?depth:stencil));if(!layout->depth_stencil_format)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Unsupported depth/stencil pixel format");
        uint32_t a=mmvk_format_aspects(layout->depth_stencil_format);
        if((depth&&!(a&2u))||(stencil&&!(a&4u)))return graphics_error(e,MMVK_ERROR_ARGUMENT,"Pipeline depth/stencil format lacks the selected aspect");}
    for(NSUInteger n=0;n<8;n++){
        NSUInteger format=[[[descriptor colorAttachments] objectAtIndexedSubscript:n] pixelFormat];if(!format)continue;
        if(format>UINT32_MAX)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid color pixel format");
        layout->color_formats[n]=mmvk_format_from_metal((uint32_t)format);
        if(!layout->color_formats[n]||mmvk_format_aspects(layout->color_formats[n])!=1)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Unsupported color attachment format");
        layout->color_count=(uint32_t)n+1;
    }
    state->sample_count=(uint32_t)samples;state->alpha_to_coverage=!![descriptor isAlphaToCoverageEnabled];state->alpha_to_one=!![descriptor isAlphaToOneEnabled];
    state->rasterizer_discard=![descriptor isRasterizationEnabled];
    return MMVK_OK;
}
static MMVKResult capture_view(id attachment,id texture,BOOL resolve,MMVKAttachmentView *out,MMVKError *e)
{
    if(!texture)return MMVK_OK;
    if(![texture respondsToSelector:@selector(maliImage)]||![texture maliImage])return graphics_error(e,MMVK_ERROR_ARGUMENT,"Attachment texture has no native allocation");
    NSUInteger level=resolve?[attachment resolveLevel]:[attachment level],slice=resolve?[attachment resolveSlice]:[attachment slice];
    NSUInteger plane=resolve?[attachment resolveDepthPlane]:[attachment depthPlane];
    if(level>UINT32_MAX||slice>UINT32_MAX||plane)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Invalid attachment subresource or unsupported 3D depth plane");
    *out=(MMVKAttachmentView){[texture maliImage],(uint32_t)level,(uint32_t)slice,1};return MMVK_OK;
}
static MMVKResult capture_attachment(id descriptor,MMVKPassAttachment *out,id *texture,id *resolve,BOOL *preserve,MMVKError *e)
{
    *texture=[descriptor texture];*resolve=[descriptor resolveTexture];if(!*texture){if(*resolve)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Resolve texture lacks a source");return MMVK_OK;}
    NSUInteger load=[descriptor loadAction],store=[descriptor storeAction];
    if(load>2||store>3)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Unsupported attachment load/store action");
    if([descriptor respondsToSelector:@selector(storeActionOptions)]&&[descriptor storeActionOptions])return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Custom attachment store options require native resolve-mode lowering");
    MMVKResult r=capture_view(descriptor,*texture,NO,&out->view,e);if(r)return r;
    r=capture_view(descriptor,*resolve,YES,&out->resolve,e);if(r)return r;
    out->load=(uint32_t)load;out->store=(uint32_t)store;*preserve=load==1;
    return MMVK_OK;
}
MMVKResult MaliMetalGraphicsCapturePass(id descriptor,MaliMetalGraphicsPass *out,MMVKError *e)
{
    if(!descriptor||!out)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Missing render pass descriptor");
    memset(out,0,sizeof(*out));out->native.layers=1;
    if([descriptor respondsToSelector:@selector(getSamplePositions:count:)]&&[descriptor getSamplePositions:NULL count:0])
        return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Custom sample positions require enabled native sample-location state");
    if([descriptor respondsToSelector:@selector(renderTargetArrayLength)]){
        NSUInteger layers=[descriptor renderTargetArrayLength];if(layers>UINT32_MAX)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid render target array length");
        out->native.layers=layers?(uint32_t)layers:1;
    }
    uint32_t minWidth=UINT32_MAX,minHeight=UINT32_MAX;
    for(NSUInteger n=0;n<8;n++){
        id attachment=[[descriptor colorAttachments] objectAtIndexedSubscript:n];BOOL preserve=NO;
        MMVKPassAttachment *p=&out->native.colors[n];
        MMVKResult r=capture_attachment(attachment,p,&out->colors[n],&out->resolves[n],&preserve,e);if(r)return r;
        out->preserve_color[n]=preserve;if(!out->colors[n])continue;
        const MMVKImageDescriptorEx *x=mmvk_image_descriptor(p->view.image);uint32_t level=p->view.level;
        if(level>=x->mip_levels)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Render attachment mip level is out of bounds");
        uint32_t w=x->width>>level,h=x->height>>level;if(!w)w=1;if(!h)h=1;
        if(w<minWidth)minWidth=w;if(h<minHeight)minHeight=h;
        p->view.layer_count=out->native.layers;if(p->resolve.image)p->resolve.layer_count=out->native.layers;
        MTLClearColor c=[attachment clearColor];p->color[0]=c.red;p->color[1]=c.green;p->color[2]=c.blue;p->color[3]=c.alpha;
        out->native.color_count=(uint32_t)n+1;
    }
    id depth=[descriptor depthAttachment],stencil=[descriptor stencilAttachment],dt=[depth texture],st=[stencil texture];
    MMVKPassAttachment *p=&out->native.depth_stencil;
    if(dt||st){
        if(dt&&st&&(dt!=st||[depth level]!=[stencil level]||[depth slice]!=[stencil slice]||[depth depthPlane]!=[stencil depthPlane]))
            return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Depth and stencil must select the same packed texture subresource");
        id selected=dt?depth:stencil,ignored=nil;
        MMVKResult r=capture_attachment(selected,p,&out->depth_stencil,&ignored,&out->preserve_depth,e);if(r)return r;
        id dr=dt?[depth resolveTexture]:nil,sr=st?[stencil resolveTexture]:nil;
        if(dr&&sr&&(dr!=sr||[depth resolveLevel]!=[stencil resolveLevel]||[depth resolveSlice]!=[stencil resolveSlice]||[depth resolveDepthPlane]!=[stencil resolveDepthPlane]))
            return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Depth/stencil resolves must select the same packed texture subresource");
        out->depth_stencil_resolve=dr?dr:sr;
        memset(&p->resolve,0,sizeof(p->resolve));
        if(out->depth_stencil_resolve){r=capture_view(dr?depth:stencil,out->depth_stencil_resolve,YES,&p->resolve,e);if(r)return r;}
        if(st&&[stencil respondsToSelector:@selector(storeActionOptions)]&&[stencil storeActionOptions])return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Custom stencil store options require native resolve-mode lowering");
        NSUInteger depthLoad=dt?[depth loadAction]:MMVK_LOAD_LOAD,depthStore=dt?[depth storeAction]:MMVK_STORE_STORE;
        NSUInteger stencilLoad=st?[stencil loadAction]:MMVK_LOAD_LOAD,stencilStore=st?[stencil storeAction]:MMVK_STORE_STORE;
        if(depthLoad>2||depthStore>3||stencilLoad>2||stencilStore>3)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Unsupported depth/stencil load/store action");
        p->load=(uint32_t)depthLoad;p->store=(uint32_t)depthStore;p->stencil_load=(uint32_t)stencilLoad;p->stencil_store=(uint32_t)stencilStore;
        if(mmvk_image_descriptor(p->view.image)->transient){
            if(!dt){p->load=MMVK_LOAD_DONT_CARE;p->store=MMVK_STORE_DONT_CARE;}
            if(!st){p->stencil_load=MMVK_LOAD_DONT_CARE;p->stencil_store=MMVK_STORE_DONT_CARE;}
        }
        if(dr){NSUInteger filter=[depth depthResolveFilter];if(filter>2)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid depth resolve filter");p->depth_resolve_mode=filter==0?1:filter==1?4:8;}
        if(sr){NSUInteger filter=[stencil stencilResolveFilter];if(filter>1)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Invalid stencil resolve filter");
            if(filter==1&&p->depth_resolve_mode!=1)return graphics_error(e,MMVK_ERROR_UNSUPPORTED,"Stencil depth-resolved-sample needs a sample-zero depth resolve or a native correlated resolve operation");
            p->stencil_resolve_mode=1;
        }
        p->depth=dt?[depth clearDepth]:1;p->stencil=st?[stencil clearStencil]:0;
        p->view.layer_count=out->native.layers;if(p->resolve.image)p->resolve.layer_count=out->native.layers;
        uint32_t sourceAspects=mmvk_format_aspects(mmvk_image_format(p->view.image));
        out->preserve_depth=!!(sourceAspects&2u)&&p->load==MMVK_LOAD_LOAD;
        out->preserve_stencil=!!(sourceAspects&4u)&&p->stencil_load==MMVK_LOAD_LOAD;
        if(p->resolve.image){uint32_t targetAspects=mmvk_format_aspects(mmvk_image_format(p->resolve.image));
            out->preserve_depth_stencil_resolve=((targetAspects&2u)&&!p->depth_resolve_mode)||((targetAspects&4u)&&!p->stencil_resolve_mode);}
        const MMVKImageDescriptorEx *x=mmvk_image_descriptor(p->view.image);uint32_t level=p->view.level;
        if(level>=x->mip_levels)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Depth/stencil attachment mip is out of bounds");
        uint32_t w=x->width>>level,h=x->height>>level;if(!w)w=1;if(!h)h=1;
        if(w<minWidth)minWidth=w;if(h<minHeight)minHeight=h;
    }
    if(minWidth==UINT32_MAX||minHeight==UINT32_MAX)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Render pass has no attachments");
    out->native.width=minWidth;out->native.height=minHeight;
    if([descriptor respondsToSelector:@selector(renderTargetWidth)]&&[descriptor renderTargetWidth]){
        NSUInteger width=[descriptor renderTargetWidth];if(width>minWidth)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Render target width exceeds attachment extent");out->native.width=(uint32_t)width;
    }
    if([descriptor respondsToSelector:@selector(renderTargetHeight)]&&[descriptor renderTargetHeight]){
        NSUInteger height=[descriptor renderTargetHeight];if(height>minHeight)return graphics_error(e,MMVK_ERROR_ARGUMENT,"Render target height exceeds attachment extent");out->native.height=(uint32_t)height;
    }
    return MMVK_OK;
}
