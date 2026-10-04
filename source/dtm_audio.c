/* dtm_audio.c -- com.halfbrick.mortar.MortarAudioMixerOut, played through
 * audout.
 *
 * The Mortar engine mixes the game's sound itself (Mortar::Audio::AudioMixer)
 * on a thread of its own, and hands the result to an output: OpenSL ES
 * (MAMAudioThread_AndroidSLES) when NativeGameLib.InitOpenSLSoundManager
 * succeeds, else Java (MAMAudioThread_AndroidJava), whose Java side is this
 * small class:
 *
 *   static Create()               new MortarAudioMixerOut()
 *   static GetNativeSampleRate()  AudioTrack.getNativeOutputSampleRate(MUSIC)
 *   Init(rate)                    AudioTrack(MUSIC, rate, STEREO, PCM 16,
 *                                 getMinBufferSize(...), STREAM); play();
 *                                 returns the RATE it was given: the engine
 *                                 takes it as the rate its sound will be
 *                                 played at, and resamples its mix to it
 *   WriteData(short[] / byte[])   AudioTrack.write(data, 0, length): blocks
 *                                 until the track took it, which is what
 *                                 paces the engine's audio thread
 *
 * The port takes the Java path (OpenSL ES stays refused: port_config.h).
 * Here the "native rate" is audout's (48 kHz), so the engine mixes at the
 * device's rate and nothing is resampled; what it writes is cut into
 * audout's buffers of RT_AUDOUT_FRAMES stereo frames, and the blocking
 * submit paces the engine as AudioTrack.write did. MIT.
 */
#include <string.h>
#include <switch.h>

#include "dtm.h"
#include "rt_audout.h"
#include "util.h"

#define MIXER "com/halfbrick/mortar/MortarAudioMixerOut"

static int g_ao_ready;
static volatile int g_paused;
static volatile uint32_t g_blocks;
static int16_t g_block[RT_AUDOUT_FRAMES * 2];
static int g_fill; /* samples (not frames) in g_block */

uint32_t dtm_audio_blocks(void) { return g_blocks; }

int dtm_audio_init(void) {
  if (rt_audout_open() != 0) {
    debugPrintf("[audio] audout did not open: no sound\n");
    return -1;
  }
  g_ao_ready = 1;
  debugPrintf("[audio] audout open at %u Hz, %d-frame buffers\n", rt_audout_rate(), RT_AUDOUT_FRAMES);
  return 0;
}

/* HOME: nothing is queued while paused (the engine's thread keeps writing,
 * at the pace sound would have taken, so its clock does not run ahead). */
void dtm_audio_pause(int paused) { g_paused = paused; }

void dtm_audio_shutdown(void) {
  if (!g_ao_ready)
    return;
  rt_audout_cancel(1); /* the engine's audio thread may be inside a submit */
  rt_audout_close();
  g_ao_ready = 0;
  debugPrintf("[audio] closed after %u blocks\n", (unsigned)g_blocks);
}

/* Interleaved stereo s16 from the engine, into audout's buffers. */
static void write_samples(const int16_t *src, int count) {
  while (count > 0) {
    int n = (int)(sizeof g_block / sizeof g_block[0]) - g_fill;
    if (n > count)
      n = count;
    memcpy(g_block + g_fill, src, sizeof(int16_t) * (size_t)n);
    g_fill += n;
    src += n;
    count -= n;
    if (g_fill < (int)(sizeof g_block / sizeof g_block[0]))
      break;
    g_fill = 0;
    if (g_paused || !g_ao_ready) {
      /* one buffer's time, as if it had played */
      svcSleepThread((s64)RT_AUDOUT_FRAMES * 1000000000ll / (s64)rt_audout_rate());
      continue;
    }
    if (rt_audout_submit(g_block) == 0)
      g_blocks++;
  }
}

/* ------------------------------------------------- the Java class's methods */
JNI_H_DECL(dtm_h_mixer_create) {
  debugPrintf("[audio] MortarAudioMixerOut.Create\n");
  return jv_l(jni_new(MIXER));
}

JNI_H_DECL(dtm_h_mixer_rate) { return jv_i((jint)rt_audout_rate()); }

/* The rate the track plays at, which is what the Java returns (its own
 * argument). The engine resamples every buffer from its mixer's rate to this
 * one: any other number here (the first build answered a buffer size, 4096)
 * and it packs several times the sound into each buffer. audout plays at
 * its own rate whatever is asked, so that is the answer. */
JNI_H_DECL(dtm_h_mixer_init) {
  const jint rate = (jint)rt_audout_rate();
  debugPrintf("[audio] MortarAudioMixerOut.Init(%d Hz) -> %d Hz\n", (int)a[0].i, (int)rate);
  return jv_i(rate);
}

/* WriteData(short[]) and WriteData(byte[]): the whole array. */
JNI_H_DECL(dtm_h_mixer_write) {
  const JObj *arr = a[0].l;
  if (!arr || arr->kind != JK_ARRAY || !arr->a.data)
    return jv_none();
  if (arr->a.elem == 'S')
    write_samples((const int16_t *)arr->a.data, (int)arr->a.len);
  else if (arr->a.elem == 'B')
    write_samples((const int16_t *)arr->a.data, (int)arr->a.len / 2); /* s16, little-endian */
  return jv_none();
}
