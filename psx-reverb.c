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
#include "lv2/core/lv2_util.h"
#include "lv2/log/log.h"
#include "lv2/log/logger.h"

/** Include standard C headers */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/**
   The URI is the identifier for a plugin, and how the host associates this
   implementation in code with its description in data.  In this plugin it is
   only used once in the code, but defining the plugin URI at the top of the
   file is a good convention to follow.  If this URI does not match that used
   in the data files, the host will fail to load the plugin.
*/
#define PSX_REV_URI "http://github.com/ipatix/lv2-psx-reverb"

/**
   In code, ports are referred to by index.  An enumeration of port indices
   should be defined for readability.
*/
typedef enum {
    PSX_REV_WET = 0,
    PSX_REV_DRY = 1,
    PSX_REV_PRESET = 2,
    PSX_REV_MASTER = 3,
    PSX_REV_MAIN0_IN = 4,
    PSX_REV_MAIN1_IN = 5,
    PSX_REV_MAIN0_OUT = 6,
    PSX_REV_MAIN1_OUT = 7,
} PortIndex;

/**
   Every plugin defines a private structure for the plugin instance.  All data
   associated with a plugin instance is stored here, and is available to
   every instance method.  In this simple plugin, only port buffers need to be
   stored, since there is no additional instance data.
*/

#define NUM_PRESETS 10
#define SPU_REV_RATE 22050
#define SPU_REV_PRESET_LONGEST_COUNT (0x18040 / 2)

