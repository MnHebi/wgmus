REV=$(shell sh -c 'git rev-parse --short @{0}')

all: wgmus.dll


wgmus.dll: wgmus.c rwinmm.c wgmus.def
	i686-w64-mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o wgmus.dll wgmus.c rwinmm.c wgmus.def -L. -lwinmm -lbass -lbasscd -lbassflac -lbassmix -lbasswasapi -D_DEBUG -static-libgcc

clean:
	rm -f wgmus.dll
