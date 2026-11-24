// Fault injection at the libasound boundary; actual ALSA null handles still run.
#include <alsa/asoundlib.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
extern "C" snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm,void *buffer,snd_pcm_uframes_t frames) {
 using Read=snd_pcm_sframes_t(*)(snd_pcm_t*,void*,snd_pcm_uframes_t);
 static auto real=reinterpret_cast<Read>(dlsym(RTLD_NEXT,"snd_pcm_readi"));
 static std::atomic<unsigned> calls{0};
 if(++calls==100)return std::getenv("JFX_TEST_FATAL")?-ENODEV:-EPIPE;
 // Exercise partial transfers independently of period size.
 return real(pcm,buffer,frames>1?frames/2:frames);
}
