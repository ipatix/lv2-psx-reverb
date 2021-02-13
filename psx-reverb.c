/*
  Copyright 2006-2016 David Robillard <d@drobilla.net>
  Copyright 2006 Steve Harris <steve@plugin.org.uk>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/**
   LV2 headers are based on the URI of the specification they come from, so a
   consistent convention can be used even for unofficial extensions.  The URI
   of the core LV2 specification is <http://lv2plug.in/ns/lv2core>, by
   replacing `http:/` with `lv2` any header in the specification bundle can be
   included, in this case `lv2.h`.
*/
#include "lv2/core/lv2.h"

/** Include standard C headers */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/**
   The URI is the identifier for a plugin, and how the host associates this
   implementation in code with its description in data.  In this plugin it is
   only used once in the code, but defining the plugin URI at the top of the
   file is a good convention to follow.  If this URI does not match that used
   in the data files, the host will fail to load the plugin.
*/
#define AMP_URI "http://github.com/ipatix/lv2-psx-reverb"

/**
   In code, ports are referred to by index.  An enumeration of port indices
   should be defined for readability.
*/
typedef enum {
    PSX_REV_WET = 0,
    PSX_REV_DRY = 1,
    PSX_REV_MAIN0_IN = 2,
    PSX_REV_MAIN1_IN = 3,
    PSX_REV_MAIN0_OUT = 4,
    PSX_REV_MAIN1_OUT = 5,
} PortIndex;

/**
   Every plugin defines a private structure for the plugin instance.  All data
   associated with a plugin instance is stored here, and is available to
   every instance method.  In this simple plugin, only port buffers need to be
   stored, since there is no additional instance data.
*/

typedef struct {
    float state;
    float alpha;
} low_pass_t;

typedef struct {
    // Port buffers
    const float* port_wet;
    const float* port_dry;
    const float* port_main0_in;
    const float* port_main1_in;
    float*       port_main0_out;
    float*       port_main1_out;
    // local data
    float        wet_state;
    float        dry_state;
    low_pass_t   lp_L;
    low_pass_t   lp_R;

    float        spu_buffer[0x8000];
    size_t       spu_buffer_count;

    float        tmp_bufL;
    float        tmp_bufR;
    bool         tmp_buf_filled;

    uint32_t BufferAddress;

    /* converted reverb parameters */
    uint32_t dAPF1;
    uint32_t dAPF2;
    float    vIIR;
    float    vCOMB1;
    float    vCOMB2;
    float    vCOMB3;
    float    vCOMB4;
    float    vWALL;
    float    vAPF1;
    float    vAPF2;
    uint32_t mLSAME;
    uint32_t mRSAME;
    uint32_t mLCOMB1;
    uint32_t mRCOMB1;
    uint32_t mLCOMB2;
    uint32_t mRCOMB2;
    uint32_t dLSAME;
    uint32_t dRSAME;
    uint32_t mLDIFF;
    uint32_t mRDIFF;
    uint32_t mLCOMB3;
    uint32_t mRCOMB3;
    uint32_t mLCOMB4;
    uint32_t mRCOMB4;
    uint32_t dLDIFF;
    uint32_t dRDIFF;
    uint32_t mLAPF1;
    uint32_t mRAPF1;
    uint32_t mLAPF2;
    uint32_t mRAPF2;
    float    vLIN;
    float    vRIN;
} PsxReverb;

static const uint16_t preset_room[0x20];
static const uint32_t preset_room_size;

static const uint16_t preset_studio_small[0x20];
static const uint32_t preset_studio_small_size;

static const uint16_t preset_studio_medium[0x20];
static const uint32_t preset_studio_medium_size;

static const uint16_t preset_studio_large[0x20];
static const uint32_t preset_studio_large_size;

static const uint16_t preset_hall[0x20];
static const uint32_t preset_hall_size;

static const uint16_t preset_half_echo[0x20];
static const uint32_t preset_half_echo_size;

static const uint16_t preset_space_echo[0x20];
static const uint32_t preset_space_echo_size;

static const uint16_t preset_chaos_echo[0x20];
static const uint32_t preset_chaos_echo_size;

static const uint16_t preset_delay[0x20];
static const uint32_t preset_delay_size;

