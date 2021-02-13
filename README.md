# PSX Reverb (LV2 Plugin)

## Overview

PSX Reverb is a small audio plugin for lv2 Hosts (I mainly use Carla). It is intended to emulate the PlayStation SPU's reverb algorithm.

At the moment it only works correctly at 44100 Hz (48000 Hz still doesn't sound too much off) and only supports the "large studio" preset, which is used in quite a few games including "Star Ocean 2" and "Final Fantasy IX".
In order to get game like sound, make sure the wet level is set correctly. Various games use different wet levels for different songs.

I'm rather new to LV2, so until I figure out how to make a plugin with selectable presets, a single one has to be sufficient.

If you're excited to try other presets, change the following line in `psx-reverb.c`:

```
preset_load(psx_rev, (struct PsxReverbPreset *)preset_studio_large, preset_studio_large_size);
```

This plugin was originally based on the "Simple Amplifier" example plugin code.

## How to compile

Install your distribution's `lv2-dev` package, init all submodules with `git submodule update --init` and run `./build.sh`.
