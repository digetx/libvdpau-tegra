# About

This is a VDPAU backend driver for the NVIDIA Tegra 20 SoC's. Currently
supports CAVLC H.264 only. Output rendering is a bit slow due to software-only
surface processing.

# Requirements:

* libvdpau
* pixman (http://www.pixman.org)
* linux kernel driver (https://github.com/digetx/picasso_upstream_support/tree/4.7-tegra-vde)

# Installation:
```
$ sh autogen.sh
$ make
$ make install
```

# Usage example:

```
$ VDPAU_DRIVER=tegra VDPAU_DRIVER_PATH=/path/to/libvdpau-tegra mpv --hwdec=vdpau --vo=vdpau video.mp4
```

Video players that support internal VDPAU decode -> X11 output, like VLC, would
yield better performance.

# Todo:

* Accelerated output to overlay
* Offload color conversion, blitting and blending to HW
* H.264 CABAC support (reverse-engineering pending)
* Support other codecs, like VC-1 or H.263
