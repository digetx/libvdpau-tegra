AM_CFLAGS = -Wall $(X11_CFLAGS) $(PIXMAN_CFLAGS) $(DRM_CFLAGS) $(CFLAG_VISIBILITY)

vdpau_tegra_libdir = $(libdir)/vdpau
vdpau_tegra_lib_LTLIBRARIES = libvdpau_tegra.la

libvdpau_tegra_la_SOURCES = vdpau_tegra.c \
                            surface_output.c \
                            surface_bitmap.c \
                            surface_video.c \
                            surface_mixer.c \
                            surface.c \
                            presentation_queue.c \
                            decoder.c \
                            dmabuf.c \
                            bitstream.c

libvdpau_tegra_la_LDFLAGS = -version-info 1:0:0 -Wl,-z,defs
libvdpau_tegra_la_LIBADD  = -lm $(X11_LIBS) $(PIXMAN_LIBS) $(DRM_LIBS)
