# 测试memleak.dll的C API
import ctypes,atexit

memleak = ctypes.CDLL("../bin/memleak.dll")
memleak.setup_mem.argtypes = [ctypes.c_size_t, ctypes.c_size_t, ctypes.c_ubyte]
memleak.setup_mem.restype = None
setup_mem = memleak.setup_mem

memleak.set_leak_detect.argtypes = [ctypes.c_bool]
memleak.set_leak_detect.restype = None
set_leak_detect = memleak.set_leak_detect

memleak._override_std_malloc.argtypes = [ctypes.c_size_t]
memleak._override_std_malloc.restype = ctypes.c_void_p
malloc = memleak._override_std_malloc

memleak._override_std_calloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
memleak._override_std_calloc.restype = ctypes.c_void_p
calloc = memleak._override_std_calloc

memleak._override_std_realloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
memleak._override_std_realloc.restype = ctypes.c_void_p
realloc = memleak._override_std_realloc

memleak._override_std_free.argtypes = [ctypes.c_void_p]
memleak._override_std_free.restype = None
free = memleak._override_std_free

memleak._override_std_strdup.argtypes = [ctypes.c_char_p]
memleak._override_std_strdup.restype = ctypes.c_char_p
strdup = memleak._override_std_strdup

memleak._override_std_wcsdup.argtypes = [ctypes.c_wchar_p]
memleak._override_std_wcsdup.restype = ctypes.c_wchar_p
wcsdup = memleak._override_std_wcsdup

def main():
    setup_mem(1 << 20, 0, 0)  # 1MB，使用默认内存块数量，并初始化为0
    set_leak_detect(True)

    a = malloc(ctypes.sizeof(ctypes.c_int) * 10)
    
    if a:
        print("Memory allocated successfully.")
    else:
        print("Memory allocation failed.")

    atexit.register(_atexit)

def _atexit():
    print("\nAfter interpreter shutdown:")

if __name__ == "__main__":main()