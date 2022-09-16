REV=$(shell sh -c 'git rev-parse --short @{0}')

all: wgmusi.dll


wgmusi.dll: wgmus.c rwinmm.c wgmus.def
	mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o wgmusi.dll wgmus.c rwinmm.c wgmus.def -L. -lwinmm -lbass -lbasscd -D_DEBUG -static-libgcc

clean:
	rm -f wgmus.dll
