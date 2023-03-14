REV=$(shell sh -c 'git rev-parse --short @{0}')

all: emusi.dll


emusi.dll: wgmus.c rwinmm.c wgmus.def
	mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o emusi.dll wgmus.c rwinmm.c wgmus.def -L. -lwinmm -lbass -lbasscd -lbassflac -lbassmix -lbasswasapi -D_DEBUG -static-libgcc

clean:
	rm -f emusi.dll
