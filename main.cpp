#include "engine.h"
#include "generated/payload.h"
#include <array>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <regex>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thread>
#include <windowsx.h>
using namespace ac4;
using namespace Gdiplus;
namespace {
constexpr int RU = 100, EN = 101, CLOSE = 102, BACK = 103, NEXT = 104, BROWSE = 105, PATHBOX = 106,
              GAME = 107, PHYS = 108, CPU = 109, FPSMODE = 110, FPSVALUE = 111, QUALITY = 112, DIAG = 113,
              REMOVE = 114, CHECK = 115;
constexpr UINT DONE = WM_APP + 1, PROGRESS = WM_APP + 2, FOUND = WM_APP + 3;
HWND window = nullptr, tooltip = nullptr;
HFONT font = nullptr;
HBRUSH editBrush = nullptr;
float scale = 1;
bool english = false, busy = false, valid = false, existing = false, rebuilding = false, customLimit = false;
int step = 0, percent = 0;
Options opts;
fs::path folder;
std::wstring status, resultText;
std::atomic_bool cancelled = false;
std::map<int, HWND> controls;
std::vector<std::unique_ptr<std::wstring>> tips;
std::unique_ptr<Image> artwork;
IStream *artStream = nullptr;
ULONG_PTR gdiplus = 0;
Package package;
std::thread worker;
std::mutex messageMutex;
std::wstring progressText;
bool doneOK = false, removed = false;
std::wstring T(const wchar_t *ru, const wchar_t *en) {
    return english ? en : ru;
}
int S(float n) {
    return (int)(n * scale + .5f);
}
std::wstring label(int id) {
    switch (id) {
    case FPSMODE:
        return customLimit ? T(L"Свой лимит  ▾", L"Custom limit  ▾")
                           : T(L"Авто · частота экрана − 3  ▾", L"Auto · refresh rate − 3  ▾");
    case QUALITY:
        return (opts.preset == 0   ? T(L"Как в игре", L"As in game")
                : opts.preset == 1 ? T(L"Высокая", L"High")
                                   : T(L"Ультра", L"Ultra")) +
               L"  ▾";
    case RU:
        return L"RU";
    case EN:
        return L"EN";
    case CLOSE:
        return L"×";
    case BACK:
        return T(L"Назад", L"Back");
    case NEXT:
        return step == 0 ? T(L"Начать настройку", L"Get started")
               : step == 2
                   ? (existing ? T(L"Применить / обновить", L"Apply / update") : T(L"Установить", L"Install"))
               : step == 4 ? T(L"Завершить", L"Finish")
                           : T(L"Далее", L"Next");
    case BROWSE:
        return T(L"Обзор…", L"Browse…");
    case CHECK:
        return T(L"Проверить", L"Check");
    case REMOVE:
        return T(L"Удалить патч", L"Remove patch");
    case DIAG:
        return T(L"Диагностический журнал", L"Diagnostic logging");
    default:
        return L"";
    }
}
Bytes resource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r)
        throw std::runtime_error("Embedded resource missing");
    DWORD n = SizeofResource(nullptr, r);
    auto p = (unsigned char *)LockResource(LoadResource(nullptr, r));
    return Bytes(p, p + n);
}
void loadPackage() {
    package.ini = text(resource(202));
    for (const auto &entry : embeddedFiles) {
        package.files[entry.path] = resource(entry.id);
        package.hashes[entry.path] = entry.sha;
    }
    auto expected = json::parse(text(resource(203)));
    if (expected.size() != package.hashes.size())
        throw std::runtime_error("Catalog mismatch");
    for (auto &[s, h] : package.hashes)
        if (expected.at(s) != h || sha(package.files.at(s)) != h)
            throw std::runtime_error("Payload verification failed");
}
void loadArt() {
    Bytes b = resource(201);
    auto mem = GlobalAlloc(GMEM_MOVEABLE, b.size());
    if (!mem)
        return;
    void *ptr = GlobalLock(mem);
    memcpy(ptr, b.data(), b.size());
    GlobalUnlock(mem);
    if (SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &artStream)))
        artwork.reset(Image::FromStream(artStream));
    else
        GlobalFree(mem);
}
void drawText(Graphics &g, const std::wstring &s, RectF r, float size, Color c, bool bold = false) {
    FontFamily family(L"Segoe UI");
    Gdiplus::Font f(&family, size * scale, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush b(c);
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    g.DrawString(s.c_str(), -1, &f, r, &fmt, &b);
}
RectF R(float x, float y, float w, float h) {
    return RectF(x * scale, y * scale, w * scale, h * scale);
}
void textAt(Graphics &g, const std::wstring &s, float x, float y, float w, float h, float size = 14,
            Color color = Color(255, 205, 224, 232)) {
    drawText(g, s, R(x, y, w, h), size, color);
}
void rounded(GraphicsPath &p, RectF r, float radius) {
    float d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}
bool option(int id) {
    return id == GAME || id == PHYS || id == CPU || id == DIAG;
}
bool selected(int id) {
    return id == RU     ? !english
           : id == EN   ? english
           : id == GAME ? opts.fixes
           : id == PHYS ? opts.physx
           : id == CPU  ? opts.cpu
           : id == DIAG ? opts.diagnostics
                        : false;
}
std::wstring title(int id) {
    if (id == GAME)
        return T(L"Исправления игры при разблокировке FPS", L"Game fixes for unlocked FPS");
    if (id == PHYS)
        return L"PhysX / Direct3D 11";
    if (id == CPU)
        return T(L"Производительные ядра Intel", L"Intel performance cores only");
    return label(id);
}
std::wstring subtitle(int id) {
    if (id == GAME)
        return T(L"Камера, ткань, паруса, тела, лифт, режимы экрана",
                 L"Camera, cloth, sails, bodies, elevator, display modes");
    if (id == PHYS)
        return T(L"Дым и искры · NVIDIA / AMD / Intel*", L"Smoke and sparks · NVIDIA / AMD / Intel*");
    if (id == CPU)
        return L"Intel 12–14 / Core Ultra";
    return L"";
}
void drawButton(const DRAWITEMSTRUCT &d) {
    Graphics g(d.hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    int id = (int)d.CtlID;
    bool disabled = (d.itemState & ODS_DISABLED) != 0, pressed = (d.itemState & ODS_SELECTED) != 0;
    // Paint the matching parent background under the child to keep glass visually continuous.
    RECT client{};
    GetClientRect(d.hwndItem, &client);
    SolidBrush bg(Color(255, 15, 30, 39));
    g.FillRectangle(&bg, 0, 0, client.right, client.bottom);
    if (option(id)) {
        float y = pressed ? (float)S(1) : 0.f;
        SolidBrush checkFill(selected(id) ? Color(255, 89, 205, 221) : Color(255, 23, 44, 55));
        Pen edge(Color(255, 126, 163, 180), scale);
        g.FillRectangle(&checkFill, R(2, 8, 16, 16));
        g.DrawRectangle(&edge, R(2, 8, 16, 16));
        if (selected(id)) {
            Pen tick(Color(255, 8, 38, 45), 2 * scale);
            g.DrawLine(&tick, S(5), S(16), S(9), S(20));
            g.DrawLine(&tick, S(9), S(20), S(15), S(12));
        }
        drawText(g, title(id), RectF((REAL)S(29), y + (REAL)S(3), (REAL)(client.right - S(30)), (REAL)S(38)),
                 13, Color(255, disabled ? 110 : 235, disabled ? 133 : 245, disabled ? 143 : 249));
        if (id != DIAG)
            drawText(g, subtitle(id), R(29, 38, 385, 27), 12,
                     Color(255, disabled ? 85 : 172, disabled ? 108 : 197, disabled ? 120 : 210));
    } else {
        RectF rect((REAL)S(1), (REAL)S(pressed ? 4.f : 1.f), (REAL)(client.right - S(3)),
                   (REAL)(client.bottom - S(pressed ? 5.f : 6.f)));
        GraphicsPath path;
        rounded(path, rect, (float)S(7));
        SolidBrush shadow(Color(200, 1, 12, 20));
        g.TranslateTransform(0, (REAL)S(4));
        g.FillPath(&shadow, &path);
        g.ResetTransform();
        bool primary = id == NEXT;
        LinearGradientBrush glass(PointF(0, rect.Y), PointF(0, rect.GetBottom()),
                                  primary ? Color(255, 57, 124, 141) : Color(255, 48, 75, 89),
                                  primary ? Color(255, 18, 71, 86) : Color(255, 21, 43, 55));
        if (disabled) {
            LinearGradientBrush dim(PointF(0, rect.Y), PointF(0, rect.GetBottom()), Color(255, 31, 49, 59),
                                    Color(255, 20, 34, 42));
            g.FillPath(&dim, &path);
        } else
            g.FillPath(&glass, &path);
        Pen border(primary ? Color(210, 105, 220, 235) : Color(145, 135, 180, 199), scale);
        g.DrawPath(&border, &path);
        Pen shine(Color(disabled ? 30 : 115, 225, 250, 255), scale);
        g.DrawLine(&shine, rect.X + S(7), rect.Y + S(2), rect.GetRight() - S(7), rect.Y + S(2));
        SolidBrush sheen(Color(disabled ? 8 : 22, 220, 247, 255));
        g.SetClip(&path);
        g.FillRectangle(&sheen, rect.X, rect.Y, rect.Width, rect.Height * .46f);
        g.ResetClip();
        FontFamily family(L"Segoe UI");
        Gdiplus::Font f(&family, (id == CLOSE ? 23.f : 14.f) * scale, FontStyleRegular, UnitPixel);
        SolidBrush fg(disabled ? Color(255, 107, 131, 144) : Color(255, 227, 246, 251));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(label(id).c_str(), -1, &f, rect, &fmt, &fg);
    }
    if (d.itemState & ODS_FOCUS) {
        RECT focus = client;
        InflateRect(&focus, -S(3), -S(3));
        DrawFocusRect(d.hDC, &focus);
    }
}
void tip(HWND h, const std::wstring &s) {
    auto storage = std::make_unique<std::wstring>(s);
    TOOLINFOW t{sizeof(t)};
    t.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    t.hwnd = window;
    t.uId = (UINT_PTR)h;
    t.lpszText = storage->data();
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&t);
    tips.push_back(std::move(storage));
}
HWND control(int id, const wchar_t *cls, DWORD style, float x, float y, float w, float h,
             const std::wstring &value = L"") {
    auto result = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, value.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, S(x), S(y), S(w), S(h), window,
                                  (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    controls[id] = result;
    SendMessageW(result, WM_SETFONT, (WPARAM)font, TRUE);
    return result;
}
void button(int id, float x, float y, float w, float h = 42) {
    control(id, L"BUTTON", BS_OWNERDRAW, x, y, w, h, title(id));
}
std::wstring editValue(int id) {
    auto h = controls.at(id);
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
void readOptions() {
    if (step != 2)
        return;
    opts.fps = opts.fixes && customLimit ? _wtoi(editValue(FPSVALUE).c_str()) : 0;
}
void refreshEnabled() {
    if (step == 2) {
        bool custom = customLimit;
        ShowWindow(controls[FPSVALUE], custom ? SW_SHOW : SW_HIDE);
        MoveWindow(controls[FPSMODE], S(632), S(457), S(custom ? 300.f : 426.f), S(34), TRUE);
        EnableWindow(controls[CPU], opts.fixes);
        EnableWindow(controls[FPSMODE], opts.fixes);
        EnableWindow(controls[FPSVALUE], opts.fixes && custom);
        EnableWindow(controls[QUALITY], opts.fixes);
        int value = custom ? _wtoi(editValue(FPSVALUE).c_str()) : 0;
        EnableWindow(controls[NEXT], (opts.fixes || opts.physx) &&
                                         (!opts.fixes || !custom || (value >= 30 && value <= 1000)));
    } else if (step == 1)
        EnableWindow(controls[NEXT], valid && !busy);
    else
        EnableWindow(controls[NEXT], !busy);
    EnableWindow(controls[BACK], step > 0 && step < 3 && !busy);
}
void rebuild() {
    rebuilding = true;
    for (auto &[id, h] : controls) {
        (void)id;
        DestroyWindow(h);
    }
    controls.clear();
    tips.clear();
    if (tooltip)
        DestroyWindow(tooltip);
    tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, S(440));
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 25000);
    button(RU, 899, 14, 44, 34);
    button(EN, 951, 14, 44, 34);
    button(CLOSE, 1046, 12, 37, 36);
    button(BACK, 632, 625, 112);
    button(NEXT, 790, 625, 268);
    if (step == 1) {
        control(PATHBOX, L"EDIT", ES_AUTOHSCROLL, 632, 250, 314, 36, folder.wstring());
        button(BROWSE, 956, 250, 102, 38);
        button(CHECK, 632, 307, 128, 38);
        if (existing)
            button(REMOVE, 812, 471, 246);
        SendMessageW(controls[PATHBOX], EM_SETLIMITTEXT, 32000, 0);
    }
    if (step == 2) {
        customLimit = opts.fps != 0;
        button(GAME, 632, 204, 426, 70);
        button(PHYS, 632, 279, 426, 70);
        button(CPU, 632, 354, 426, 70);
        tip(controls[GAME],
            T(L"Исправления при высоком FPS:\n• Подавление зависшего крена камеры, включено автоматически.\n• Корректная скорость движения ткани, одежды, парусов и "
              L"флагов.\n• Рэгдоллы: устранение дёрганья и подбросов, столкновения тел.\n• Исправление "
              L"работы лифта при высоком FPS.\n• Разрешения и частоты обновления в меню экрана.\n• Точный "
              L"ограничитель FPS. F10: выбранный лимит / 60 FPS.",
              L"High-FPS fixes:\n• Scoped camera-roll fix, enabled automatically.\n• Correct movement speed for cloth, coats, sails and flags.\n• Ragdolls: "
              L"reduce twitching and launches; body collisions.\n• Elevator operation at high FPS.\n• "
              L"Display resolutions and refresh rates in the menu.\n• Precise frame limiter. F10: selected "
              L"limit / 60 FPS."));
        tip(controls[PHYS],
            T(L"PhysX-дым и искры костров через Direct3D 11. В исходном пакете заявлены NVIDIA, AMD и Intel. "
              L"Подтверждённые в документации тесты: RTX 5090 + i9-14900K, Steam 2023. AMD и Intel требуют "
              L"проверки на реальном оборудовании.",
              L"PhysX campfire smoke and sparks through Direct3D 11. The source package targets NVIDIA, AMD "
              L"and Intel. Documented testing: RTX 5090 + i9-14900K, Steam 2023. AMD and Intel still require "
              L"hardware validation."));
        tip(controls[CPU], T(L"Игра использует производительные ядра на Intel 12–14 / Core Ultra. На других "
                             L"процессорах настройка не применяется. Требует компонента исправлений игры.",
                             L"Uses performance cores on Intel 12th–14th gen / Core Ultra. Ignored on other "
                             L"CPUs. Requires Game fixes."));
        button(FPSMODE, 632, 457, 300, 34);
        auto mode = controls[FPSMODE];
        control(FPSVALUE, L"EDIT", ES_NUMBER | ES_AUTOHSCROLL, 946, 457, 112, 30,
                std::to_wstring(opts.fps ? opts.fps : 60));
        SendMessageW(controls[FPSVALUE], EM_SETLIMITTEXT, 4, 0);
        tip(mode, T(L"Авто: 144 Гц → 141 FPS; 165 Гц → 162 FPS.\nЛимит помогает оставаться в диапазоне "
                    L"G-SYNC / FreeSync, но не включает синхронизацию сам по себе и не гарантирует "
                    L"устранение тиринга.\nСвой лимит: 30–1000 FPS.",
                    L"Auto: 144 Hz → 141 FPS; 165 Hz → 162 FPS.\nHelps stay within the G-SYNC / FreeSync "
                    L"range; does not enable synchronization or guarantee tear-free output on its "
                    L"own.\nCustom limit: 30–1000 FPS."));
        button(QUALITY, 632, 521, 426, 34);
        auto quality = controls[QUALITY];
        tip(quality, T(L"Дальность детализации персонажей и мира. «Как в игре» не изменяет её; «Высокая» и "
                       L"«Ультра» сохраняют подробные модели дальше.",
                       L"Character and world detail distance. As in game leaves it unchanged; High and Ultra "
                       L"keep detailed models visible farther away."));
        button(DIAG, 632, 563, 426, 36);
        tip(controls[DIAG], T(L"Подробный журнал и тестовые клавиши для диагностики. По умолчанию выключено.",
                              L"Detailed log and test hotkeys for troubleshooting. Off by default."));
    }
    refreshEnabled();
    rebuilding = false;
    InvalidateRect(window, nullptr, TRUE);
}
std::wstring readable(const std::exception &e) {
    std::string s = e.what();
    if (s == "UNSUPPORTED_GAME")
        return T(L"Эта версия AC4BFSP.exe не поддерживается. Пакет рассчитан только на Steam (2023) с "
                 L"исходным EXE. Ничего не установлено.",
                 L"Unsupported AC4BFSP.exe. This package supports the original Steam (2023) executable only. "
                 L"Nothing was installed.");
    if (s == "GAME_NOT_FOUND")
        return T(L"Выбери папку с AC4BFSP.exe.", L"Select the folder containing AC4BFSP.exe.");
    if (s == "GAME_RUNNING")
        return T(L"Закрой игру перед установкой или удалением.",
                 L"Close the game before installing or removing the patch.");
    if (s == "CANCELLED")
        return T(L"Операция отменена. Изменения отменены.",
                 L"Operation cancelled. Changes were rolled back.");
    return T(L"Операция остановлена. Техническая информация:\n", L"Operation stopped. Technical details:\n") +
           wide(s);
}
void checkFolder() {
    valid = false;
    existing = false;
    status = T(L"Проверка файлов…", L"Checking files…");
    InvalidateRect(window, nullptr, FALSE);
    try {
        folder = fs::path(editValue(PATHBOX));
        Engine engine(package, {}, folder);
        existing = engine.installed();
        if (engine.pending()) {
            if (MessageBoxW(window,
                            T(L"Найдена незавершённая операция. Восстановить предыдущее состояние?",
                              L"An unfinished operation was found. Restore the previous state?")
                                .c_str(),
                            L"AC4 PhysX Fix", MB_YESNO | MB_ICONQUESTION) == IDYES)
                engine.recover();
            else
                throw std::runtime_error("Recovery required");
        }
        engine.validateGame();
        valid = true;
        status = existing ? T(L"Патч уже установлен. Можно изменить компоненты или удалить его.",
                              L"Patch already installed. Change components or remove it.")
                          : T(L"✓ Игра найдена · Steam (2023)", L"✓ Game found · Steam (2023)");
        if (existing) {
            auto m = engine.choices();
            opts.fixes = m.value("fixes", true);
            opts.physx = m.value("physx", true);
            opts.cpu = m.value("cpu", true);
            opts.diagnostics = m.value("diag", false);
            opts.fps = m.value("limit", 0);
            if (m.contains("preset") && m["preset"].is_number_integer())
                opts.preset = m["preset"];
            else
                opts.preset = 0;
        }
        remember(folder);
    } catch (const std::exception &e) {
        status = readable(e);
    }
    rebuild();
}
void startDiscovery() {
    if (worker.joinable())
        worker.join();
    busy = true;
    status = T(L"Поиск установленных копий игры…", L"Looking for installed copies of the game…");
    rebuild();
    worker = std::thread([]() {
        auto found = discover();
        auto result = new std::vector<fs::path>(std::move(found));
        PostMessageW(window, FOUND, 0, (LPARAM)result);
    });
}
void run(bool uninstall) {
    readOptions();
    removed = uninstall;
    cancelled = false;
    busy = true;
    percent = 0;
    step = 3;
    rebuild();
    if (worker.joinable())
        worker.join();
    worker = std::thread([uninstall]() {
        std::wstring message;
        bool ok = false;
        try {
            Context context;
            context.cancel = &cancelled;
            context.progress = [](int n, const std::wstring &s) {
                {
                    std::lock_guard<std::mutex> lock(messageMutex);
                    progressText = s;
                }
                PostMessageW(window, PROGRESS, n, 0);
            };
            Engine e(package, std::move(context), folder);
            auto r = uninstall ? e.remove(true) : e.install(opts);
            message = uninstall ? T(L"Патч удалён. Исходные файлы игры и сохранения не изменены.",
                                    L"Patch removed. Original game files and saves were not modified.")
                                : T(L"Выбранные компоненты установлены. Запускай игру обычным способом.",
                                    L"Selected components installed. Start the game as usual.");
            if (r.message == L"REMOVED_KEEP_USER")
                message += T(L"\nСуществовавший до патча User.ini сохранён.",
                             L"\nA pre-existing User.ini was kept.");
            if (!r.backup.empty())
                message += T(L"\nКопии изменённых настроек и журналов:\n",
                             L"\nCopies of changed settings and logs:\n") +
                           r.backup.wstring();
            ok = true;
        } catch (const std::exception &e) {
            message = readable(e);
        }
        auto result = new std::pair<bool, std::wstring>(ok, message);
        PostMessageW(window, DONE, 0, (LPARAM)result);
    });
}
void closeWindow() {
    if (busy && step == 3) {
        if (MessageBoxW(window,
                        T(L"Отменить операцию и вернуть предыдущее состояние?",
                          L"Cancel this operation and restore the previous state?")
                            .c_str(),
                        L"AC4 PhysX Fix", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            cancelled = true;
            status = T(L"Отмена операции…", L"Cancelling…");
        }
        return;
    }
    if (busy)
        return;
    DestroyWindow(window);
}
void browse() {
    IFileDialog *dialog = nullptr;
    if (SUCCEEDED(
            CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD flags = 0;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(T(L"Выбери папку Assassin’s Creed IV: Black Flag",
                           L"Select the Assassin’s Creed IV: Black Flag folder")
                             .c_str());
        if (!folder.empty()) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
                dialog->SetFolder(item);
                item->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(window))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                    SetWindowTextW(controls[PATHBOX], name);
                    CoTaskMemFree(name);
                    checkFolder();
                }
                item->Release();
            }
        }
        dialog->Release();
    }
}
void paint(HDC dc) {
    RECT rc;
    GetClientRect(window, &rc);
    Bitmap buffer(rc.right, rc.bottom);
    Graphics g(&buffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.Clear(Color(255, 11, 23, 31));
    if (artwork) {
        g.DrawImage(artwork.get(), R(-195, 60, 1040, 580));
    }
    LinearGradientBrush cover(PointF((REAL)S(550), 0), PointF((REAL)S(1100), 0), Color(60, 10, 23, 31),
                              Color(247, 10, 23, 31));
    g.FillRectangle(&cover, R(550, 60, 550, 660));
    SolidBrush top(Color(255, 13, 25, 33));
    g.FillRectangle(&top, R(0, 0, 1100, 60));
    textAt(g, L"AC IV  /  PHYSX FIX", 24, 19, 400, 30, 13, Color(255, 177, 198, 209));
    const wchar_t *ru[] = {L"ДОБРО ПОЖАЛОВАТЬ", L"ПАПКА ИГРЫ", L"КОМПОНЕНТЫ", L"УСТАНОВКА", L"ЗАВЕРШЕНИЕ"};
    const wchar_t *en[] = {L"WELCOME", L"GAME FOLDER", L"COMPONENTS", L"INSTALLATION", L"COMPLETE"};
    textAt(g, english ? en[step] : ru[step], 632, 88, 426, 24, 12, Color(255, 193, 166, 122));
    for (int n = 0; n < 5; n++) {
        SolidBrush b(n <= step ? Color(255, 89, 205, 221) : Color(255, 47, 69, 81));
        g.FillRectangle(&b, R(632 + (float)n * 87, 124, 78, 3));
    }
    if (step == 0) {
        textAt(g, T(L"Обнови свою игру\nPhysX Fix", L"Upgrade your game\nPhysX Fix"), 632, 197, 420, 130, 36,
               Color(255, 239, 249, 252));
        textAt(g,
               T(L"Настрой состав патча для Assassin’s Creed IV: Black Flag. Выбери компоненты и установи их "
                 L"за несколько шагов.",
                 L"Configure your patch for Assassin’s Creed IV: Black Flag. Choose components and install "
                 L"them in a few steps."),
               632, 352, 415, 120, 16);
        textAt(g, L"0.3.2 alpha · Steam (2023)", 632, 520, 420, 30, 13, Color(255, 162, 190, 201));
    }
    if (step == 1) {
        textAt(g, T(L"Где установлена игра?", L"Where is your game installed?"), 632, 158, 425, 70, 29,
               Color(255, 239, 249, 252));
        textAt(g, T(L"Папка с AC4BFSP.exe", L"Folder containing AC4BFSP.exe"), 632, 222, 420, 24, 13);
        textAt(g, status, 632, 367, 426, 100, 14,
               valid ? Color(255, 162, 233, 198) : Color(255, 223, 201, 174));
        if (existing)
            textAt(g,
                   T(L"Удаление уберёт файлы патча. Изменённые настройки и журналы будут сохранены вне папки "
                     L"игры.",
                     L"Removal deletes patch files. Changed settings and logs are backed up outside the game "
                     L"folder."),
                   632, 528, 426, 70, 12);
    }
    if (step == 2) {
        textAt(g, T(L"Выбери свои фиксы", L"Choose your fixes"), 632, 158, 426, 44, 29,
               Color(255, 239, 249, 252));
        textAt(g, T(L"Лимит FPS и синхронизация", L"FPS limit and synchronization"), 632, 433, 426, 22, 12);
        textAt(g, T(L"Дальность детализации", L"Draw distance preset"), 632, 496, 426, 22, 12);
    }
    if (step == 3) {
        textAt(g,
               removed ? T(L"Удаление патча", L"Removing patch") : T(L"Установка патча", L"Installing patch"),
               632, 173, 426, 70, 30, Color(255, 239, 249, 252));
        textAt(g,
               T(L"Проверяем файлы и сохраняем возможность отката.",
                 L"Verifying files and maintaining rollback data."),
               632, 264, 426, 70, 16);
        SolidBrush track(Color(255, 37, 59, 69)), fill(Color(255, 89, 205, 221));
        g.FillRectangle(&track, R(632, 375, 426, 8));
        g.FillRectangle(&fill, R(632, 375, 426.f * percent / 100, 8));
        textAt(g, std::to_wstring(percent) + L"%", 632, 407, 426, 60, 30, Color(255, 122, 224, 235));
        std::wstring progress;
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            progress = progressText;
        }
        textAt(g, progress, 632, 485, 426, 82, 12);
    }
    if (step == 4) {
        textAt(g,
               doneOK ? (removed ? T(L"Патч удалён", L"Patch removed")
                                 : T(L"Патч установлен", L"Patch installed"))
                      : T(L"Операция остановлена", L"Operation stopped"),
               632, 174, 426, 80, 30, Color(255, 239, 249, 252));
        textAt(g, resultText, 632, 267, 426, 190, 14);
        if (doneOK && !removed) {
            Pen line(Color(255, 76, 96, 106));
            g.DrawLine(&line, S(632), S(474), S(1058), S(474));
            textAt(g, T(L"Проект продолжает развиваться", L"The project keeps evolving"), 632, 495, 426, 34,
                   15, Color(255, 202, 176, 135));
            textAt(g,
                   T(L"Мы продолжаем совершенствовать патч. В работе — новые исправления и улучшения, а "
                     L"впереди ещё много идей. Спасибо, что играешь с нашим патчем!",
                     L"We’re continuing to refine the patch. More improvements are in progress, with plenty "
                     L"of ideas for what comes next. Thanks for playing with our patch!"),
                   632, 538, 426, 77, 13);
        }
    }
    textAt(g, T(L"0.3.2 alpha  ·  Установщик 0.3.2 native", L"0.3.2 alpha  ·  Installer 0.3.2 native"), 632, 685,
           426, 20, 11, Color(255, 145, 171, 184));
    Graphics out(dc);
    out.DrawImage(&buffer, 0, 0);
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        window = h;
        return 0;
    case WM_PRINTCLIENT:
        paint((HDC)w);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        drawButton(*(DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)w, RGB(227, 242, 248));
        SetBkColor((HDC)w, RGB(20, 43, 54));
        return (LRESULT)editBrush;
    case WM_MEASUREITEM: {
        auto item = (MEASUREITEMSTRUCT *)l;
        item->itemHeight = S(25);
        return TRUE;
    }
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        if (pt.y < S(60) && pt.x < S(890))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        readOptions();
        auto r = (RECT *)l;
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &monitor);
        scale = std::min({HIWORD(w) / 96.f, (monitor.rcWork.right - monitor.rcWork.left - 24) / 1100.f,
                          (monitor.rcWork.bottom - monitor.rcWork.top - 24) / 720.f});
        int x = std::clamp(r->left, monitor.rcWork.left,
                           std::max(monitor.rcWork.left, monitor.rcWork.right - S(1100)));
        int y = std::clamp(r->top, monitor.rcWork.top,
                           std::max(monitor.rcWork.top, monitor.rcWork.bottom - S(720)));
        SetWindowPos(h, nullptr, x, y, S(1100), S(720), SWP_NOZORDER);
        if (font)
            DeleteObject(font);
        font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        rebuild();
        return 0;
    }
    case WM_COMMAND: {
        if (rebuilding)
            return 0;
        int id = LOWORD(w), code = HIWORD(w);
        if (id == RU || id == EN) {
            if (!busy) {
                readOptions();
                english = id == EN;
                rebuild();
            }
            return 0;
        }
        if (id == CLOSE) {
            closeWindow();
            return 0;
        }
        if (busy)
            return 0;
        if (id == PATHBOX && code == EN_CHANGE) {
            valid = false;
            EnableWindow(controls[NEXT], FALSE);
            return 0;
        }
        if (id == BROWSE) {
            browse();
            return 0;
        }
        if (id == CHECK) {
            checkFolder();
            return 0;
        }
        if (id == REMOVE) {
            if (MessageBoxW(
                    h,
                    T(L"Удалить компоненты патча и созданные им настройки? Изменённые настройки и журналы "
                      L"будут сохранены вне папки игры. Сохранения и чужие моды не затрагиваются.",
                      L"Remove the patch and its settings? Changed settings and logs will be backed up "
                      L"outside the game folder. Saves and other mods are not affected.")
                        .c_str(),
                    L"AC4 PhysX Fix", MB_YESNO | MB_ICONQUESTION) == IDYES)
                run(true);
            return 0;
        }
        if (id == FPSMODE || id == QUALITY) {
            HMENU menu = CreatePopupMenu();
            int count = id == FPSMODE ? 2 : 3;
            for (int n = 0; n < count; n++) {
                auto value = id == FPSMODE ? (n == 0 ? T(L"Автоматически: частота экрана − 3 FPS",
                                                         L"Automatic: refresh rate − 3 FPS")
                                                     : T(L"Свой лимит", L"Custom limit"))
                                           : (n == 0   ? T(L"Как в игре", L"As in game")
                                              : n == 1 ? T(L"Высокая", L"High")
                                                       : T(L"Ультра", L"Ultra"));
                AppendMenuW(menu,
                            MF_STRING |
                                ((id == FPSMODE ? (int)customLimit : opts.preset) == n ? MF_CHECKED : 0),
                            n + 1, value.c_str());
            }
            RECT r;
            GetWindowRect(controls[id], &r);
            int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, h, nullptr);
            DestroyMenu(menu);
            if (choice) {
                if (id == FPSMODE) {
                    customLimit = choice == 2;
                    readOptions();
                } else
                    opts.preset = choice - 1;
                SetWindowTextW(controls[id], label(id).c_str());
                InvalidateRect(controls[id], nullptr, TRUE);
                refreshEnabled();
            }
            return 0;
        }
        if (option(id)) {
            if (id == GAME)
                opts.fixes = !opts.fixes;
            if (id == PHYS)
                opts.physx = !opts.physx;
            if (id == CPU)
                opts.cpu = !opts.cpu;
            if (id == DIAG)
                opts.diagnostics = !opts.diagnostics;
            refreshEnabled();
            InvalidateRect(controls[id], nullptr, TRUE);
            return 0;
        }
        if (id == FPSVALUE && code == EN_CHANGE) {
            refreshEnabled();
            return 0;
        }
        if (id == BACK && step > 0 && step < 3) {
            readOptions();
            --step;
            rebuild();
            return 0;
        }
        if (id == NEXT) {
            if (step == 0) {
                step = 1;
                startDiscovery();
            } else if (step == 1 && valid) {
                step = 2;
                rebuild();
            } else if (step == 2) {
                readOptions();
                run(false);
            } else if (step == 4)
                closeWindow();
            return 0;
        }
        return 0;
    }
    case FOUND: {
        std::unique_ptr<std::vector<fs::path>> results((std::vector<fs::path> *)l);
        busy = false;
        if (worker.joinable())
            worker.join();
        if (!results->empty()) {
            folder = results->front();
            rebuild();
            checkFolder();
        } else {
            status = T(L"Не удалось найти игру автоматически. Нажми «Обзор…» и укажи папку игры.",
                       L"Could not locate the game automatically. Click Browse and select its folder.");
            rebuild();
        }
        return 0;
    }
    case PROGRESS:
        percent = (int)w;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case DONE: {
        std::unique_ptr<std::pair<bool, std::wstring>> r((std::pair<bool, std::wstring> *)l);
        if (worker.joinable())
            worker.join();
        busy = false;
        doneOK = r->first;
        resultText = r->second;
        step = 4;
        rebuild();
        return 0;
    }
    case WM_CLOSE:
        closeWindow();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
} // namespace
#ifdef AC4_RENDER_TEST
void renderChecks() {
    fs::path directory = fs::current_path() / L"ui-previews";
    fs::create_directories(directory);
    UINT count = 0, size = 0;
    GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> encoders(size);
    auto codec = (ImageCodecInfo *)encoders.data();
    GetImageEncoders(count, size, codec);
    CLSID png{};
    for (UINT n = 0; n < count; n++)
        if (wcscmp(codec[n].MimeType, L"image/png") == 0)
            png = codec[n].Clsid;
    for (float testScale : {1.f, 1.5f, 2.f}) {
        scale = testScale;
        DeleteObject(font);
        font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SetWindowPos(window, nullptr, 0, 0, S(1100), S(720), SWP_NOZORDER | SWP_NOACTIVATE);
        for (bool en : {false, true})
            for (int screen : {0, 1, 2, 3, 4}) {
                english = en;
                step = screen;
                valid = true;
                existing = screen == 1;
                percent = 63;
                folder = L"D:\\Games\\Assassin's Creed IV Black Flag";
                status = T(L"✓ Игра найдена · Steam (2023)", L"✓ Game found · Steam (2023)");
                doneOK = true;
                resultText = T(L"Выбранные компоненты установлены. Запускай игру обычным способом.",
                               L"Selected components installed. Start the game as usual.");
                rebuild();
                RECT expected{};
                GetClientRect(window, &expected);
                if (expected.right != S(1100) || expected.bottom != S(720))
                    throw std::runtime_error("Window dimensions changed");
                for (auto &[id, child] : controls) {
                    RECT r{};
                    GetWindowRect(child, &r);
                    MapWindowPoints(nullptr, window, (POINT *)&r, 2);
                    if (r.left < 0 || r.top < 0 || r.right > expected.right ||
                        ((id != FPSMODE && id != QUALITY) && r.bottom > expected.bottom))
                        throw std::runtime_error("Control clipped");
                }
                HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, S(1100), S(720));
                auto old = SelectObject(mem, bmp);
                SendMessageW(window, WM_PRINT, (WPARAM)mem, PRF_CLIENT | PRF_CHILDREN);
                auto name = directory / (std::wstring(en ? L"en-" : L"ru-") + std::to_wstring(screen) +
                                         L"-dpi" + std::to_wstring((int)(testScale * 100)) + L".png");
                SelectObject(mem, old);
                {
                    Bitmap image(bmp, nullptr);
                    if (image.Save(name.c_str(), &png, nullptr) != Ok)
                        throw std::runtime_error("Preview export failed");
                }
                DeleteObject(bmp);
                DeleteDC(mem);
                ReleaseDC(window, dc);
            }
    }
    write(directory / L"result.txt",
          bytes("PASS: 30 native UI renders; RU/EN, five screens, 100/150/200 percent DPI; fixed window "
                "dimensions and control bounds verified.\n"));
}
#endif
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    // Read-only diagnostics for release verification, without running patch DLLs.
    if (wcscmp(command, L"--verify-package") == 0) {
        try {
            loadPackage();
            return 0;
        } catch (...) {
            return 2;
        }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input;
    GdiplusStartup(&gdiplus, &input, nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    try {
        loadPackage();
        loadArt();
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"AC4 PhysX Fix", MB_ICONERROR);
        return 2;
    }
    english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_RUSSIAN;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    scale = std::min({GetDpiForSystem() / 96.f, (workArea.right - workArea.left - 24) / 1100.f,
                      (workArea.bottom - workArea.top - 24) / 720.f});
    font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Segoe UI");
    editBrush = CreateSolidBrush(RGB(20, 43, 54));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"AC4NativeInstaller";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(200), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    int x = workArea.left + (workArea.right - workArea.left - S(1100)) / 2,
        y = workArea.top + (workArea.bottom - workArea.top - S(720)) / 2;
    window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"AC4 PhysX Fix — Native Installer",
                             WS_POPUP | WS_MINIMIZEBOX, x, y, S(1100), S(720), nullptr, nullptr, instance,
                             nullptr);
    rebuild();
#ifdef AC4_RENDER_TEST
    int renderResult = 0;
    try {
        renderChecks();
    } catch (const std::exception &e) {
        write(fs::current_path() / L"render-error.txt", bytes(e.what()));
        renderResult = 2;
    }
    DestroyWindow(window);
    artwork.reset();
    if (artStream)
        artStream->Release();
    DeleteObject(font);
    DeleteObject(editBrush);
    GdiplusShutdown(gdiplus);
    CoUninitialize();
    return renderResult;
#endif
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (worker.joinable())
        worker.join();
    artwork.reset();
    if (artStream)
        artStream->Release();
    DeleteObject(font);
    DeleteObject(editBrush);
    GdiplusShutdown(gdiplus);
    CoUninitialize();
    return 0;
}