typedef struct {
    // lv2 stuff
    LV2_URID_Map*  map;     // URID map feature
    LV2_Log_Logger logger;  // Logger API      

    // Port buffers
    const float* port_wet;
    const float* port_dry;
    const float* port_preset;   // <-- this is technically an int
    const float* port_master;
    const float* port_main0_in;
    const float* port_main1_in;
    float*       port_main0_out;
    float*       port_main1_out;

    // processing state data
    float        master;
    float        wet;
    int          preset;
    float        dry;

    float       *spu_buffer;
    size_t       spu_buffer_count;
    size_t       spu_buffer_count_mask;

    uint32_t     BufferAddress;

    /* misc things */
    float        rate;

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

static const uint16_t presets[10][0x20];

static void preset_load(PsxReverb *, int); 

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

/* convert iir filter constant to center frequency */
static float alpha2fc(float alpha, float samplerate) {
    const double dt = 1.0 / samplerate;
    const double fc_inv = 2.0 * M_PI * (dt / alpha  - dt);
    return (float)(1.0 / fc_inv);
}

/* convert center frequency to iir filter constant */
static float fc2alpha(float fc, float samplerate) {
    const double dt = 1.0 / samplerate;
    const double rc = 1.0 / (2.0 * M_PI * fc);
    return (float)(dt / (rc + dt));
}

static uint32_t ceilpower2(uint32_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
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
    PsxReverb* psxrev = (PsxReverb*)calloc(1, sizeof(PsxReverb));

    /* init logging */
    const char* missing = lv2_features_query(
        features,
        LV2_LOG__log,  &psxrev->logger.log, false,
        LV2_URID__map, &psxrev->map, true,
        NULL);
    lv2_log_logger_set_map(&psxrev->logger, psxrev->map);

    if (missing) {
        lv2_log_error(&psxrev->logger, "Missing feature <%s>\n", missing);
        free(psxrev);
        return NULL;
    }

    /* low samplerates are not supported */
    if (rate <= 1.0) {
        lv2_log_error(&psxrev->logger, "Samplerate is too low: %f\n", rate);
        free(psxrev);
        return NULL;
    }

    psxrev->rate = (float)rate;

    /* alloc reverb buffer */
    psxrev->spu_buffer_count = ceilpower2((uint32_t)ceil(SPU_REV_PRESET_LONGEST_COUNT * (rate / SPU_REV_RATE)));
    psxrev->spu_buffer_count_mask = psxrev->spu_buffer_count - 1; // <-- we can use this for quick circular buffer access
    psxrev->spu_buffer = calloc(psxrev->spu_buffer_count, sizeof(float));
    if (psxrev->spu_buffer == NULL) {
        lv2_log_error(&psxrev->logger, "Could not allocate SPU buffer\n");
        free(psxrev);
        return NULL;
    }

    return (LV2_Handle)psxrev;
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
    case PSX_REV_PRESET:
        psx_rev->port_preset = (const float*)data;
        break;
    case PSX_REV_MASTER:
        psx_rev->port_master = (const float*)data;
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
    psx_rev->dry = 1.0f;
    psx_rev->wet = 1.0f;
    psx_rev->preset = 0;
    preset_load(psx_rev, psx_rev->preset);
    psx_rev->master = 1.0f;
    psx_rev->BufferAddress = 0;
    memset(psx_rev->spu_buffer, 0, psx_rev->spu_buffer_count * sizeof(psx_rev->spu_buffer[0]));
}

/** Define a macro for converting a gain in dB to a coefficient. */
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)

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

    /* load preset if it was changed */
    int preset = (int)*rev->port_preset;
    if (preset != rev->preset)
        preset_load(rev, preset);

    const float wet_gain = *(rev->port_wet);
    const float wet_coef = DB_CO(wet_gain);
    const float dry_gain = *(rev->port_dry);
    const float dry_coef = DB_CO(dry_gain);
    const float master_gain = *(rev->port_master);
    const float master_coef = DB_CO(master_gain);

    for (uint32_t i = 0; i < n_samples; i++) {
        rev->dry += 0.001f * (dry_coef - rev->dry);
        rev->wet += 0.001f * (wet_coef - rev->wet);
        rev->master += 0.001f * (master_coef - rev->master);

#define mem(idx) (rev->spu_buffer[(unsigned)((idx) + rev->BufferAddress) & rev->spu_buffer_count_mask])
        const float LeftInput  = rev->port_main0_in[i];
        const float RightInput = rev->port_main1_in[i];

        const float Lin = rev->vLIN * LeftInput;
        const float Rin = rev->vRIN * RightInput;

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

        // output to mixer
        const float LeftOutput  = Lout;
        const float RightOutput = Rout;

        rev->BufferAddress = ((rev->BufferAddress + 1) & rev->spu_buffer_count_mask);

        rev->port_main0_out[i] = (LeftOutput  * rev->wet + Lin * rev->dry) * rev->master;
        rev->port_main1_out[i] = (RightOutput * rev->wet + Rin * rev->dry) * rev->master;
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
    PsxReverb* rev = (PsxReverb*)instance;

    free(rev->spu_buffer);
    free(rev);
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
    PSX_REV_URI,
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

void preset_load(PsxReverb *psx_rev, int preset_index) {
    // even if loading preset fails, set the ID regardless so we don't get log spam
    psx_rev->preset = preset_index;

    if (preset_index >= NUM_PRESETS) {
        lv2_log_error(&psx_rev->logger, "Invalid Preset: %d\n", preset_index);
        return;
    }

    float stretch_factor = psx_rev->rate / SPU_REV_RATE;

    PsxReverbPreset *preset = (PsxReverbPreset *)&presets[psx_rev->preset];

    psx_rev->dAPF1   = (uint32_t)((preset->dAPF1 << 2) * stretch_factor);
    psx_rev->dAPF2   = (uint32_t)((preset->dAPF2 << 2) * stretch_factor);
    // correct 22050 Hz IIR alpha to our actual rate
    psx_rev->vIIR    = fc2alpha(alpha2fc(s2f(preset->vIIR), SPU_REV_RATE), psx_rev->rate);
    psx_rev->vCOMB1  = s2f(preset->vCOMB1);
    psx_rev->vCOMB2  = s2f(preset->vCOMB2);
    psx_rev->vCOMB3  = s2f(preset->vCOMB3);
    psx_rev->vCOMB4  = s2f(preset->vCOMB4);
    psx_rev->vWALL   = s2f(preset->vWALL);
    psx_rev->vAPF1   = s2f(preset->vAPF1);
    psx_rev->vAPF2   = s2f(preset->vAPF2);
    psx_rev->mLSAME  = (uint32_t)((preset->mLSAME << 2) * stretch_factor);
    psx_rev->mRSAME  = (uint32_t)((preset->mRSAME << 2) * stretch_factor);
    psx_rev->mLCOMB1 = (uint32_t)((preset->mLCOMB1 << 2) * stretch_factor);
    psx_rev->mRCOMB1 = (uint32_t)((preset->mRCOMB1 << 2) * stretch_factor);
    psx_rev->mLCOMB2 = (uint32_t)((preset->mLCOMB2 << 2) * stretch_factor);
    psx_rev->mRCOMB2 = (uint32_t)((preset->mRCOMB2 << 2) * stretch_factor);
    psx_rev->dLSAME  = (uint32_t)((preset->dLSAME << 2) * stretch_factor);
    psx_rev->dRSAME  = (uint32_t)((preset->dRSAME << 2) * stretch_factor);
    psx_rev->mLDIFF  = (uint32_t)((preset->mLDIFF << 2) * stretch_factor);
    psx_rev->mRDIFF  = (uint32_t)((preset->mRDIFF << 2) * stretch_factor);
    psx_rev->mLCOMB3 = (uint32_t)((preset->mLCOMB3 << 2) * stretch_factor);
    psx_rev->mRCOMB3 = (uint32_t)((preset->mRCOMB3 << 2) * stretch_factor);
    psx_rev->mLCOMB4 = (uint32_t)((preset->mLCOMB4 << 2) * stretch_factor);
    psx_rev->mRCOMB4 = (uint32_t)((preset->mRCOMB4 << 2) * stretch_factor);
    psx_rev->dLDIFF  = (uint32_t)((preset->dLDIFF << 2) * stretch_factor);
    psx_rev->dRDIFF  = (uint32_t)((preset->dRDIFF << 2) * stretch_factor);
    psx_rev->mLAPF1  = (uint32_t)((preset->mLAPF1 << 2) * stretch_factor);
    psx_rev->mRAPF1  = (uint32_t)((preset->mRAPF1 << 2) * stretch_factor);
    psx_rev->mLAPF2  = (uint32_t)((preset->mLAPF2 << 2) * stretch_factor);
    psx_rev->mRAPF2  = (uint32_t)((preset->mRAPF2 << 2) * stretch_factor);
    psx_rev->vLIN    = s2f(preset->vLIN);
    psx_rev->vRIN    = s2f(preset->vRIN);

    memset(psx_rev->spu_buffer, 0, psx_rev->spu_buffer_count * sizeof(psx_rev->spu_buffer[0]));
}

static const uint16_t presets[NUM_PRESETS][0x20] = {
    {
        /* Name: Room, SPU mem required: 0x26C0 */
        0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
        0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
        0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000,
    },
    {
        /* Name: Studio Small, SPU mem required: 0x1F40 */
        0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
        0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
        0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
        0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000,
    },
    {
        /* Name: Studio Medium, SPU mem required: 0x4840 */
        0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
        0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
        0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
        0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000,
    },
    {
        /* Name: Studio Large, SPU mem required: 0x6FE0*/
        0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
        0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
        0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
        0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000,
    },
    {
        /* Name: Hall, SPU mem required: 0xADE0 */
        0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
        0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
        0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
        0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000,
    },
    {
        /* Name: Half Echo, SPU mem required: 0x3C00 */
        0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
        0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
        0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
        0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000,
    },
    {
        /* Name: Space Echo, SPU mem required: 0xF6C0 */
        0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
        0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
        0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
        0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000,
    },
    {
        /* Name: Chaos Echo, SPU mem required: 0x18040 */
        0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
        0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
        0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000,
    },
    {
        /* Name: Delay, SPU mem required: 0x18040 */
        0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
        0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000,
    },
    {
        /* Name: Off, SPU mem required: 0x10 */
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
        0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000,
    },
};
