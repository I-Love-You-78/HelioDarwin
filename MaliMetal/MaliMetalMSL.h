#pragma once
#import "MaliMetalDevice.h"

/* Returns a +1 library made from genuine system-compiler MTLB/AIR output.
 * Source compilation does not claim that every resulting function is executable
 * by the enabled native GPU profile; normal pipeline compilation checks that.
 * The default deadline is 20 seconds. Outstanding native requests are bounded
 * and retained across timeout until their compiler/callback ownership is safe. */
id<MTLLibrary> MaliMetalNewLibraryWithSource(MaliMetalDevice *device,NSString *source,
                                            MTLCompileOptions *options,NSError **error);

/* Native AIR decoder gate supplied by api_reflection.rs. */
struct MMShaderError;
int32_t MMMetalValidateAIRFunction(const void *,size_t,const char *,uint32_t,
                                  struct MMShaderError *);
