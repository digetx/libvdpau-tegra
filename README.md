# About

This is a VDPAU backend driver for the NVIDIA Tegra 20 SoC's. Currently
supports CAVLC H.264 only.

# Requirements:

* libvdpau
* pixman (http://www.pixman.org)
* linux kernel driver (https://github.com/digetx/picasso_upstream_support/tree/tegra-drm-fixes-and-vde)
* libxv
* libdrm-tegra (https://github.com/grate-driver/libdrm)
* opentegra (https://github.com/grate-driver/xf86-video-opentegra)

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

Other players that support VDPAU are also (kinda) working, but mpv is recommended. The `VDPAU_DRIVER_PATH` isn't required if mesa (https://github.com/grate-driver/mesa) is installed.

# Todo:

* ~~Accelerated output to overlay~~
* Offload ~~color conversion, blitting~~ and blending to HW
* H.264 CABAC support (reverse-engineering pending)
* Support other codecs, like VC-1 or H.263
