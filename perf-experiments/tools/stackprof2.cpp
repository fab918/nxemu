// Minimal cross-process stack sampler: stackprof <pid> <seconds> <symbol path> [tid...]
// Without tids it picks the threads that used the most CPU over one second.
#include <windows.h>
static HANDLE g_proc;
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
#include <timeapi.h>
static std::string Line(DWORD64 addr) {
    IMAGEHLP_LINE64 l{sizeof(l)}; DWORD d = 0;
    if (!SymGetLineFromAddr64(g_proc, addr, &d, &l)) return "";
    std::string f = l.FileName; auto k = f.find_last_of("\\/"); if (k != std::string::npos) f = f.substr(k + 1);
    return f + ":" + std::to_string(l.LineNumber);
}



static std::string Sym(DWORD64 addr) {
    static std::unordered_map<DWORD64, std::string> cache;
    auto it = cache.find(addr);
    if (it != cache.end()) return it->second;
    char buf[sizeof(SYMBOL_INFO) + 512];
    auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 511;
    DWORD64 disp = 0;
    std::string mod = "?";
    IMAGEHLP_MODULE64 mi{sizeof(mi)};
    if (SymGetModuleInfo64(g_proc, addr, &mi)) mod = mi.ModuleName;
    std::string r;
    if (SymFromAddr(g_proc, addr, &disp, si)) r = mod + "!" + si->Name;
    else if (mod != "?") { char b[64]; sprintf_s(b, "+0x%llx", addr - mi.BaseOfImage); r = mod + b; }
    else r = "JIT";
    cache[addr] = r;
    return r;
}

static double ThreadCpu(HANDLE h) {
    FILETIME c, e, k, u;
    GetThreadTimes(h, &c, &e, &k, &u);
    return (double)(((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 1e4;
}

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: stackprof pid seconds sympath [tid...]\n"); return 1; }
    DWORD pid = atoi(argv[1]);
    int seconds = atoi(argv[2]);
    g_proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!g_proc) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitialize(g_proc, argv[3], TRUE)) { printf("SymInitialize failed\n"); return 1; }

    std::vector<DWORD> tids;
    for (int i = 4; i < argc; ++i) tids.push_back(atoi(argv[i]));
    std::map<DWORD, HANDLE> handles;
    if (tids.empty()) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof(te)};
        std::vector<std::pair<DWORD, double>> cpu0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!h) continue;
            handles[te.th32ThreadID] = h;
            cpu0.push_back({te.th32ThreadID, ThreadCpu(h)});
        }
        CloseHandle(snap);
        Sleep(1000);
        std::vector<std::pair<double, DWORD>> d;
        for (auto& [tid, c] : cpu0) d.push_back({ThreadCpu(handles[tid]) - c, tid});
        std::sort(d.rbegin(), d.rend());
        for (size_t i = 0; i < d.size() && i < 8; ++i) {
            if (d[i].first < 50) break;
            printf("thread %lu cpu %.0f%%\n", d[i].second, d[i].first / 10);
            tids.push_back(d[i].second);
        }
    } else {
        for (DWORD t : tids) handles[t] = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t);
    }

    timeBeginPeriod(1);
    struct Agg { int samples = 0; std::map<std::string, int> lines, self, incl; std::map<std::string, int> stacks; std::map<std::string, int> edges; };
    std::map<DWORD, Agg> agg;
    std::map<DWORD, std::vector<std::vector<DWORD64>>> raw;
    ULONGLONG end = GetTickCount64() + seconds * 1000ULL;
    alignas(16) CONTEXT ctx;
    while (GetTickCount64() < end) {
        for (DWORD tid : tids) {
            HANDLE h = handles[tid];
            if (SuspendThread(h) == (DWORD)-1) continue;
            ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            std::vector<DWORD64> frames;
            if (GetThreadContext(h, &ctx)) {
                STACKFRAME64 sf{};
                sf.AddrPC.Offset = ctx.Rip; sf.AddrPC.Mode = AddrModeFlat;
                sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
                sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
                CONTEXT c2 = ctx;
                for (int i = 0; i < 40; ++i) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_proc, h, &sf, &c2, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                    if (!sf.AddrPC.Offset) break;
                    frames.push_back(sf.AddrPC.Offset);
                }
            }
            ResumeThread(h);
            raw[tid].push_back(std::move(frames));
        }
        Sleep(1);
    }
    if (const char* rawp = std::getenv("STACKPROF_RAW")) { FILE* f = fopen(rawp, "w"); for (auto& [tid, list] : raw) for (auto& fr : list) if (!fr.empty()) fprintf(f, "%lu,%llx\n", tid, fr[0]); fclose(f); }
    // Symbolize after sampling so symbol loading does not stall the target.
    for (auto& [tid, list] : raw) {
        Agg& a = agg[tid];
        for (auto& frames : list) {
            a.samples++;
            if (frames.empty()) { a.self["<no stack>"]++; continue; }
            std::vector<std::string> names;
            for (DWORD64 f : frames) names.push_back(Sym(f));
            a.self[names[0]]++;
            { std::string ln = Line(frames[0]); if (!ln.empty()) a.lines[ln + "  " + names[0].substr(0, 60)]++; }
            std::map<std::string, int> seen;
            for (auto& n : names) if (!seen[n]++) a.incl[n]++;
            std::string key;
            for (size_t i = 0; i < names.size() && i < 12; ++i) key += (i ? " <- " : "") + names[i];
            a.stacks[key]++;
            for (size_t i = 0; i + 1 < names.size(); ++i) { std::string e = names[i+1] + " -> " + names[i]; if (!seen[e]++) a.edges[e]++; }
        }
    }
    for (auto& [tid, a] : agg) {
        printf("\n===== thread %lu: %d samples\n-- self\n", tid, a.samples);
        auto top = [&](std::map<std::string, int>& m, size_t n) {
            std::vector<std::pair<int, std::string>> v;
            for (auto& [k, c] : m) v.push_back({c, k});
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < n; ++i)
                printf("%5.1f%%  %s\n", 100.0 * v[i].first / a.samples, v[i].second.c_str());
        };
        top(a.self, 60);
        printf("-- self lines\n");
        top(a.lines, 120);
        printf("-- inclusive\n");
        top(a.incl, 150);
        printf("-- stacks\n");
        top(a.stacks, 5);
        printf("-- edges\n");
        top(a.edges, 400);
    }
    return 0;
}

