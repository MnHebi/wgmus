REV=$(shell sh -c 'git rev-parse --short @{0}')

all: mmusi.dll


mmusi.dll: mmusi.c rwinmm.c mmusi.def
	mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o mmusi.dll mmusi.c rwinmm.c mmusi.def -L. -lwinmm -D_DEBUG -static-libgcc

clean:
	rm -f mmusi.dll
