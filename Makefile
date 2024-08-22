REV=$(shell sh -c 'git rev-parse --short @{0}')
all: win32.dll


win32.dll: wgmus.c rwinmm.c wgmus.def
	i686-w64-mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o win32.dll wgmus.c rwinmm.c wgmus.def -L. -lwinmm -lbass -lbassflac -lbasscd -lbassmix -lbasswasapi -D_DEBUG -static-libgcc

clean:
	rm -f win32.dll
