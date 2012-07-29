
# settings
use_musashi = 1
use_fame = 1
#use_mz80 = 1

# profile = 1


DEFINC = -I../.. -I. -D__GP2X__ -DA320 -D_UNZIP_SUPPORT \
	 #-Dmemcpy=memcpy_mips -Dmemset=memset_mips # -DBENCHMARK

CROSS = /opt/mipsel-linux-uclibc/usr/bin/

GCC = $(CROSS)mipsel-linux-gcc
STRIP = $(CROSS)mipsel-linux-strip
AS = $(CROSS)mipsel-linux-gcc

ifeq "$(profile)" "1"
COPT_COMMON = -s -O3 -ftracer -fstrength-reduce -Wall -funroll-loops -fomit-frame-pointer -fstrict-aliasing -ffast-math -fprofile-generate
COPT = $(COPT_COMMON)
else
COPT = -O3 -static -fsigned-char -msoft-float -funroll-loops \
   	-ffast-math -fomit-frame-pointer -fno-strength-reduce \
	-finline-functions -G0 -march=mips32 -mtune=r4600 -mno-mips16

COPT_COMMON = $(COPT)
endif

# gtk
COPT    += 
LDFLAGS += 
COPT    += 
LDFLAGS += -lao -lm -lpng

# frontend
OBJS += platform/gp2x/main.o platform/gp2x/menu.o platform/gp2x/emu.o platform/gp2x/usbjoy.o \
		blit.o gp2x.o log_io.o

# common
OBJS += platform/common/emu.o platform/common/menu.o platform/common/fonts.o \
		platform/common/readpng.o platform/common/mp3_helix.o

# Pico
OBJS += Pico/Area.o Pico/Cart.o Pico/Memory.o Pico/Misc.o Pico/Pico.o Pico/Sek.o \
		Pico/VideoPort.o Pico/Draw2.o Pico/Draw.o Pico/Patch.o
# Pico - CD
OBJS += Pico/cd/Pico.o Pico/cd/Memory.o Pico/cd/Sek.o Pico/cd/LC89510.o \
		Pico/cd/cd_sys.o Pico/cd/cd_file.o Pico/cd/gfx_cd.o \
		Pico/cd/Area.o Pico/cd/Misc.o Pico/cd/pcm.o Pico/cd/buffering.o
# Pico - sound
OBJS += Pico/sound/sound.o Pico/sound/sn76496.o Pico/sound/ym2612.o Pico/sound/mix.o
# zlib
OBJS += zlib/gzio.o zlib/inffast.o zlib/inflate.o zlib/inftrees.o zlib/trees.o \
	zlib/deflate.o zlib/crc32.o zlib/adler32.o zlib/zutil.o zlib/compress.o zlib/uncompr.o
# unzip
OBJS += unzip/unzip.o unzip/unzip_stream.o
# CPU cores
ifeq "$(use_musashi)" "1"
DEFINC += -DEMU_M68K
OBJS += cpu/musashi/m68kops.o cpu/musashi/m68kcpu.o
endif
ifeq "$(use_fame)" "1"
DEFINC += -DEMU_F68K
OBJS += cpu/fame/famec.o
endif
# z80
ifeq "$(use_mz80)" "1"
DEFINC += -D_USE_MZ80
OBJS += cpu/mz80/mz80.o
else
DEFINC += -D_USE_CZ80
OBJS += cpu/cz80/cz80.o
endif
# misc
ifeq "$(use_fame)" "1"
ifeq "$(use_musashi)" "1"
OBJS += Pico/Debug.o
OBJS += cpu/musashi/m68kdasm.o
endif
endif

vpath %.c = ../..
DIRS = platform platform/gp2x platform/common Pico Pico/cd Pico/sound zlib unzip \
	cpu cpu/musashi cpu/fame cpu/mz80 cpu/cz80

all: mkdirs PicoDrive
clean: tidy
	@$(RM) PicoDrive
tidy:
	$(RM) $(OBJS)
	rm -rf $(DIRS)
	@make -C ../../cpu/mz80/ clean
	@make -C ../common/helix/ clean

PicoDrive : $(OBJS) 
	@echo ">>>" $@
	$(GCC) $(COPT) $^ $(LDFLAGS) -lao -lm -lpng -Wl,-Map=PicoDrive.map -o $@ #mips/memcpy-mips.o mips/memset-mips.o
	$(STRIP) $@ -o $@.dge

mkdirs:
	mkdir -p $(DIRS)

../../cpu/musashi/m68kops.c :
	@make -C ../../cpu/musashi

.c.o:
	@echo ">>>" $<
	$(GCC) $(COPT) $(DEFINC) -c $< -o $@
.s.o:
	@echo ">>>" $<
	$(AS) -march=allegrex -mtune=allegrex $< -o $@

Pico/sound/ym2612.o : ../../Pico/sound/ym2612.c
	@echo ">>>" $@
	$(GCC) $(COPT_COMMON) $(DEFINC) -c $< -o $@

cpu/fame/famec.o : ../../cpu/fame/famec.c ../../cpu/fame/famec_opcodes.h
	@echo ">>>" $<
	$(GCC) -O3 -static -fsigned-char -msoft-float \
   	-ffast-math -fomit-frame-pointer -fno-strength-reduce \
	-finline-functions -G0 -march=mips32 -mtune=r4600 -mno-mips16  $(DEFINC) -Wno-unused -c $< -o $@


