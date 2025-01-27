@echo off
rem C++ static
call g++32 -c memleak.cpp -o bin/32-bit/memleak_static.o -O2 -Wall -Iinclude
rem C DLL
call g++32 memleak.cpp -o bin/32-bit/memleak.dll -s -O2 -Wall -shared -DMEMLEAK_DLL_EXPORT -Wl,--out-implib,lib/32-bit/libmemleak.a -Iinclude

rem C static test
call gcc32 -c memleak_ctest.c -o bin/32-bit/memleak_ctest.o -O2 -Wall -Iinclude
call g++32 bin/32-bit/memleak_ctest.o bin/32-bit/memleak_static.o -o bin/32-bit/memleak_ctest_static -s -Wall -Iinclude
rem C test with DLL
call gcc32 memleak_ctest.c -o bin/32-bit/memleak_ctest -s -Wall -Iinclude -Llib/32-bit -lmemleak
rem C++ static test
call g++32 memleak_test.cpp bin/32-bit/memleak_static.o -o bin/32-bit/memleak_test -s -O2 -Wall -Iinclude
call g++32 memmgr_test.cpp bin/32-bit/memleak_static.o -o bin/32-bit/memmgr_test -s -O2 -Wall -Iinclude