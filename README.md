# FFoveated :movie_camera:

A workbench for developing and researching foveated video codecs and applications.

## Foveal Vision :eyes:

The acuity of the human eye rapidly decreases outside of a circular region that
covers roughly 2.5° of visual angle around the current center of fixation. We
are limited to perceive imagery in the periphery at a significantly reduced
resolution due to the density of photoreceptive cells on the retina. A concise
introduction to the biological background can be found in
[Foundations of Vision](https://foundationsofvision.stanford.edu/chapter-3-the-photoreceptor-mosaic/)
by Brian A. Wandell, Vision Imaging Science and Technology Lab, Stanford.

## Example
Click on the image below for a short 1080p demo clip (~1.5M) that was
compressed using [x264](https://www.videolan.org/developers/x264.html):  
  
[<img src="https://oliver-wiedemann.net/static/external/github/ffoveated/video-foveated-preview.jpg">](https://oliver-wiedemann.net/static/external/github/ffoveated/video-foveated.mp4?raw=true)
  
The fixation point is set to be at the center of each frame, thus the visual
quality decreases towards the periphery. The corners are already **quite
severely degraded** and e.g. block flicker effects are clearly visible. **This
is intentional** to showcase the effects of foveated encoding.  In practice,
one would ideally use feedback from an eye-tracker to steer the compression
according to an observer's gaze in real time, which ideally renders these
impairments invisible.

## Implementation

The graphic below displays a conceptual schematic of FFoveated.
![FFoveated Schematic](https://oliver-wiedemann.net/static/external/github/ffoveated/schematic.png)

Central part is the "foveated encoder", which takes input either directly from
some sort of live source, e.g. a camera or the display buffer of some render
engine, or as a raw frame sequence from a previously decoded file. The latter
is useful to repeatedly evaluate this approach with different codec
parameterizations.


## Building :hammer:
Building FFoveated is *slightly* inconvenient - enhancing this process is on the
todo list. However, the two main components of this project are found in the
`ffmpeg-foveated/` and `src/` directories.

### Dependencies

The project is pretty tame dependency wise should build on most
Linux installations right out of the box, you'll just need

- libsdl2


#### SMI Eye Tracking
To be able to use the provided eye-tracking implementation you need
the iViewX SDK, which was originally distributed with scientific
eye-tracking hardware by Sensomotoric Instruments, now known as
[Gaze Intelligence](https://gazeintelligence.com/smi-software-download).
I'm unable to share their their libraries and it wouldn't be much use
to you anyways without one of their devices.  

Alternatives should be easy enough to integrate, as long as you can
determine fixation point positions in terms of pixels on the screen
you're good to go.


### ffmpeg-foveated
The subtree in `lib/ffmpeg-foveated` contains a patched fork of
[FFmpeg](https://ffmpeg.org/), which you can configure and compile its contents
as follows.  If you already have FFmpeg installed on your system you can save
time by disabling more compilation targets, however, this might lead to
compatibility issues in the future.

```bash
cd lib/ffmpeg-foveated/
./configure --enable-avcodec --enable-gpl --enable-libx264 --enable-shared
make
```

### FFoveated

Building `FFoveated` itself is very straight forward. Just call `make` in `src/`.  

Make sure to **point your linker to these patched FFmpeg libraries**,
especially `libavcodec`.  
Setting the `LD_PRELOAD` environment variable is a
temporary way to override loading symbols from the system libraries:

```bash
export LD_PRELOAD="/path/to/FFoveated/ffmpeg-foveated/libavcodec/libavcodec.so"
```

## Application Scenarios and Limitations
This is a rather niche project that aims to optimize high quality, low
bandwidth video streaming applications with a single observer.
Prominent use cases are:

- cloud gaming, where prerendered content is served via a network
- controlling of remote controlled vehicles/drones

There are better methods available for broadcasts and on-demand streaming.
Even videotelephony probably allows for enough buffering to employ codecs
relying on more a thorough content analysis, which has undeniable benefits
over requiring a feedback-loop with an eye-tracker.

## Publications :scroll:

This work was used, mentioned or utilized in the following papers:

- O. Wiedemann, V. Hosu, H. Lin and D. Saupe, "Foveated Video Coding for Real Time Streaming Applications",  
  12th International Conference on Quality of Multimedia Experience (QoMEX), IEEE, 2020. [PDF](https://oliver-wiedemann.net/static/publications/wiedemann2020foveated.pdf)
- O. Wiedemann and D. Saupe, "Gaze Data for Quality Assessment of Foveated Video",  
  Workshop on Eye Tracking for Quality of Experience in Multimedia (ET-MM) at ACM ETRA, Stuttgart, 2020. [PDF](https://oliver-wiedemann.net/static/publications/wiedemann2020gaze.pdf)

## Contact, Contributing and Collaboration :email:.

If you're interested in this project or related work have a look at my [personal](https://oliver-wiedemann.net) and
[group](https://www.mmsp.uni-konstanz.de/research/projects/visual-quality-assessment/) websites or drop me an [email](mailto:mail@oliver-wiedemann.net).

