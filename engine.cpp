#include "engine.h"
#include <algorithm>
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <regex>
#include <shlobj.h>
#include <sstream>
#include <tlhelp32.h>
namespace ac4 {
static void fail(const std::string &s) {
    throw std::runtime_error(s);
}
static void wincheck(bool ok, const std::string &s) {
    if (!ok)
        fail(s + " [Windows " + std::to_string(GetLastError()) + "]");
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        fail("Invalid Unicode");
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        fail("Invalid UTF-8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
    return r;
}
Bytes bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
std::string text(const Bytes &b) {
    return std::string(b.begin(), b.end());
}
Bytes read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        fail("Cannot read: " + utf8(p.wstring()));
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || n > 128 * 1024 * 1024)
        fail("File size out of bounds: " + utf8(p.wstring()));
    f.seekg(0);
    Bytes b((size_t)n);
    if (n && !f.read((char *)b.data(), n))
        fail("Incomplete read");
    return b;
}
void write(const fs::path &p, const Bytes &b) {
    HANDLE h =
        CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    wincheck(h != INVALID_HANDLE_VALUE, "Cannot write: " + utf8(p.wstring()));
    DWORD out = 0;
    bool ok =
        WriteFile(h, b.data(), (DWORD)b.size(), &out, nullptr) && out == b.size() && FlushFileBuffers(h);
    DWORD err = GetLastError();
    CloseHandle(h);
    SetLastError(err);
    wincheck(ok, "Write failed");
}
std::string sha(const Bytes &b) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    auto st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0)
        fail("SHA256 unavailable");
    DWORD len = 0, size = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&size, sizeof(size), &len, 0);
    Bytes object(size), digest(32);
    st = BCryptCreateHash(alg, &h, object.data(), size, nullptr, 0, 0);
    if (st >= 0)
        st = BCryptHashData(h, const_cast<PUCHAR>(b.data()), (ULONG)b.size(), 0);
    if (st >= 0)
        st = BCryptFinishHash(h, digest.data(), 32, 0);
    if (h)
        BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0)
        fail("SHA256 failed");
    std::ostringstream s;
    for (auto v : digest)
        s << std::hex << std::setw(2) << std::setfill('0') << (int)v;
    return s.str();
}
std::string hash(const fs::path &p) {
    return sha(read(p));
}
static bool same(const fs::path &a, const fs::path &b) {
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}
static void noLinks(const fs::path &p) {
    fs::path current;
    for (auto &part : fs::absolute(p)) {
        current /= part;
        DWORD a = GetFileAttributesW(current.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("Links/junctions are not supported: " + utf8(current.wstring()));
    }
    if (fs::is_regular_file(p)) {
        HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        wincheck(h != INVALID_HANDLE_VALUE, "Cannot inspect file");
        BY_HANDLE_FILE_INFORMATION info{};
        bool ok = GetFileInformationByHandle(h, &info) != 0;
        CloseHandle(h);
        wincheck(ok, "Cannot inspect links");
        if (info.nNumberOfLinks != 1)
            fail("Hard-linked files are not supported");
    }
}
static bool relative(const std::string &s) {
    if (s.empty() || s.size() > 220 || s.front() == '/' || s.back() == '/' || s.find('\\') != s.npos ||
        s.find(':') != s.npos || s.find('\0') != s.npos)
        return false;
    std::stringstream ss(s);
    std::string seg;
    while (std::getline(ss, seg, '/'))
        if (seg.empty() || seg == "." || seg == ".." || seg.back() == '.' || seg.back() == ' ')
            return false;
    return true;
}
struct Lock {
    HANDLE h;
    explicit Lock(const fs::path &r) {
        auto name = L"Local\\AC4Fix-" + wide(sha(bytes(utf8(r.wstring()))));
        h = CreateMutexW(nullptr, FALSE, name.c_str());
        wincheck(h != nullptr, "Cannot lock installer");
        DWORD w = WaitForSingleObject(h, 0);
        if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
            CloseHandle(h);
            h = nullptr;
            fail("Another installer is working with this folder");
        }
    }
    ~Lock() {
        if (h) {
            ReleaseMutex(h);
            CloseHandle(h);
        }
    }
};
bool gameRunning() {
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    wincheck(h != INVALID_HANDLE_VALUE, "Cannot inspect running processes");
    PROCESSENTRY32W e{sizeof(e)};
    bool found = false;
    if (Process32FirstW(h, &e))
        do {
            if (!_wcsicmp(e.szExeFile, L"AC4BFSP.exe") || !_wcsicmp(e.szExeFile, L"AC4BFMP.exe")) {
                found = true;
                break;
            }
        } while (Process32NextW(h, &e));
    CloseHandle(h);
    return found;
}
fs::path localData() {
    PWSTR s = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &s)))
        fail("LocalAppData unavailable");
    fs::path p(s);
    CoTaskMemFree(s);
    return p / L"AC4-Modular-Patch";
}
static fs::path backupPath() {
    auto p = localData() / L"Removed" /
             (std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count()) + L"-" +
              std::to_wstring(GetCurrentProcessId()));
    noLinks(p);
    fs::create_directories(p);
    return p;
}
Engine::Engine(Package package, Context context, fs::path folder)
    : p(std::move(package)), ctx(std::move(context)), root(fs::absolute(folder).lexically_normal()) {
    if (root.filename().empty() && root.has_relative_path())
        root = root.parent_path();
    if (root == root.root_path())
        fail("Select a game directory, not a volume root");
    noLinks(root);
    if (!fs::is_directory(root))
        fail("Game folder does not exist");
    for (auto &[name, h] : p.hashes)
        if (!relative(name) ||
            name.rfind("AC4-PhysX-D3D/", 0) != 0 && name != "dinput8.dll" && name != "nvcuda.dll")
            fail("Invalid embedded catalog");
}
static bool runtimeName(const std::string &s) {
    return std::regex_match(
        s,
        std::regex(
            R"((AC4Fix/runtime[^/]*\.log|AC4Fix/EffectPackage[^/]*\.apb|AC4-PhysX-D3D/[^/]*\.(log|csv)|AC4-PhysX-D3D/crash-[^/]*\.txt))"));
}
static bool ownedDir(const std::string &s) {
    return relative(s) && (s == "AC4Fix" || s == "AC4-PhysX-D3D" || s.rfind("AC4-PhysX-D3D/", 0) == 0);
}
bool Engine::allowed(const std::string &s) const {
    return relative(s) &&
           (p.hashes.count(s) || s == SETTINGS || s == MANIFEST || s == "AC4Fix/User.ini" || runtimeName(s));
}
fs::path Engine::safe(const std::string &s) const {
    if (!allowed(s))
        fail("Unknown managed path: " + s);
    auto q = root / fs::path(wide(s));
    noLinks(q);
    return q;
}
void Engine::report(int n, const std::wstring &s) const {
    if (ctx.progress)
        ctx.progress(n, s);
}
void Engine::checkCancel() const {
    if (ctx.cancel && *ctx.cancel)
        fail("CANCELLED");
}
void Engine::validateGame() const {
    if (ctx.running ? ctx.running() : gameRunning())
        fail("GAME_RUNNING");
    noLinks(root / L"AC4BFSP.exe");
    if (!fs::is_regular_file(root / L"AC4BFSP.exe"))
        fail("GAME_NOT_FOUND");
    if (hash(root / L"AC4BFSP.exe") != ctx.supportedHash)
        fail("UNSUPPORTED_GAME");
}
bool Engine::installed() const {
    return fs::exists(safe(MANIFEST));
}
bool Engine::pending() const {
    auto q = root / TXN;
    noLinks(q);
    return fs::exists(q);
}
json Engine::loadManifest() const {
    auto m = json::parse(text(read(safe(MANIFEST))));
    if (m.value("product", "") != PRODUCT)
        fail("Invalid installation manifest");
    auto r = m.value("root", "");
    if (!r.empty() && !same(fs::path(wide(r)), root))
        fail("Manifest belongs to a different folder");
    if (!m.contains("files") || !m["files"].is_array())
        fail("Invalid manifest file list");
    std::set<std::string> seen;
    for (auto &f : m["files"]) {
        std::string s = f.at("path");
        if (!allowed(s) || s == MANIFEST || s == "AC4Fix/User.ini" || runtimeName(s) ||
            !seen.insert(s).second)
            fail("Invalid managed file: " + s);
        if (!std::regex_match(f.at("sha256").get<std::string>(), std::regex("[0-9a-f]{64}")))
            fail("Invalid file hash");
        if (p.hashes.count(s) && f.at("sha256").get<std::string>() != p.hashes.at(s))
            fail("Unknown patch version or modified manifest: " + s);
    }
    for (auto &d : m.value("dirs", std::vector<std::string>{}))
        if (!ownedDir(d))
            fail("Invalid owned directory");
    return m;
}
json Engine::choices() const {
    return loadManifest();
}
[[maybe_unused]] static void mkdirTracked(const fs::path &root, const fs::path &dir,
                                          std::vector<std::string> &created) {
    std::vector<fs::path> missing;
    for (auto q = dir; !same(q, root) && !fs::exists(q); q = q.parent_path())
        missing.push_back(q);
    for (auto i = missing.rbegin(); i != missing.rend(); ++i) {
        noLinks(*i);
        fs::create_directory(*i);
        created.push_back(utf8(i->lexically_relative(root).generic_wstring()));
    }
}
static void removeEmpty(const fs::path &root, std::vector<std::string> dirs) {
    std::sort(dirs.begin(), dirs.end(), [](auto &a, auto &b) { return a.size() > b.size(); });
    for (auto &s : dirs) {
        if (!relative(s) || (s != "AC4Fix" && s != "AC4-PhysX-D3D" && s.rfind("AC4-PhysX-D3D/", 0) != 0))
            fail("Invalid owned directory");
        auto q = root / wide(s);
        noLinks(q);
        if (fs::is_directory(q) && fs::is_empty(q))
            fs::remove(q);
    }
}
void Engine::recoverUnlocked() {
    auto tx = root / TXN;
    noLinks(tx);
    if (!fs::exists(tx))
        return;
    auto jp = tx / L"journal.json";
    noLinks(jp);
    if (!fs::exists(jp))
        fail("Incomplete transaction without a journal; inspect .AC4Patch-transaction before continuing");
    auto j = json::parse(text(read(jp)));
    if (j.value("product", "") != PRODUCT || !same(wide(j.at("root").get<std::string>()), root))
        fail("Invalid transaction journal");
    if (j.value("committed", false)) {
        // A durable committed marker means the new installation is authoritative.
    } else if (j.value("prepared", true)) {
        for (auto &entry : j.at("ops")) {
            std::string rel = entry.at("path");
            auto target = safe(rel);
            auto old = tx / L"old" / wide(rel);
            noLinks(old);
            bool had = entry.at("hadOld");
            if (had) {
                if (!fs::is_regular_file(old) || hash(old) != entry.at("oldHash").get<std::string>())
                    fail("Rollback backup is missing or corrupt");
                if (fs::exists(target)) {
                    auto current = hash(target);
                    if (current != entry.at("oldHash").get<std::string>() &&
                        current != entry.value("newHash", std::string{}))
                        fail("Rollback conflict: " + rel);
                }
                fs::create_directories(target.parent_path());
                auto temp = tx / L"restore.tmp";
                write(temp, read(old));
                wincheck(MoveFileExW(temp.c_str(), target.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                         "Cannot restore original file");
            } else if (fs::exists(target)) {
                if (hash(target) != entry.value("newHash", std::string{}))
                    fail("Rollback conflict: " + rel);
                fs::remove(target);
            }
        }
        removeEmpty(root, j.value("createdDirs", std::vector<std::string>{}));
    }
    // Every transaction path is derived from validated operation names; never recurse through unknown
    // entries.
    for (auto &entry : j.at("ops")) {
        std::string rel = entry.at("path");
        if (!allowed(rel))
            fail("Invalid journal path");
        for (auto prefix : {L"old", L"new"}) {
            auto q = tx / prefix / wide(rel);
            noLinks(q);
            if (fs::is_regular_file(q))
                fs::remove(q);
        }
    }
    if (fs::exists(tx / L"restore.tmp"))
        fs::remove(tx / L"restore.tmp");
    std::vector<fs::path> empty;
    for (auto it = fs::recursive_directory_iterator(tx); it != fs::recursive_directory_iterator(); ++it) {
        noLinks(it->path());
        if (it->is_directory())
            empty.push_back(it->path());
        else if (it->path() != jp && it->path() != tx / L"journal.next")
            fail("Unknown file in transaction folder");
    }
    std::sort(empty.begin(), empty.end(),
              [](auto &a, auto &b) { return a.native().size() > b.native().size(); });
    for (auto &q : empty)
        if (fs::is_empty(q))
            fs::remove(q);
    if (fs::exists(tx / L"journal.next"))
        fs::remove(tx / L"journal.next");
    fs::remove(jp);
    fs::remove(tx);
}
void Engine::recover() {
    Lock lock(root);
    if (ctx.running ? ctx.running() : gameRunning())
        fail("GAME_RUNNING");
    recoverUnlocked();
}
void Engine::transact(const std::map<std::string, Bytes> &desired, const std::set<std::string> &deletions,
                      const std::vector<std::string> &cleanupDirs) {
    auto tx = root / TXN;
    if (fs::exists(tx))
        fail("Pending transaction");
    wincheck(CreateDirectoryW(tx.c_str(), nullptr) != 0,
             "Cannot create transaction folder (check write permissions)");
    json j = {{"product", PRODUCT}, {"root", utf8(root.wstring())}, {"committed", false},
              {"prepared", false},  {"ops", json::array()},         {"createdDirs", json::array()}};
    auto journal = [&]() {
        write(tx / L"journal.next", bytes(j.dump(2)));
        wincheck(MoveFileExW((tx / L"journal.next").c_str(), (tx / L"journal.json").c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                 "Cannot persist transaction journal");
    };
    std::set<std::string> names = deletions;
    for (auto &[s, b] : desired) {
        (void)b;
        names.insert(s);
    }
    std::vector<std::string> made;
    try {
        // Fully prepare and verify every backup before the first change in the game directory.
        for (auto &s : names) {
            checkCancel();
            auto target = safe(s);
            bool had = fs::exists(target);
            if (had && !fs::is_regular_file(target))
                fail("Target is not a regular file: " + s);
            j["ops"].push_back({{"path", s},
                                {"hadOld", had},
                                {"oldHash", had ? hash(target) : ""},
                                {"newHash", desired.count(s) ? sha(desired.at(s)) : ""}});
        }
        journal();
        size_t staged = 0;
        for (auto &op : j["ops"]) {
            checkCancel();
            std::string s = op["path"];
            auto target = safe(s);
            if (op["hadOld"].get<bool>()) {
                auto b = tx / L"old" / wide(s);
                fs::create_directories(b.parent_path());
                write(b, read(target));
                if (hash(b) != op["oldHash"].get<std::string>())
                    fail("Backup verification failed");
            }
            if (desired.count(s)) {
                auto stage = tx / L"new" / wide(s);
                fs::create_directories(stage.parent_path());
                write(stage, desired.at(s));
                if (hash(stage) != op["newHash"].get<std::string>())
                    fail("Staged data verification failed");
            }
            if (ctx.afterStage)
                ctx.afterStage(++staged);
        }
        j["prepared"] = true;
        journal();
        for (auto &op : j["ops"]) {
            auto target = safe(op["path"].get<std::string>());
            std::vector<fs::path> missing;
            for (auto q = target.parent_path(); !same(q, root) && !fs::exists(q); q = q.parent_path())
                missing.push_back(q);
            for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
                auto rel = utf8(it->lexically_relative(root).generic_wstring());
                if (std::find(made.begin(), made.end(), rel) == made.end()) {
                    made.push_back(rel);
                    j["createdDirs"] = made;
                    journal();
                    noLinks(*it);
                    fs::create_directory(*it);
                }
            }
        }
        size_t index = 0;
        for (auto &op : j["ops"]) {
            checkCancel();
            if (ctx.running ? ctx.running() : gameRunning())
                fail("GAME_RUNNING");
            std::string s = op["path"];
            auto target = safe(s);
            bool had = op["hadOld"];
            if (fs::exists(target) != had || (had && hash(target) != op["oldHash"].get<std::string>()))
                fail("File changed during installation: " + s);
            if (desired.count(s)) {
                auto stage = tx / L"new" / wide(s);
                wincheck(MoveFileExW(stage.c_str(), target.c_str(),
                                     (had ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH) != 0,
                         "Cannot commit " + s);
                if (hash(target) != op["newHash"].get<std::string>())
                    fail("Installed file verification failed");
            } else if (had)
                fs::remove(target);
            ++index;
            report(15 + (int)(80 * index / names.size()), wide(s));
            if (ctx.afterCommit)
                ctx.afterCommit(index);
        }
        if (desired.count(MANIFEST) && hash(root / L"AC4BFSP.exe") != ctx.supportedHash)
            fail("Game EXE changed during installation");
        j["committed"] = true;
        journal();
        recoverUnlocked();
        removeEmpty(root, cleanupDirs);
        report(100, L"OK");
    } catch (...) {
        auto original = std::current_exception();
        if (!fs::exists(tx / L"journal.json")) {
            // No changes to the game have occurred before the first journal publication. Publish the prepared
            // subset for safe cleanup.
            try {
                journal();
                recoverUnlocked();
            } catch (...) {
                throw;
            }
        } else
            recoverUnlocked();
        std::rethrow_exception(original);
    }
}
static std::string replaceAll(std::string s, const std::string &f, const std::string &t) {
    size_t at = 0;
    while ((at = s.find(f, at)) != s.npos) {
        s.replace(at, f.size(), t);
        at += t.size();
    }
    return s;
}
Result Engine::install(const Options &o) {
    Lock lock(root);
    recoverUnlocked();
    validateGame();
    checkCancel();
    if (!o.fixes && !o.physx)
        fail("NO_COMPONENTS");
    if (o.fps != 0 && (o.fps < 30 || o.fps > 1000))
        fail("INVALID_FPS");
    if (o.preset < 0 || o.preset > 2)
        fail("Invalid preset");
    report(0, L"SHA256");
    std::map<std::string, Bytes> desired;
    for (auto &[s, b] : p.files)
        if ((o.fixes && s == "dinput8.dll") ||
            (o.physx && (s == "nvcuda.dll" || s.rfind("AC4-PhysX-D3D/", 0) == 0))) {
            if (!p.hashes.count(s) || sha(b) != p.hashes.at(s))
                fail("Corrupt embedded payload: " + s);
            desired[s] = b;
        }
    if (o.fixes && !desired.count("dinput8.dll") || o.physx && !desired.count("nvcuda.dll"))
        fail("Incomplete package");
    std::set<std::string> oldNames;
    json previous;
    std::vector<std::string> dirs;
    std::set<std::string> baseline;
    bool baselineKnown = true;
    Result result;
    if (installed()) {
        previous = loadManifest();
        for (auto &f : previous["files"]) {
            std::string s = f["path"];
            oldNames.insert(s);
            auto q = safe(s);
            if (fs::exists(q) && hash(q) != f["sha256"].get<std::string>()) {
                if (s != SETTINGS)
                    fail("CONFLICT: " + s);
                if (result.backup.empty())
                    result.backup = backupPath();
                auto backup = result.backup / wide(s);
                fs::create_directories(backup.parent_path());
                write(backup, read(q));
            }
        }
        dirs = previous.value("dirs", std::vector<std::string>{});
        baseline = previous.value("baseline", std::set<std::string>{});
        baselineKnown = previous.value("baselineKnown", previous.contains("baseline"));
    } else {
        for (auto prefix : {L"AC4Fix", L"AC4-PhysX-D3D"}) {
            auto q = root / prefix;
            noLinks(q);
            if (fs::exists(q)) {
                if (!fs::is_directory(q))
                    fail("Directory conflict");
                for (auto &entry : fs::recursive_directory_iterator(q)) {
                    noLinks(entry.path());
                    if (entry.is_regular_file())
                        baseline.insert(utf8(entry.path().lexically_relative(root).generic_wstring()));
                }
            }
        }
    }
    for (auto &[s, b] : desired) {
        (void)b;
        auto q = safe(s);
        if (fs::exists(q) && !oldNames.count(s))
            fail("CONFLICT: " + s);
    }
    if (fs::exists(safe(SETTINGS)) && !oldNames.count(SETTINGS))
        fail("CONFLICT: Settings.ini");
    auto ini = p.ini;
    const char *names[] = {"game", "high", "ultra"};
    const char *chars[] = {"-1", "0.5", "0"};
    const char *lod[] = {"-1", "0.75", "0.5"};
    for (auto &kv : std::map<std::string, std::string>{{"{CPU}", o.cpu && o.fixes ? "1" : "0"},
                                                       {"{FPS}", o.fixes ? "1" : "0"},
                                                       {"{LIMIT}", std::to_string(o.fps)},
                                                       {"{DIAG}", o.diagnostics ? "1" : "0"},
                                                       {"{PRESET}", names[o.preset]},
                                                       {"{CHARLOD}", chars[o.preset]},
                                                       {"{LODSCALE}", lod[o.preset]}})
        ini = replaceAll(ini, kv.first, kv.second);
    desired[SETTINGS] = bytes(ini);
    for (auto &[s, b] : desired) {
        (void)b;
        for (auto d = (root / wide(s)).parent_path(); !same(d, root) && !fs::exists(d); d = d.parent_path()) {
            auto rel = utf8(d.lexically_relative(root).generic_wstring());
            if (std::find(dirs.begin(), dirs.end(), rel) == dirs.end())
                dirs.push_back(rel);
        }
    }
    json m = {{"product", PRODUCT},
              {"version", "0.3.1-native"},
              {"patchVersion", "0.3.0-alpha"},
              {"root", utf8(root.wstring())},
              {"exeSha256", ctx.supportedHash},
              {"build", "Steam (2023)"},
              {"fixes", o.fixes},
              {"cpu", o.cpu && o.fixes},
              {"physx", o.physx},
              {"limit", o.fps},
              {"preset", o.preset},
              {"diag", o.diagnostics},
              {"baseline", baseline},
              {"baselineKnown", baselineKnown},
              {"dirs", dirs},
              {"files", json::array()}};
    for (auto &[s, b] : desired)
        m["files"].push_back({{"path", s}, {"sha256", sha(b)}});
    desired[MANIFEST] = bytes(m.dump(2));
    oldNames.insert(MANIFEST);
    transact(desired, oldNames, {});
    result.message = L"INSTALLED";
    return result;
}
Result Engine::remove(bool clean) {
    Lock lock(root);
    recoverUnlocked();
    if (ctx.running ? ctx.running() : gameRunning())
        fail("GAME_RUNNING");
    if (!fs::is_regular_file(root / L"AC4BFSP.exe"))
        fail("GAME_NOT_FOUND");
    auto m = loadManifest();
    std::set<std::string> names;
    Result r;
    auto preserve = [&](const std::string &s) {
        if (r.backup.empty())
            r.backup = backupPath();
        auto q = r.backup / wide(s);
        fs::create_directories(q.parent_path());
        write(q, read(safe(s)));
    };
    for (auto &f : m["files"]) {
        std::string s = f["path"];
        auto q = safe(s);
        if (fs::exists(q) && hash(q) != f["sha256"].get<std::string>()) {
            if (s != SETTINGS)
                fail("CONFLICT: " + s);
            preserve(s);
        }
        names.insert(s);
    }
    auto baseline = m.value("baseline", std::set<std::string>{});
    for (auto prefix : {L"AC4Fix", L"AC4-PhysX-D3D"}) {
        auto q = root / prefix;
        noLinks(q);
        if (fs::is_directory(q))
            for (auto &entry : fs::directory_iterator(q)) {
                noLinks(entry.path());
                if (!entry.is_regular_file())
                    continue;
                auto s = utf8(entry.path().lexically_relative(root).generic_wstring());
                if (!baseline.count(s) && runtimeName(s)) {
                    preserve(s);
                    names.insert(s);
                }
            }
    }
    auto user = safe("AC4Fix/User.ini");
    if (clean && m.value("baselineKnown", m.contains("baseline")) && fs::exists(user) &&
        !baseline.count("AC4Fix/User.ini")) {
        preserve("AC4Fix/User.ini");
        names.insert("AC4Fix/User.ini");
    }
    names.insert(MANIFEST);
    transact({}, names, m.value("dirs", std::vector<std::string>{}));
    r.message = fs::exists(user) ? L"REMOVED_KEEP_USER" : L"REMOVED";
    return r;
}
void remember(const fs::path &p) {
    auto folder = localData();
    noLinks(folder);
    fs::create_directories(folder);
    noLinks(folder / L"native-preferences.json");
    write(folder / L"native-preferences.json", bytes(json{{"path", utf8(p.wstring())}}.dump()));
}
static std::wstring reg(HKEY hive, const std::wstring &key, const wchar_t *name) {
    wchar_t buf[32768];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(hive, key.c_str(), name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &sz) ==
        ERROR_SUCCESS)
        return buf;
    return {};
}
std::vector<fs::path> discover() {
    std::vector<fs::path> candidates, found;
    try {
        auto pref = localData() / L"native-preferences.json";
        if (fs::exists(pref))
            candidates.emplace_back(wide(json::parse(text(read(pref))).value("path", "")));
    } catch (...) {
    }
    wchar_t exe[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, exe, (DWORD)std::size(exe));
    candidates.push_back(fs::path(exe).parent_path());
    candidates.push_back(fs::path(exe).parent_path().parent_path());
    for (auto hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (auto key : {L"SOFTWARE\\Ubisoft\\Assassin's Creed IV Black Flag",
                         L"SOFTWARE\\WOW6432Node\\Ubisoft\\Assassin's Creed IV Black Flag",
                         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 242050"})
            for (auto value : {L"InstallDir", L"InstallLocation"}) {
                auto s = reg(hive, key, value);
                if (!s.empty())
                    candidates.emplace_back(s);
            }
    for (auto hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (const std::wstring key : {L"SOFTWARE\\Ubisoft\\Launcher\\Installs",
                                       L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs"}) {
            HKEY installs = nullptr;
            if (RegOpenKeyExW(hive, key.c_str(), 0, KEY_READ, &installs) == ERROR_SUCCESS) {
                for (DWORD n = 0;; ++n) {
                    wchar_t name[256];
                    DWORD size = (DWORD)std::size(name);
                    auto code = RegEnumKeyExW(installs, n, name, &size, nullptr, nullptr, nullptr, nullptr);
                    if (code == ERROR_NO_MORE_ITEMS)
                        break;
                    if (code != ERROR_SUCCESS)
                        continue;
                    auto s = reg(hive, key + L"\\" + name, L"InstallDir");
                    if (!s.empty())
                        candidates.emplace_back(s);
                }
                RegCloseKey(installs);
            }
        }
    std::vector<fs::path> libraries;
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        for (auto key : {L"SOFTWARE\\Valve\\Steam", L"SOFTWARE\\WOW6432Node\\Valve\\Steam"})
            for (auto val : {L"SteamPath", L"InstallPath"}) {
                auto s = reg(hive, key, val);
                if (!s.empty())
                    libraries.emplace_back(s);
            }
    auto initial = libraries;
    for (auto &l : initial)
        try {
            auto s = text(read(l / L"steamapps/libraryfolders.vdf"));
            std::regex re("\"path\"\\s*\"([^\"]+)\"");
            for (auto i = std::sregex_iterator(s.begin(), s.end(), re); i != std::sregex_iterator(); ++i)
                libraries.emplace_back(wide(replaceAll((*i)[1], "\\\\", "\\")));
        } catch (...) {
        }
    for (auto &l : libraries) {
        auto steam = l / L"steamapps";
        candidates.push_back(steam / L"common/Assassin's Creed IV Black Flag");
        try {
            auto s = text(read(steam / L"appmanifest_242050.acf"));
            std::smatch m;
            if (std::regex_search(s, m, std::regex("\"installdir\"\\s*\"([^\"]+)\"")))
                candidates.push_back(steam / L"common" / wide(m[1]));
        } catch (...) {
        }
    }
    for (auto &q : candidates)
        try {
            if (q.empty())
                continue;
            q = fs::absolute(q).lexically_normal();
            noLinks(q);
            if (fs::is_regular_file(q / L"AC4BFSP.exe") &&
                std::none_of(found.begin(), found.end(), [&](auto &r) { return same(q, r); }))
                found.push_back(q);
        } catch (...) {
        }
    return found;
}
} // namespace ac4
