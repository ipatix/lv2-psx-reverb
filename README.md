# PSX Reverb (LV2 Plugin)

## Overview

PSX Reverb is a small audio plugin for LV2 hosts (I like to use Carla). It is intended to emulate the PlayStation SPU's reverb algorithm.

![psxreverb](https://user-images.githubusercontent.com/8502545/107978482-2084ac80-6fbd-11eb-9ff9-16f2c6a050a7.png)

It now works for all samplerates and compensates for different buffer sizes and coefficient changes accordingly.
10 different presets are available which are the ones you'll find in most commercial games.
Games will likely use one of the available algorithms but usually they vary the wet level, so you may have to tweak around with that.

In order to find out which preset a game uses you can use NO$PSX and check the contents of the reverb registers.
```
http://www.problemkaputt.de/psx-spx.htm#spureverbregisters
```
Compare the register values to the presets here:
```
http://www.problemkaputt.de/psx-spx.htm#spureverbexamples
```
Games usually only adjust `vLOUT` and `vROUT` (usually equal) which you can convert to logarithmic units in order to tweak the wet level of this plugin.

If you want to add new presets, you'll have to do so in the code.
Exposing the reverb registers directly as control ports would make the plugin almost unusable since you have to deeply understand the algorithm in order to make the sound not go bad.

This plugin was originally based on the "Simple Amplifier" example plugin code.

## Examples

This is a demo with a Final Fantasy XII sequence. It's not a PSX game but it still sounds reasonable:

**Large Studio @ -5 dB wet**: https://user-images.githubusercontent.com/8502545/107985750-f9cd7280-6fca-11eb-869c-4189c44ba8c2.mp4
**Clean**: https://user-images.githubusercontent.com/8502545/107985125-baeaed00-6fc9-11eb-8d6c-42a298a2ce17.mp4

The next one is from Final Fantasy IX and tries to mimic the original reverb settings as close as possible.
It sounds somewhat different because unlike the real console, this code does downsample the reverb to 22050 Hz.
But other than the additional brightness of the higher frequencies it sounds almost spot on to the original:

**Large Studio @ -9.3 dB wet**: https://user-images.githubusercontent.com/8502545/112371228-a8f21d80-8cde-11eb-8ef5-dca29fb85383.mp4
**Clean**: https://user-images.githubusercontent.com/8502545/112371107-86f89b00-8cde-11eb-9b97-105547b498dd.mp4

For now I don't have examples of the other presets recorded, but the Large Studio is no doubt my favorite presets, followed by Hall.

## How to compile

Install your distribution's `lv2-dev` package, init all submodules with `git submodule update --init` and run `./build.sh`.
This will automatically install the plugin to your home directory `~/.lv2` where most hosts will be able to find it.
If you want to install it to somewhere else, change the `build.sh` script.
