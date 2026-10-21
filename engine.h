#pragma once
#include "vendor/json.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
namespace ac4 {
namespace fs = std::filesystem;
using json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
inline constexpr char GAME_HASH[] = "bc11ad7924e1eca7363ee86aa3332c05386d0a05896c0b7370131daee9ab5150";
inline constexpr char PRODUCT[] = "AC4-Modular-Patch";
inline constexpr char MANIFEST[] = "AC4Fix/install-manifest.json";
inline constexpr char SETTINGS[] = "AC4Fix/Settings.ini";
inline constexpr wchar_t TXN[] = L".AC4Patch-transaction";
std::string utf8(const std::wstring &);
std::wstring wide(const std::string &);
Bytes read(const fs::path &);
void write(const fs::path &, const Bytes &);
std::string sha(const Bytes &);
std::string hash(const fs::path &);
std::string text(const Bytes &);
Bytes bytes(const std::string &);
struct Options {
    bool fixes = true, cpu = true, physx = true, diagnostics = false;
    int fps = 0, preset = 0;
};
struct Package {
    std::map<std::string, Bytes> files;
    std::map<std::string, std::string> hashes;
    std::string ini;
};
struct Context {
    std::string supportedHash = GAME_HASH;
    std::function<void(int, const std::wstring &)> progress;
    std::atomic_bool *cancel = nullptr;
    std::function<bool()> running;
    // Fault injection is provided only by the separate test harness, never by setup arguments.
    std::function<void(size_t)> afterCommit;
    std::function<void(size_t)> afterStage;
};
struct Result {
    std::wstring message;
    fs::path backup;
};
class Engine {
    Package p;
    Context ctx;
    fs::path root;
    bool allowed(const std::string &) const;
    fs::path safe(const std::string &) const;
    json loadManifest() const;
    void transact(const std::map<std::string, Bytes> &, const std::set<std::string> &,
                  const std::vector<std::string> &);
    void recoverUnlocked();
    void report(int, const std::wstring &) const;
    void checkCancel() const;

  public:
    Engine(Package, Context, fs::path);
    void validateGame() const;
    bool installed() const;
    bool pending() const;
    json choices() const;
    Result install(const Options &);
    Result remove(bool cleanUserSettings);
    void recover();
};
bool gameRunning();
fs::path localData();
std::vector<fs::path> discover();
void remember(const fs::path &);
} // namespace ac4
