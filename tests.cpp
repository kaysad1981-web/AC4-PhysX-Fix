#include "engine.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
using namespace ac4;
int checks = 0;
void require(bool value, const char *s) {
    ++checks;
    if (!value)
        throw std::runtime_error(s);
}
Package sample() {
    Package p;
    p.files = {{"dinput8.dll", bytes("test game fixes")},
               {"nvcuda.dll", bytes("test physx")},
               {"AC4-PhysX-D3D/ac4d3d.dll", bytes("test backend")},
               {"AC4-PhysX-D3D/k/one.cso", bytes("test kernel")}};
    for (auto &[s, b] : p.files)
        p.hashes[s] = sha(b);
    p.ini = "[Modules]\nCPUFix={CPU}\nFPSAndElevator={FPS}\n[FPS]\nLimit={LIMIT}\n[Graphics]\nCharacterLOD={"
            "CHARLOD}\nLODScale={LODSCALE}\n[Debug]\nDiagnostics={DIAG}\n";
    return p;
}
Context context() {
    Context c;
    c.supportedHash = sha(bytes("fixture game"));
    c.running = []() { return false; };
    return c;
}
std::map<std::string, std::string> snapshot(const fs::path &r) {
    std::map<std::string, std::string> result;
    for (auto &e : fs::recursive_directory_iterator(r)) {
        if (e.is_regular_file())
            result[utf8(e.path().lexically_relative(r).generic_wstring())] = hash(e.path());
    }
    return result;
}
template <class F> void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (...) {
        failed = true;
    }
    require(failed, "operation unexpectedly accepted");
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc >= 3 && std::wstring(argv[1]) == L"--package") {
            fs::path source = argv[2];
            Package real;
            auto catalog = json::parse(text(read(source / L"payload-hashes.json")));
            for (auto &[s, h] : catalog.items()) {
                real.hashes[s] = h;
                real.files[s] = read(source / L"payload" / wide(s));
                require(sha(real.files[s]) == real.hashes[s], "real payload hash mismatch");
            }
            real.ini = text(read(source / L"Settings.template.ini"));
            auto root =
                fs::absolute(L"test-artifacts") / (L"real-payload-" + std::to_wstring(GetTickCount64()));
            fs::create_directories(root);
            write(root / L"AC4BFSP.exe", bytes("fixture game"));
            write(root / L"save.dat", bytes("original save"));
            auto original = snapshot(root);
            Engine engine(real, context(), root);
            engine.install({});
            auto full = snapshot(root);
            require(full.size() == real.files.size() + 4, "real payload installation count mismatch");
            for (int position : {1, 203, 406}) {
                auto c = context();
                c.afterCommit = [position](size_t n) {
                    if (n == (size_t)position)
                        throw std::runtime_error("real package commit fault");
                };
                Engine failing(real, c, root);
                Options o;
                o.physx = false;
                rejects([&] { failing.install(o); });
                require(snapshot(root) == full, "real package rollback mismatch");
            }
            Options changed;
            changed.physx = false;
            changed.fps = 141;
            engine.install(changed);
            require(!fs::exists(root / L"nvcuda.dll"), "real component switch failed");
            engine.remove(true);
            require(snapshot(root) == original, "real payload roundtrip mismatch");
            if (argc == 4) {
                fs::path manual = argv[3];
                for (const auto &entry : fs::recursive_directory_iterator(manual)) {
                    auto target = root / entry.path().lexically_relative(manual);
                    if (entry.is_directory())
                        fs::create_directories(target);
                    else if (entry.is_regular_file())
                        write(target, read(entry.path()));
                }
                require(engine.installed(), "manual ZIP manifest not recognized");
                engine.remove(true);
                require(snapshot(root) == original, "manual ZIP uninstall changed original files");
            }
            std::cout << "PASS: " << checks << " real-package checks; " << real.files.size()
                      << " original payload files, full install, component update, early/middle/late "
                         "rollback, clean remove.\n";
            return 0;
        }
        if (argc == 4 && (std::wstring(argv[1]) == L"--crash" || std::wstring(argv[1]) == L"--stage-crash")) {
            auto c = context();
            int target = _wtoi(argv[3]);
            auto fault = [target](size_t n) {
                if (n == (size_t)target)
                    ExitProcess(77);
            };
            if (std::wstring(argv[1]) == L"--stage-crash")
                c.afterStage = fault;
            else
                c.afterCommit = fault;
            Engine e(sample(), c, argv[2]);
            Options o;
            o.fps = 144;
            o.physx = false;
            e.install(o);
            return 3;
        }
        auto base = fs::absolute(L"test-artifacts");
        fs::create_directories(base);
        auto root = base / (L"AC4-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()) + L"-тест");
        fs::create_directory(root);
        write(root / L"AC4BFSP.exe", bytes("fixture game"));
        write(root / L"unrelated-save.dat", bytes("KEEP"));
        auto original = snapshot(root);
        auto p = sample();
        Engine e(p, context(), root);
        e.validateGame();
        Options o;
        e.install(o);
        require(e.installed(), "install manifest missing");
        require(fs::exists(root / L"nvcuda.dll"), "physx missing");
        require(text(read(root / L"AC4Fix/Settings.ini")).find("Limit=0") != std::string::npos,
                "auto limit incorrect");
        auto full = snapshot(root);
        o.physx = false;
        o.cpu = false;
        o.fps = 141;
        o.preset = 2;
        e.install(o);
        require(!fs::exists(root / L"nvcuda.dll"), "old component not removed");
        require(text(read(root / L"AC4Fix/Settings.ini")).find("Limit=141") != std::string::npos,
                "custom limit incorrect");
        e.remove(true);
        require(snapshot(root) == original, "round trip changed game");
        write(root / L"dinput8.dll", bytes("foreign mod"));
        auto foreign = snapshot(root);
        rejects([&] { e.install({}); });
        require(snapshot(root) == foreign, "conflict changed files");
        fs::remove(root / L"dinput8.dll");
        // Preserve pre-existing user configuration, but back up and remove user settings generated after
        // installation.
        fs::create_directory(root / L"AC4Fix");
        write(root / L"AC4Fix/User.ini", bytes("pre-existing"));
        auto withUser = snapshot(root);
        e.install({});
        e.remove(true);
        require(snapshot(root) == withUser, "pre-existing User.ini lost");
        fs::remove(root / L"AC4Fix/User.ini");
        fs::remove(root / L"AC4Fix");
        e.install({});
        write(root / L"AC4Fix/Settings.ini", bytes("custom changed settings"));
        write(root / L"AC4Fix/User.ini", bytes("new user settings"));
        write(root / L"AC4Fix/runtime-test.log", bytes("diagnostic"));
        auto removed = e.remove(true);
        require(!removed.backup.empty() && fs::exists(removed.backup / L"AC4Fix/User.ini"),
                "user backup missing");
        require(snapshot(root) == original, "generated data left behind");
        // Every individual commit position must roll back an update byte-for-byte.
        e.install({});
        full = snapshot(root);
        for (int fault = 1; fault <= 7; ++fault) {
            auto c = context();
            c.afterCommit = [fault](size_t n) {
                if (n == (size_t)fault)
                    throw std::runtime_error("injected I/O error");
            };
            Engine failing(p, c, root);
            Options changed;
            changed.physx = false;
            changed.fps = 144;
            bool failed = false;
            try {
                failing.install(changed);
            } catch (...) {
                failed = true;
            }
            if (failed)
                require(snapshot(root) == full, "fault rollback differs");
            else {
                e.install({});
                full = snapshot(root);
            }
        }
        for (int fault = 1; fault <= 6; ++fault) {
            auto c = context();
            c.afterStage = [fault](size_t n) {
                if (n == (size_t)fault)
                    throw std::runtime_error("injected staging failure");
            };
            Engine failing(p, c, root);
            rejects([&] { failing.install({}); });
            require(snapshot(root) == full, "preparation failure altered game");
        }
        // Simulate process termination during commit and recover with a new process instance.
        wchar_t self[32768];
        GetModuleFileNameW(nullptr, self, 32768);
        for (int fault = 1; fault <= 7; ++fault) {
            std::wstring cmd = L"\"" + std::wstring(self) + L"\" --crash \"" + root.wstring() + L"\" " +
                               std::to_wstring(fault);
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi) != 0,
                    "crash test process failed");
            WaitForSingleObject(pi.hProcess, 30000);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 77) {
                require(e.pending(), "crash journal missing");
                e.recover();
                require(snapshot(root) == full, "crash recovery differs");
            } else {
                e.install({});
                full = snapshot(root);
            }
        }
        for (int fault = 1; fault <= 6; ++fault) {
            std::wstring cmd = L"\"" + std::wstring(self) + L"\" --stage-crash \"" + root.wstring() + L"\" " +
                               std::to_wstring(fault);
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            require(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi) != 0,
                    "stage crash process failed");
            WaitForSingleObject(pi.hProcess, 30000);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            require(code == 77, "stage fault not triggered");
            e.recover();
            require(snapshot(root) == full, "stage crash recovery differs");
        }
        for (int fault = 1; fault <= 6; ++fault) {
            auto c = context();
            c.afterCommit = [fault](size_t n) {
                if (n == (size_t)fault)
                    throw std::runtime_error("uninstall fault");
            };
            Engine failing(p, c, root);
            rejects([&] { failing.remove(true); });
            require(snapshot(root) == full, "uninstall rollback differs");
        }
        // Modified patch DLL must stop deletion before any file is removed.
        write(root / L"dinput8.dll", bytes("replacement mod"));
        auto changed = snapshot(root);
        rejects([&] { e.remove(true); });
        require(snapshot(root) == changed, "foreign replacement deleted");
        write(root / L"dinput8.dll", p.files["dinput8.dll"]);
        // Manifest traversal and directory tampering are rejected without touching outside data.
        auto manifest = read(root / wide(MANIFEST));
        auto m = json::parse(text(manifest));
        m["files"].push_back({{"path", "../unrelated-save.dat"}, {"sha256", sha(bytes("KEEP"))}});
        write(root / wide(MANIFEST), bytes(m.dump()));
        rejects([&] { e.remove(true); });
        require(text(read(root / L"unrelated-save.dat")) == "KEEP", "traversal escaped root");
        write(root / wide(MANIFEST), manifest);
        m = json::parse(text(manifest));
        m["dirs"].push_back("../outside");
        write(root / wide(MANIFEST), bytes(m.dump()));
        auto badManifest = snapshot(root);
        rejects([&] { e.remove(true); });
        require(snapshot(root) == badManifest, "invalid directories caused partial removal");
        write(root / wide(MANIFEST), manifest);
        m = json::parse(text(manifest));
        m["root"] = "";
        write(root / wide(MANIFEST), bytes(m.dump()));
        e.remove(true);
        require(snapshot(root) == original, "manual manifest removal failed");
        e.install({});
        write(root / L"AC4Fix/User.ini", bytes("legacy user settings"));
        m = json::parse(text(read(root / wide(MANIFEST))));
        m.erase("baseline");
        m.erase("baselineKnown");
        write(root / wide(MANIFEST), bytes(m.dump()));
        e.install({});
        e.remove(true);
        require(text(read(root / L"AC4Fix/User.ini")) == "legacy user settings",
                "legacy settings with unknown history were removed");
        fs::remove(root / L"AC4Fix/User.ini");
        fs::remove(root / L"AC4Fix");
        require(snapshot(root) == original, "legacy migration changed original files");
        e.install({});
        full = snapshot(root);
        std::atomic_bool midCancel = false;
        auto mid = context();
        mid.cancel = &midCancel;
        mid.afterCommit = [&](size_t n) {
            if (n == 2)
                midCancel = true;
        };
        Engine cancelMid(p, mid, root);
        rejects([&] {
            Options next;
            next.physx = false;
            cancelMid.install(next);
        });
        require(snapshot(root) == full, "mid-commit cancellation rollback differs");
        auto c = context();
        std::atomic_bool cancel = true;
        c.cancel = &cancel;
        Engine cancelled(p, c, root);
        auto before = snapshot(root);
        rejects([&] { cancelled.install({}); });
        require(snapshot(root) == before, "cancel changed files");
        e.remove(true);
        require(snapshot(root) == original, "final game changed");
        Context wrong;
        wrong.running = []() { return false; };
        Engine unsupported(p, wrong, root);
        rejects([&] { unsupported.install({}); });
        require(snapshot(root) == original, "unsupported game changed");
        auto running = context();
        running.running = []() { return true; };
        Engine blocked(p, running, root);
        rejects([&] { blocked.install({}); });
        require(snapshot(root) == original, "running game was modified");
        // Root and destination hard links must never be modified by the installer.
        auto external = base / L"external-test.dat";
        write(external, bytes("external"));
        require(CreateHardLinkW((root / L"dinput8.dll").c_str(), external.c_str(), nullptr) != 0,
                "cannot create test hardlink");
        rejects([&] { e.install({}); });
        require(text(read(external)) == "external", "hardlink target modified");
        fs::remove(root / L"dinput8.dll");
        fs::remove(external);
        std::cout << "PASS: " << checks
                  << " checks; install/update/remove, Unicode paths, foreign files, user backups, rollback, "
                     "crash recovery, manifest traversal, cancellation, unsupported EXE, hard links.\n";
        // Keep test fixtures for inspection; no recursive cleanup is performed.
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
