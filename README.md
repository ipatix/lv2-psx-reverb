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

## How to compile

Install your distribution's `lv2-dev` package, init all submodules with `git submodule update --init` and run `./build.sh`.
This will automatically install the plugin to your home directory `~/.lv2` where most hosts will be able to find it.
If you want to install it to somewhere else, change the `build.sh` script.
