REV=$(shell sh -c 'git rev-parse --short @{0}')

all: mmusi.dll


mmusi.dll: mmusi.c mmusi.def stubs.c
	mingw32-gcc -std=gnu99 -Wl,--enable-stdcall-fixup -Ilibs/include -O2 -shared -s -o mmusi.dll mmusi.c stubs.c mmusi.def -L. -lvorbisfile-3  -lwinmm -lbass -D_DEBUG -static-libgcc

clean:
	rm -f mmusi.dll
