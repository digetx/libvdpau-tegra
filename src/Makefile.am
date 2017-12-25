AM_CFLAGS = -Wall -pthread \
	    $(X11_CFLAGS) $(PIXMAN_CFLAGS) $(DRM_CFLAGS) $(XV_CFLAGS) \
	    $(CFLAG_VISIBILITY)

vdpau_tegra_libdir = $(libdir)/vdpau
vdpau_tegra_lib_LTLIBRARIES = libvdpau_tegra.la

libvdpau_tegra_la_SOURCES = vdpau_tegra.c \
                            surface_output.c \
                            surface_bitmap.c \
                            surface_video.c \
                            surface_mixer.c \
                            surface_shared.c \
                            surface.c \
                            presentation_queue.c \
                            decoder.c \
                            dmabuf.c \
                            bitstream.c \
                            host1x-gr2d.c \
                            host1x-pixelbuffer.c \
                            tegra_stream.c \
                            dri2.c

libvdpau_tegra_la_LDFLAGS = -version-info 1:0:0 -module -Wl,-z,defs
libvdpau_tegra_la_LIBADD  = -lm $(X11_LIBS) $(PIXMAN_LIBS) $(DRM_LIBS) $(XV_LIBS)

pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = vdpau-tegra.pc
