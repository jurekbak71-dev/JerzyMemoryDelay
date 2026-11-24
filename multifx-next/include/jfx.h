#ifndef JFX_NEXT_H
#define JFX_NEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_MAGIC 0x3346584aU
#define JFX_MAJOR 3U
#define JFX_MIDI_INPUT 1U
#define JFX_STATE 2U
#define JFX_EXPORT __attribute__((visibility("default")))
typedef struct JfxMidi {
    uint32_t frame;
    uint32_t size;
    uint8_t data[3];
} JfxMidi;
typedef struct JfxTransport {
    double bpm;
    uint64_t frame;
    uint32_t playing;
    uint32_t valid;
} JfxTransport;
typedef struct JfxBlock {
    uint32_t frames;
    const float *const *inputs;
    float *const *outputs;
    const float *parameters;
    const JfxMidi *events;
    uint32_t event_count;
    JfxTransport transport;
} JfxBlock;
typedef struct JfxParameter {
    const char *key;
    const char *name;
    float minimum, maximum, initial;
} JfxParameter;
// All callbacks must contain exceptions. process is nonblocking and allocation-free.
// Other callbacks run on the controller; state callbacks require a quiescent instance.
typedef struct JfxDescriptor {
    uint32_t magic, struct_size, abi_major, abi_minor;
    const char *id;
    const char *version;
    const char *name;
    uint32_t audio_inputs, audio_outputs, flags, parameter_count;
    const JfxParameter *parameters;
    uint32_t latency_frames, state_schema;
    void *(*create)(double sample_rate, uint32_t max_frames, const char *asset_path);
    void (*destroy)(void *instance);
    void (*process)(void *instance, const JfxBlock *block);
    uint32_t (*state_size)(void *instance);
    int32_t (*save_state)(void *instance, void *destination, uint32_t size);
    int32_t (*load_state)(void *instance, const void *source, uint32_t size);
    uint64_t (*resource_bytes)(void *instance);
} JfxDescriptor;
typedef const JfxDescriptor *(*JfxEntryV3)(void);
JFX_EXPORT const JfxDescriptor *jfx_entry_v3(void);
#ifdef __cplusplus
}
#endif
#endif
