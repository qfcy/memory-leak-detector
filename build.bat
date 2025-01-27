@echo off
rem C++ static
g++ -c memleak.cpp -o bin/memleak_static.o -O2 -Wall -Iinclude
rem C DLL
g++ memleak.cpp -o bin/memleak.dll -s -O2 -Wall -shared -DMEMLEAK_DLL_EXPORT -Wl,--out-implib,lib/libmemleak.a -Iinclude

rem C static test
gcc -c memleak_ctest.c -o bin/memleak_ctest.o -O2 -Wall -Iinclude
g++ bin/memleak_ctest.o bin/memleak_static.o -o bin/memleak_ctest_static -s -Wall -Iinclude
rem C test with DLL
gcc memleak_ctest.c -o bin/memleak_ctest -s -Wall -Iinclude -Llib -lmemleak
rem C++ static test
g++ memleak_test.cpp bin/memleak_static.o -o bin/memleak_test -s -O2 -Wall -Iinclude
g++ memmgr_test.cpp bin/memleak_static.o -o bin/memmgr_test -s -O2 -Wall -Iinclude