static const uint16_t preset_reverb_off[0x20];
static const uint32_t preset_reverb_off_size;

struct PsxReverbPreset;
static void preset_load(PsxReverb *, struct PsxReverbPreset *, uint32_t);

static float avg(float a, float b) {
    return (a + b) / 2.0f;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int16_t f2s(float v) {
    return (int16_t)(clampf(v * 32768.0f, -32768.0f, 32767.0f));
}

static float s2f(int16_t v) {
    return (float)(v) / 32768.0f;
}

/**
   The `instantiate()` function is called by the host to create a new plugin
   instance.  The host passes the plugin descriptor, sample rate, and bundle
   path for plugins that need to load additional resources (e.g. waveforms).
   The features parameter contains host-provided features defined in LV2
   extensions, but this simple plugin does not use any.

   This function is in the ``instantiation'' threading class, so no other
   methods on this instance will be called concurrently with it.
*/
static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    PsxReverb* amp = (PsxReverb*)calloc(1, sizeof(PsxReverb));

    return (LV2_Handle)amp;
}

/**
   The `connect_port()` method is called by the host to connect a particular
   port to a buffer.  The plugin must store the data location, but data may not
   be accessed except in run().

   This method is in the ``audio'' threading class, and is called in the same
   context as run().
*/
static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
    PsxReverb* psx_rev = (PsxReverb*)instance;

    switch ((PortIndex)port) {
    case PSX_REV_WET:
        psx_rev->port_wet = (const float*)data;
        break;
    case PSX_REV_DRY:
        psx_rev->port_dry = (const float*)data;
        break;
    case PSX_REV_MAIN0_IN:
        psx_rev->port_main0_in = (const float*)data;
        break;
    case PSX_REV_MAIN1_IN:
        psx_rev->port_main1_in = (const float*)data;
        break;
    case PSX_REV_MAIN0_OUT:
        psx_rev->port_main0_out = (float*)data;
        break;
    case PSX_REV_MAIN1_OUT:
        psx_rev->port_main1_out = (float*)data;
        break;
    }
}

/**
   The `activate()` method is called by the host to initialise and prepare the
   plugin instance for running.  The plugin must reset all internal state
   except for buffer locations set by `connect_port()`.  Since this plugin has
   no other internal state, this method does nothing.

   This method is in the ``instantiation'' threading class, so no other
   methods on this instance will be called concurrently with it.
*/
static void
activate(LV2_Handle instance)
{
    PsxReverb* psx_rev = (PsxReverb*)instance;
    psx_rev->dry_state = 1.0f;
    psx_rev->wet_state = 1.0f;
    psx_rev->BufferAddress = 0;
    psx_rev->lp_L.state = 0.0f;
    psx_rev->lp_L.alpha = 0.707f;
    psx_rev->lp_R.state = 0.0f;
    psx_rev->lp_R.alpha = 0.707f;
    psx_rev->tmp_buf_filled = false;
    psx_rev->tmp_bufL = 0.0f;
    psx_rev->tmp_bufR = 0.0f;
    preset_load(psx_rev, (struct PsxReverbPreset *)preset_studio_large, preset_studio_large_size);
    memset(&psx_rev->spu_buffer, 0, sizeof(psx_rev->spu_buffer));
}

/** Define a macro for converting a gain in dB to a coefficient. */
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)

/* aux functions for run */
float *spu_buffer_mem(PsxReverb* psx_rev, int idx) {
    int index = (int)psx_rev->BufferAddress + idx;
    if (index >= (int)psx_rev->spu_buffer_count)
        index -= (int)psx_rev->spu_buffer_count;
    if (index < 0)
        index += (int)psx_rev->spu_buffer_count;
    return &psx_rev->spu_buffer[index];
}

float lp_process(low_pass_t *lp, float in) {
    lp->state = lp->state + lp->alpha * (in - lp->state);
    return lp->state;
}

