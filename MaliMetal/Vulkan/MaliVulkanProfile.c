/* SPDX-License-Identifier: MIT */
#include "MaliVulkanProfile.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

struct json_writer { char *data; size_t length,capacity; unsigned depth; unsigned first[16]; int failed; };
struct json_name { uint64_t value; const char *name; };
static void append(struct json_writer *j,const char *fmt,...)
{
    if(j->failed)return;
    va_list args,copy;va_start(args,fmt);va_copy(copy,args);
    int n=vsnprintf(NULL,0,fmt,copy);va_end(copy);
    if(n<0||(size_t)n>2*1024*1024-j->length){j->failed=1;va_end(args);return;}
    size_t required=j->length+(size_t)n+1;
    if(required>j->capacity){size_t cap=j->capacity?j->capacity:4096;while(cap<required)cap*=2;
        char *p=realloc(j->data,cap);if(!p){j->failed=1;va_end(args);return;}j->data=p;j->capacity=cap;}
    vsnprintf(j->data+j->length,j->capacity-j->length,fmt,args);va_end(args);j->length+=(size_t)n;
}
static void key(struct json_writer *j,const char *name)
{if(!j->depth){j->failed=1;return;}if(!j->first[j->depth-1])append(j,",");j->first[j->depth-1]=0;append(j,"\"%s\":",name);}
static void object(struct json_writer *j,const char *name)
{if(name)key(j,name);if(j->depth==16){j->failed=1;return;}append(j,"{");j->first[j->depth++]=1;}
static void close_object(struct json_writer *j){if(!j->depth){j->failed=1;return;}append(j,"}");j->depth--;}
static void json_unsigned(struct json_writer *j,const char *name,uint64_t value)
{key(j,name);append(j,"%llu",(unsigned long long)value);}
static void json_signed(struct json_writer *j,const char *name,int64_t value)
{key(j,name);append(j,"%lld",(long long)value);}
static void json_float(struct json_writer *j,const char *name,double value)
{if(!isfinite(value)){j->failed=1;return;}key(j,name);append(j,"%.9g",value);}
static void json_flags(struct json_writer *j,const char *name,uint64_t value,const struct json_name *table)
{
    key(j,name);append(j,"[");unsigned n=0;
    for(;table->name;table++)if(table->value&&(table->value&(table->value-1))==0&&(value&table->value)){
        append(j,"%s\"%s\"",n++?",":"",table->name);value&=~table->value;}
    append(j,"]");
}
static void json_enum(struct json_writer *j,const char *name,uint64_t value,const struct json_name *table)
{for(;table->name;table++)if(table->value==value){key(j,name);append(j,"\"%s\"",table->name);return;}}
#define MMVK_JSON_BOOL(j,name,value) do {key(j,name);append(j,(value)?"true":"false");}while(0)
#define MMVK_JSON_ARRAY(j,name,values,count) do {key(j,name);append(j,"[");for(size_t a_=0;a_<(count);a_++)append(j,"%s%.9g",a_?",":"",(double)(values)[a_]);append(j,"]");}while(0)
#include "MaliVulkanProfileFields.h"

void mmvk_profile_export(const char *json)
{
    const char *path=getenv("MALIMETAL_NATIVE_PROFILE_PATH");
    if(!path||!path[0]||!json)return;
    char temporary[4096];
    int n=snprintf(temporary,sizeof(temporary),"%s.XXXXXX",path);
    if(path[0]!='/'||n<0||(size_t)n>=sizeof(temporary)){
        fprintf(stderr,"[MaliMetal] Native profile export path is invalid\n");return;
    }
    int fd=mkstemp(temporary);
    if(fd<0){fprintf(stderr,"[MaliMetal] Native profile export open failed: %d\n",errno);return;}
    size_t size=strlen(json),offset=0;int failure=0;
    if(fcntl(fd,F_SETFD,FD_CLOEXEC))failure=errno;
    while(!failure&&offset<size){
        ssize_t written=write(fd,json+offset,size-offset);
        if(written<0&&errno==EINTR)continue;
        if(written<=0){failure=written<0?errno:EIO;break;}
        offset+=(size_t)written;
    }
    if(!failure&&fsync(fd))failure=errno;
    if(close(fd)&&!failure)failure=errno;
    if(!failure&&rename(temporary,path))failure=errno;
    if(failure){unlink(temporary);fprintf(stderr,"[MaliMetal] Native profile export failed: %d\n",failure);}
    else fprintf(stderr,"[MaliMetal] Native profile saved: %zu bytes to %s\n",size,path);
}

