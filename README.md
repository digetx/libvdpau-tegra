# About

This is a VDPAU backend driver for the NVIDIA Tegra20 SoC's. Currently
supports CAVLC H.264 only.

# Requirements:

* libvdpau
* pixman (http://www.pixman.org)
* linux kernel Video Decoder Engine (VDE) driver (https://github.com/digetx/picasso_upstream_support/tree/tegra-drm-fixes-and-vde)
* libX11 libXext xextproto libXfixes libXv
* libdrm-tegra (https://github.com/grate-driver/libdrm)
* opentegra (https://github.com/grate-driver/xf86-video-opentegra)

The VDE linux kernel driver is optional, it is required for HW accelerated video decoding. The accelerated video output works without it.

# Installation:
```
$ sh autogen.sh
$ make
$ make install
```

# Usage example:

```
$ VDPAU_DRIVER=tegra VDPAU_DRIVER_PATH=/path/to/libvdpau_tegra.so mpv --hwdec=vdpau --vo=vdpau video.mp4
```

You should see the following lines in the terminal:
```
Using hardware decoding (vdpau).
VO: [vdpau] 1280x720 vdpau[yuv420p]
```
If you don't see anything related to VDPAU, it means that it doesn't work for some reason. Check that `/usr/lib/vdpau/libvdpau_tegra.so.1` exists, note that `VDPAU_DRIVER_PATH` must point to the directory containing the shared library, not the library file.

Other players that support VDPAU are also (kinda) working, but mpv is recommended. The `VDPAU_DRIVER` and `VDPAU_DRIVER_PATH` aren't required if mesa (https://github.com/grate-driver/mesa) is installed.

# Todo:

* ~~Accelerated output to overlay~~
* Offload ~~color conversion, blitting~~ and blending to HW
* H.264 CABAC support (reverse-engineering pending)
* Support other codecs, like VC-1 or H.263
