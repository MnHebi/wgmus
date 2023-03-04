REV=$(shell sh -c 'git rev-parse --short @{0}')

all: mmusi.dll


mmusi.dll: wgmus.c rwinmm.c wgmus.def
	mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o mmusi.dll wgmus.c rwinmm.c wgmus.def -L. -lwinmm -lbass -lbasscd -lbassmix -lbasswasapi -D_DEBUG -static-libgcc

clean:
	rm -f mmusi.dll