char *mmvk_profile_create(VkPhysicalDevice physical,
    const VkPhysicalDeviceFeatures *enabled,const VkPhysicalDeviceVulkan11Features *enabled11,
    const VkPhysicalDeviceVulkan12Features *enabled12,const VkPhysicalDeviceProperties *p,
    const VkPhysicalDeviceVulkan11Properties *p11,const VkPhysicalDeviceVulkan12Properties *p12,
    PFN_vkGetPhysicalDeviceFormatProperties get_format,PFN_vkGetPhysicalDeviceImageFormatProperties2 get_image_format,
    const MMVKProfileSelection *selection)
{
    if(!physical||!enabled||!enabled11||!enabled12||!p||!p11||!p12||!get_format||!get_image_format||p->apiVersion<VK_API_VERSION_1_2)return NULL;
    struct json_writer j={0};VkSampleCountFlags color_samples[185]={0};
    uint32_t api=selection?selection->api_version:VK_API_VERSION_1_2;
    if(api<VK_API_VERSION_1_2||api>p->apiVersion)return NULL;
    object(&j,NULL);key(&j,"profiles");append(&j,"{\"actual\":{\"api-version\":\"%u.%u.%u\",\"capabilities\":[\"actual\"]}}",
        VK_API_VERSION_MAJOR(api),VK_API_VERSION_MINOR(api),VK_API_VERSION_PATCH(api));
    object(&j,"capabilities");object(&j,"actual");object(&j,"features");
    object(&j,"VkPhysicalDeviceFeatures");serialize_VkPhysicalDeviceFeatures(&j,enabled);close_object(&j);
    object(&j,"VkPhysicalDeviceVulkan11Features");serialize_VkPhysicalDeviceVulkan11Features(&j,enabled11);close_object(&j);
    object(&j,"VkPhysicalDeviceVulkan12Features");serialize_VkPhysicalDeviceVulkan12Features(&j,enabled12);close_object(&j);
    if(api>=VK_API_VERSION_1_3){
        if(!selection||!selection->features13||!selection->properties13){free(j.data);return NULL;}
        object(&j,"VkPhysicalDeviceVulkan13Features");serialize_VkPhysicalDeviceVulkan13Features(&j,selection->features13);close_object(&j);
    }
    if(selection&&selection->graphics&&(selection->graphics->extensions&MMVK_GRAPHICS_EXT_TILE_IMAGE)){
        const MMVKGraphicsCapabilities *g=selection->graphics;
        object(&j,"VkPhysicalDeviceShaderTileImageFeaturesEXT");
        MMVK_JSON_BOOL(&j,"shaderTileImageColorReadAccess",g->tile_color_read);
        MMVK_JSON_BOOL(&j,"shaderTileImageDepthReadAccess",g->tile_depth_read);
        MMVK_JSON_BOOL(&j,"shaderTileImageStencilReadAccess",g->tile_stencil_read);close_object(&j);
    }
    close_object(&j);object(&j,"properties");object(&j,"VkPhysicalDeviceProperties");
    json_unsigned(&j,"apiVersion",p->apiVersion);json_unsigned(&j,"driverVersion",p->driverVersion);
    json_unsigned(&j,"vendorID",p->vendorID);json_unsigned(&j,"deviceID",p->deviceID);
    json_enum(&j,"deviceType",p->deviceType,VkPhysicalDeviceType_names);
    object(&j,"limits");serialize_VkPhysicalDeviceLimits(&j,&p->limits);close_object(&j);close_object(&j);
    object(&j,"VkPhysicalDeviceVulkan11Properties");serialize_VkPhysicalDeviceVulkan11Properties(&j,p11);close_object(&j);
    object(&j,"VkPhysicalDeviceVulkan12Properties");serialize_VkPhysicalDeviceVulkan12Properties(&j,p12);close_object(&j);
    if(api>=VK_API_VERSION_1_3){object(&j,"VkPhysicalDeviceVulkan13Properties");serialize_VkPhysicalDeviceVulkan13Properties(&j,selection->properties13);close_object(&j);}
    if(selection&&selection->graphics&&(selection->graphics->extensions&MMVK_GRAPHICS_EXT_TILE_IMAGE)){
        const MMVKGraphicsCapabilities *g=selection->graphics;
        object(&j,"VkPhysicalDeviceShaderTileImagePropertiesEXT");
        MMVK_JSON_BOOL(&j,"shaderTileImageCoherentReadAccelerated",g->tile_coherent_accelerated);
        MMVK_JSON_BOOL(&j,"shaderTileImageReadSampleFromPixelRateInvocation",g->tile_sample_from_pixel);
        MMVK_JSON_BOOL(&j,"shaderTileImageReadFromHelperInvocation",g->tile_helper_reads);close_object(&j);
    }
    close_object(&j);object(&j,"formats");
    for(const struct json_name *f=VkFormat_names;f->name;f++){
        VkFormatProperties props;get_format(physical,(VkFormat)f->value,&props);
        if(!(props.linearTilingFeatures|props.optimalTilingFeatures|props.bufferFeatures))continue;
        object(&j,f->name);object(&j,"VkFormatProperties");
        json_flags(&j,"linearTilingFeatures",props.linearTilingFeatures,VkFormatFeatureFlags_names);
        json_flags(&j,"optimalTilingFeatures",props.optimalTilingFeatures,VkFormatFeatureFlags_names);
        json_flags(&j,"bufferFeatures",props.bufferFeatures,VkFormatFeatureFlags_names);close_object(&j);close_object(&j);
        if(props.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT){
            VkPhysicalDeviceImageFormatInfo2 info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
                .format=(VkFormat)f->value,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
            VkImageFormatProperties2 out={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
            if(get_image_format(physical,&info,&out)==VK_SUCCESS)color_samples[f->value]=out.imageFormatProperties.sampleCounts;
        }
    }
    close_object(&j);object(&j,"extensions");
    if(selection&&selection->graphics&&(selection->graphics->extensions&MMVK_GRAPHICS_EXT_TILE_IMAGE))
        json_unsigned(&j,VK_EXT_SHADER_TILE_IMAGE_EXTENSION_NAME,VK_EXT_SHADER_TILE_IMAGE_SPEC_VERSION);
    close_object(&j);close_object(&j);close_object(&j);
    object(&j,"m2v_color_attachment_sample_counts");object(&j,"actual");
    for(const struct json_name *f=VkFormat_names;f->name;f++)if(color_samples[f->value])json_flags(&j,f->name,color_samples[f->value],VkSampleCountFlags_names);
    close_object(&j);close_object(&j);close_object(&j);
    if(j.failed||j.depth){free(j.data);return NULL;}return j.data;
}
