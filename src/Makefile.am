AUTOMAKE_OPTIONS=subdir-objects

AM_CFLAGS = -Wall -pthread -mfp16-format=ieee \
	    $(X11_CFLAGS) $(PIXMAN_CFLAGS) $(DRM_CFLAGS) $(XV_CFLAGS) \
	    $(CFLAG_VISIBILITY) $(VALGRIND_CFLAGS) $(DEFINES)

AM_CFLAGS += -I$(srcdir)/tegradrm

vdpau_tegra_libdir = $(libdir)/vdpau
vdpau_tegra_lib_LTLIBRARIES = libvdpau_tegra.la

libvdpau_tegra_la_SOURCES = vdpau_tegra.c \
                            surface_rotate.c \
                            surface_output.c \
                            surface_bitmap.c \
                            surface_video.c \
                            surface_mixer.c \
                            surface_shared.c \
                            surface.c \
                            presentation_queue.c \
                            presentation_queue_target.c \
                            decoder.c \
                            dmabuf.c \
                            bitstream.c \
                            host1x-gr2d.c \
                            host1x-pixelbuffer.c \
                            tegra_stream_v1.c \
                            tegra_stream_v2.c \
                            dri2.c \
                            shaders/prog.h \
                            $(shaders_gen) \
                            tgr_3d.xml.h \
                            shaders.h \
                            host1x-gr3d.c \
                            host1x-gr3d.h

libvdpau_tegra_la_SOURCES += tegradrm/atomic.h \
                             tegradrm/lists.h \
                             tegradrm/opentegra_drm.h \
                             tegradrm/opentegra_lib.h \
                             tegradrm/private.h \
                             tegradrm/tegra.c \
                             tegradrm/tegra_bo_cache.c \
                             tegradrm/uapi_v1/channel.c \
                             tegradrm/uapi_v1/fence.c \
                             tegradrm/uapi_v1/job.c \
                             tegradrm/uapi_v1/pushbuf.c \
                             tegradrm/uapi_v2/job.c

libvdpau_tegra_la_LDFLAGS = -version-info 1:0:0 -module -Wl,-z,defs
libvdpau_tegra_la_LIBADD  = -lm $(X11_LIBS) $(PIXMAN_LIBS) $(DRM_LIBS) $(XV_LIBS)

pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = vdpau-tegra.pc

shaders_dir := $(filter %/, $(wildcard $(srcdir)/shaders/*/))
shaders_gen := $(addsuffix .bin.h, $(shaders_dir:%/=%))

%.bin.h: gen_shader_bin \
			%/vertex.asm \
			%/linker.asm \
			%/fragment.asm
	$(builddir)/gen_shader_bin \
		--vs $*/vertex.asm \
		--lnk $*/linker.asm \
		--fs $*/fragment.asm \
		--name $(*F) \
		--out $@

asm_grammars := $(wildcard $(srcdir)/asm/*.y)
asm_headers  := $(wildcard $(srcdir)/asm/*.h)
asm_lexers   := $(wildcard $(srcdir)/asm/*.l)

asm_gen_c := \
	$(asm_lexers:%.l=%.lex.c) \
	$(asm_grammars:%.y=%.tab.c)

asm_gen_h := \
	$(asm_grammars:%.y=%.tab.h)

%.lex.c: $(asm_lexers)
	$(LEX) -P $(*F) --nounput -o $@ $*.l

%.tab.c %.tab.h: $(asm_grammars)
	$(YACC) -p $(*F) -b $(*F) -d --debug -o $@ $*.y

HOSTCC = gcc

gen_shader_bin: gen_shader_bin.c $(asm_gen_c) $(asm_headers)
	$(HOSTCC) -I $(srcdir)/asm -o $(builddir)/$@ $< $(asm_gen_c)

BUILT_SOURCES = \
	$(asm_gen_c) \
	$(asm_gen_h) \
	$(shaders_gen)

CLEANFILES = \
	$(asm_gen_c) \
	$(asm_gen_h) \
	$(shaders_gen) \
	$(builddir)/gen_shader_bin
