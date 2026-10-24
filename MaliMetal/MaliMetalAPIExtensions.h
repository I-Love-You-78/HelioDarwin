#pragma once
#import "MaliMetalDevice.h"
#import "Shader/MaliMetalShader.h"

/* All metadata returned by these helpers is derived from the actual library and
 * specialization. The native AIR provider is required for decoded reflection. */
id MaliMetalFunctionProperties(id function, BOOL specialized, NSError **error);
NSDictionary *MaliMetalFunctionConstantsDictionary(id function, BOOL specialized,
                                                   NSError **error);
id MaliMetalLibraryFunctionReflection(id library, NSString *name, NSError **error);
NSArray *MaliMetalFunctionAttributes(id properties, BOOL vertex);
NSUInteger MaliMetalFunctionPatchType(id properties);
NSInteger MaliMetalFunctionPatchControlPointCount(id properties);
id MaliMetalNewFunctionArgumentEncoder(id function, NSUInteger index, NSError **error);

/* An execution contract, not a feature/profile claim. The device exposes each
 * bit only after the canonical resident/handle table consumer for that resource
 * kind is wired. A missing selector or bit disables encoding that kind. Buffers
 * additionally require a real, live, bounded native device address at encoding. */
enum {
    MM_METAL_ARGUMENT_BUFFERS = 1u,
    MM_METAL_ARGUMENT_TEXTURES = 2u,
    MM_METAL_ARGUMENT_SAMPLERS = 4u
};
@interface MaliMetalDevice (MaliMetalArgumentConsumer)
- (uint32_t)maliArgumentBufferConsumerCapabilities;
@end

typedef void (^MMMetalLibraryCompletion)(id<MTLLibrary>, NSError *);
typedef void (^MMMetalFunctionCompletion)(id<MTLFunction>, NSError *);
typedef void (^MMMetalPipelineCompletion)(id, NSError *);
typedef void (^MMMetalPipelineReflectionCompletion)(id, id, NSError *);

@interface MaliMetalDevice (MaliMetalAPIExtensions)
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options
           completionHandler:(MMMetalLibraryCompletion)handler;
- (void)newLibraryWithURL:(NSURL *)url completionHandler:(MMMetalLibraryCompletion)handler;
- (void)newLibraryWithData:(dispatch_data_t)data completionHandler:(MMMetalLibraryCompletion)handler;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                          completionHandler:(MMMetalPipelineCompletion)handler;
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                                   options:(NSUInteger)options reflection:(id *)reflection
                                     error:(NSError **)error;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor
                                    options:(NSUInteger)options
                          completionHandler:(MMMetalPipelineReflectionCompletion)handler;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)function
                         completionHandler:(MMMetalPipelineCompletion)handler;
- (id)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(NSUInteger)options
                               reflection:(id *)reflection error:(NSError **)error;
- (void)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(NSUInteger)options
                         completionHandler:(MMMetalPipelineReflectionCompletion)handler;
- (id)newComputePipelineStateWithDescriptor:(id)descriptor error:(NSError **)error;
- (id)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options
                                 reflection:(id *)reflection error:(NSError **)error;
- (void)newComputePipelineStateWithDescriptor:(id)descriptor
                           completionHandler:(MMMetalPipelineCompletion)handler;
- (void)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options
                           completionHandler:(MMMetalPipelineReflectionCompletion)handler;
- (id)newArgumentEncoderWithArguments:(NSArray *)arguments;
@end

/* Provider extension, independent of MMShaderCompiler ABI. JSON uses numeric
 * Metal enumerants and owns no Objective-C objects. Release through the existing
 * compiler free_string callback. mode: 0 raw properties, 1 specialized properties,
 * 2 library reflection (JSON null when RFLT is absent), 3 compute reflection. */
int32_t MMMetalReflectFunction(const void *, size_t, const char *,
                             const MMShaderConstantValue *, size_t, uint32_t mode,
                             char **, size_t *, MMShaderError *);
int32_t MMMetalReflectRender(const MMShaderCompilerRequest *, char **, size_t *, MMShaderError *);
int32_t MMMetalFunctionArgumentLayout(const void *, size_t, const char *,
                                    const MMShaderConstantValue *, size_t, uint32_t,
                                    char **, size_t *, MMShaderError *);
