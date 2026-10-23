#pragma once
// Stable C ABI. A standalone JFX module is a .so + same-name .json manifest.
// Do not expose std::string, C++ classes or container types across this boundary.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_ABI_VERSION 2
#define JFX_MAX_PARAMS 24
#define JFX_MAX_SLOTS 16
typedef struct JfxParameter {
    const char* key;
    const char* label;
    const char* unit;
    float default_value;  // normalized [0,1]
} JfxParameter;
typedef struct JfxDescriptor {
    uint32_t abi_version;
    const char* key;          // ASCII [a-z0-9_]+
    const char* name;         // UTF-8 display name
    uint32_t parameter_count;
    const JfxParameter* parameters;
    void* (*create)(double sample_rate);
    void (*destroy)(void* instance);
    void (*process)(void* instance, const float input[2], float output[2],
                    const float* normalized_parameters, double bpm);
} JfxDescriptor;
typedef const JfxDescriptor* (*JfxEntryPoint)(void);
// Every plugin must export: extern "C" const JfxDescriptor* jfx_plugin(void);
#ifdef __cplusplus
}
#endif