/**
   The `run()` method is the main process function of the plugin.  It processes
   a block of audio in the audio context.  Since this plugin is
   `lv2:hardRTCapable`, `run()` must be real-time safe, so blocking (e.g. with
   a mutex) or memory allocation are not allowed.
*/
static void
run(LV2_Handle instance, uint32_t n_samples)
{
    PsxReverb* rev = (PsxReverb*)instance;

    const float wet_gain = *(rev->port_wet);
    const float wet_coef = DB_CO(wet_gain);
    const float dry_gain = *(rev->port_dry);
    const float dry_coef = DB_CO(dry_gain);

    for (uint32_t i = 0; i < n_samples; i++) {
        rev->dry_state += 0.001f * (dry_coef - rev->dry_state);
        rev->wet_state += 0.001f * (wet_coef - rev->wet_state);

        /* samples are always processed in pairs, so we buffer one first */
        if (rev->tmp_buf_filled) {
            /* load second sample and process */
#define mem(idx) (*spu_buffer_mem(rev, (idx)))
            float l1 = rev->tmp_bufL;
            float l2 = rev->port_main0_in[i];
            float r1 = rev->tmp_bufR;
            float r2 = rev->port_main1_in[i];

            float LeftInput  = avg(l1, l2);
            float RightInput = avg(r1, r2);

            float Lin = rev->vLIN * LeftInput;
            float Rin = rev->vRIN * RightInput;

            // same side reflection
            mem(rev->mLSAME) = (Lin + mem(rev->dLSAME) * rev->vWALL - mem(rev->mLSAME-1)) * rev->vIIR + mem(rev->mLSAME-1);
            mem(rev->mRSAME) = (Rin + mem(rev->dRSAME) * rev->vWALL - mem(rev->mRSAME-1)) * rev->vIIR + mem(rev->mRSAME-1);

            // different side reflection
            mem(rev->mLDIFF) = (Lin + mem(rev->dRDIFF) * rev->vWALL - mem(rev->mLDIFF-1)) * rev->vIIR + mem(rev->mLDIFF-1);
            mem(rev->mRDIFF) = (Rin + mem(rev->dLDIFF) * rev->vWALL - mem(rev->mRDIFF-1)) * rev->vIIR + mem(rev->mRDIFF-1);

            // early echo
            float Lout = rev->vCOMB1 * mem(rev->mLCOMB1) + rev->vCOMB2 * mem(rev->mLCOMB2) + rev->vCOMB3 * mem(rev->mLCOMB3) + rev->vCOMB4 * mem(rev->mLCOMB4);
            float Rout = rev->vCOMB1 * mem(rev->mRCOMB1) + rev->vCOMB2 * mem(rev->mRCOMB2) + rev->vCOMB3 * mem(rev->mRCOMB3) + rev->vCOMB4 * mem(rev->mRCOMB4);

            // late reverb APF1
            Lout -= rev->vAPF1 * mem(rev->mLAPF1-rev->dAPF1); mem(rev->mLAPF1) = Lout; Lout = Lout * rev->vAPF1 + mem(rev->mLAPF1-rev->dAPF1);
            Rout -= rev->vAPF1 * mem(rev->mRAPF1-rev->dAPF1); mem(rev->mRAPF1) = Rout; Rout = Rout * rev->vAPF1 + mem(rev->mRAPF1-rev->dAPF1);

            // late reverb APF2
            Lout -= rev->vAPF2 * mem(rev->mLAPF2-rev->dAPF2); mem(rev->mLAPF2) = Lout; Lout = Lout * rev->vAPF2 + mem(rev->mLAPF2-rev->dAPF2);
            Rout -= rev->vAPF2 * mem(rev->mRAPF2-rev->dAPF2); mem(rev->mRAPF2) = Rout; Rout = Rout * rev->vAPF2 + mem(rev->mRAPF2-rev->dAPF2);
#undef mem

            //static float dbg = 0.0f;
            //Lout = sinf(dbg) * rev->vCOMB1;
            //Rout = sinf(dbg) * rev->vCOMB2;
            //dbg += 0.1f;

            // output to mixer
            float LeftOutput  = Lout * 1.0f;
            float RightOutput = Rout * 1.0f;

            rev->BufferAddress++;
            if (rev->BufferAddress >= rev->spu_buffer_count)
                rev->BufferAddress = 0;

            rev->port_main0_out[i] = lp_process(&rev->lp_L, LeftOutput)  * rev->wet_state + l1 * rev->dry_state;
            rev->tmp_bufL          = lp_process(&rev->lp_L, LeftOutput)  * rev->wet_state + l2 * rev->dry_state;
            rev->port_main1_out[i] = lp_process(&rev->lp_R, RightOutput) * rev->wet_state + r1 * rev->dry_state;
            rev->tmp_bufR          = lp_process(&rev->lp_R, RightOutput) * rev->wet_state + r2 * rev->dry_state;
            //rev->port_main0_out[i] = LeftOutput  * rev->wet_state + l1 * rev->dry_state;
            //rev->tmp_bufL          = LeftOutput  * rev->wet_state + l2 * rev->dry_state;
            //rev->port_main1_out[i] = RightOutput * rev->wet_state + r1 * rev->dry_state;
            //rev->tmp_bufR          = RightOutput * rev->wet_state + r2 * rev->dry_state;
            rev->tmp_buf_filled = false;
        } else {
            rev->port_main0_out[i] = rev->tmp_bufL;
            rev->port_main1_out[i] = rev->tmp_bufR;
            rev->tmp_bufL = rev->port_main0_in[i];
            rev->tmp_bufR = rev->port_main1_in[i];
            rev->tmp_buf_filled = true;
        }
    }
}

