int nxemu_ipo_cxx();
#ifdef NXEMU_PROBE_OBJCXX
int nxemu_ipo_objcxx();
#endif
#ifdef _WIN32
// Modules export explicitly; CMake cannot generate exports from /GL objects
__declspec(dllexport)
#endif
int nxemu_ipo_shared() {
#ifdef NXEMU_PROBE_OBJCXX
    if (nxemu_ipo_objcxx() != 42) return 0;
#endif
    return nxemu_ipo_cxx();
}