/**
   The `deactivate()` method is the counterpart to `activate()`, and is called by
   the host after running the plugin.  It indicates that the host will not call
   `run()` again until another call to `activate()` and is mainly useful for more
   advanced plugins with ``live'' characteristics such as those with auxiliary
   processing threads.  As with `activate()`, this plugin has no use for this
   information so this method does nothing.

   This method is in the ``instantiation'' threading class, so no other
   methods on this instance will be called concurrently with it.
*/
static void
deactivate(LV2_Handle instance)
{
}

/**
   Destroy a plugin instance (counterpart to `instantiate()`).

   This method is in the ``instantiation'' threading class, so no other
   methods on this instance will be called concurrently with it.
*/
static void
cleanup(LV2_Handle instance)
{
    free(instance);
}

/**
   The `extension_data()` function returns any extension data supported by the
   plugin.  Note that this is not an instance method, but a function on the
   plugin descriptor.  It is usually used by plugins to implement additional
   interfaces.  This plugin does not have any extension data, so this function
   returns NULL.

   This method is in the ``discovery'' threading class, so no other functions
   or methods in this plugin library will be called concurrently with it.
*/
static const void*
extension_data(const char* uri)
{
    return NULL;
}

/**
   Every plugin must define an `LV2_Descriptor`.  It is best to define
   descriptors statically to avoid leaking memory and non-portable shared
   library constructors and destructors to clean up properly.
*/
static const LV2_Descriptor descriptor = {
    AMP_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

/**
   The `lv2_descriptor()` function is the entry point to the plugin library.  The
   host will load the library and call this function repeatedly with increasing
   indices to find all the plugins defined in the library.  The index is not an
   indentifier, the URI of the returned descriptor is used to determine the
   identify of the plugin.

   This method is in the ``discovery'' threading class, so no other functions
   or methods in this plugin library will be called concurrently with it.
*/
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0:  return &descriptor;
    default: return NULL;
    }
}

/* My own stuff. PSX standard presets used in most games can be found here */

typedef struct PsxReverbPreset {
    uint16_t dAPF1;
    uint16_t dAPF2;
    int16_t  vIIR;
    int16_t  vCOMB1;
    int16_t  vCOMB2;
    int16_t  vCOMB3;
    int16_t  vCOMB4;
    int16_t  vWALL;
    int16_t  vAPF1;
    int16_t  vAPF2;
    uint16_t mLSAME;
    uint16_t mRSAME;
    uint16_t mLCOMB1;
    uint16_t mRCOMB1;
    uint16_t mLCOMB2;
    uint16_t mRCOMB2;
    uint16_t dLSAME;
    uint16_t dRSAME;
    uint16_t mLDIFF;
    uint16_t mRDIFF;
    uint16_t mLCOMB3;
    uint16_t mRCOMB3;
    uint16_t mLCOMB4;
    uint16_t mRCOMB4;
    uint16_t dLDIFF;
    uint16_t dRDIFF;
    uint16_t mLAPF1;
    uint16_t mRAPF1;
    uint16_t mLAPF2;
    uint16_t mRAPF2;
    int16_t  vLIN;
    int16_t  vRIN;
} PsxReverbPreset;

void preset_load(PsxReverb *psx_rev, PsxReverbPreset *preset, uint32_t size) {
    psx_rev->spu_buffer_count = (size >> 1);

    psx_rev->dAPF1   = preset->dAPF1 << 2;
    psx_rev->dAPF2   = preset->dAPF2 << 2;
    psx_rev->vIIR    = s2f(preset->vIIR);
    psx_rev->vCOMB1  = s2f(preset->vCOMB1);
    psx_rev->vCOMB2  = s2f(preset->vCOMB2);
    psx_rev->vCOMB3  = s2f(preset->vCOMB3);
    psx_rev->vCOMB4  = s2f(preset->vCOMB4);
    psx_rev->vWALL   = s2f(preset->vWALL);
    psx_rev->vAPF1   = s2f(preset->vAPF1);
    psx_rev->vAPF2   = s2f(preset->vAPF2);
    psx_rev->mLSAME  = preset->mLSAME << 2;
    psx_rev->mRSAME  = preset->mRSAME << 2;
    psx_rev->mLCOMB1 = preset->mLCOMB1 << 2;
    psx_rev->mRCOMB1 = preset->mRCOMB1 << 2;
    psx_rev->mLCOMB2 = preset->mLCOMB2 << 2;
    psx_rev->mRCOMB2 = preset->mRCOMB2 << 2;
    psx_rev->dLSAME  = preset->dLSAME << 2;
    psx_rev->dRSAME  = preset->dRSAME << 2;
    psx_rev->mLDIFF  = preset->mLDIFF << 2;
    psx_rev->mRDIFF  = preset->mRDIFF << 2;
    psx_rev->mLCOMB3 = preset->mLCOMB3 << 2;
    psx_rev->mRCOMB3 = preset->mRCOMB3 << 2;
    psx_rev->mLCOMB4 = preset->mLCOMB4 << 2;
    psx_rev->mRCOMB4 = preset->mRCOMB4 << 2;
    psx_rev->dLDIFF  = preset->dLDIFF << 2;
    psx_rev->dRDIFF  = preset->dRDIFF << 2;
    psx_rev->mLAPF1  = preset->mLAPF1 << 2;
    psx_rev->mRAPF1  = preset->mRAPF1 << 2;
    psx_rev->mLAPF2  = preset->mLAPF2 << 2;
    psx_rev->mRAPF2  = preset->mRAPF2 << 2;
    psx_rev->vLIN    = s2f(preset->vLIN);
    psx_rev->vRIN    = s2f(preset->vRIN);
}

static const uint32_t preset_room_size = 0x26C0;
static const uint16_t preset_room[0x20] = {
    0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
    0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
    0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000,
};


static const uint32_t preset_studio_small_size = 0x1F40;
static const uint16_t preset_studio_small[0x20] = {
    0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
    0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
    0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
    0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000,
};


static const uint32_t preset_studio_medium_size = 0x4840;
static const uint16_t preset_studio_medium[0x20] = {
    0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
    0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
    0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
    0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000,
};


static const uint32_t preset_studio_large_size = 0x6FE0;
static const uint16_t preset_studio_large[0x20] = {
    0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
    0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
    0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
    0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000,
};


static const uint32_t preset_hall_size = 0xADE0;
static const uint16_t preset_hall[0x20] = {
    0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
    0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
    0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
    0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000,
};


static const uint32_t preset_half_echo_size = 0x3C00;
static const uint16_t preset_half_echo[0x20] = {
    0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
    0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
    0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
    0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000,
};


static const uint32_t preset_space_echo_size = 0xF6C0;
static const uint16_t preset_space_echo[0x20] = {
    0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
    0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
    0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
    0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000,
};


static const uint32_t preset_chaos_echo_size = 0x18040;
static const uint16_t preset_chaos_echo[0x20] = {
    0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
    0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
    0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000,
};


static const uint32_t preset_delay_size = 0x18040;
static const uint16_t preset_delay[0x20] = {
    0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
    0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000,
};


static const uint32_t preset_reverb_off_size = 0x10;
static const uint16_t preset_reverb_off[0x20] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
    0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
    0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000,
};
