// ============================================================================
//  AdminPRO — список системных инструментов Windows с иконками их же
//  программ, тёмная/светлая тема, трей, диалог "О программе", настройки.
//
//  Сборка (MSYS2 MinGW64):
//      windres AdminPro_resource.rc -O coff -o resource.o
//      g++ -std=c++17 -municode -mwindows -O2 -static -static-libgcc -static-libstdc++ ^
//          AdminPro.cpp resource.o -o AdminPRO.exe -lshell32 -lgdi32 -luser32 -lole32 -lcomctl32
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

// ============================================================================
//  Иконки: берём иконку у самой программы, к которой ведёт плитка.
// ============================================================================
struct IconSpec {
    enum Kind { RESOLVE, STOCK } kind = RESOLVE;
    std::wstring resolveName;   // напр. L"regedit.exe" или L"devmgmt.msc"
    SHSTOCKICONID stockId = SIID_APPLICATION;
};
static IconSpec IconResolve(const wchar_t* name) { IconSpec s; s.kind = IconSpec::RESOLVE; s.resolveName = name; return s; }
static IconSpec IconStock(SHSTOCKICONID id) { IconSpec s; s.kind = IconSpec::STOCK; s.stockId = id; return s; }

static HICON LoadTileIcon(const IconSpec& spec) {
    if (spec.kind == IconSpec::STOCK) {
        SHSTOCKICONINFO sii = { sizeof(sii) };
        if (SUCCEEDED(SHGetStockIconInfo(spec.stockId, SHGSI_ICON | SHGSI_LARGEICON, &sii)))
            return sii.hIcon;
    }
    wchar_t full[MAX_PATH] = {};
    SHFILEINFOW shfi = {};
    DWORD n = SearchPathW(nullptr, spec.resolveName.c_str(), nullptr, MAX_PATH, full, nullptr);
    if (n > 0 && SHGetFileInfoW(full, 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_LARGEICON))
        return shfi.hIcon;
    // старая добрая иконка "неизвестного приложения" — так Windows показывает
    // .exe, у которого нет собственной иконки / который не нашёлся.
    SHGetFileInfoW(L"missing_app.exe", FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi),
        SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
    return shfi.hIcon;
}

// ============================================================================
//  Данные строки списка
// ============================================================================
struct Tile {
    std::wstring title, why; // why — короткое объяснение, показывается справа
    COLORREF color;
    IconSpec icon;
    HICON hIcon = nullptr;
    bool featured = false;
    std::function<void()> action;
    HWND hwnd = nullptr;
};

static std::vector<Tile> g_tiles;
static HWND g_hMain, g_hSettings = nullptr;
static HINSTANCE g_hInstReal;
static HFONT g_fontTitle, g_fontSub, g_fontFeatured, g_fontUI, g_fontCard, g_fontCardHeader;
static int g_scrollY = 0, g_contentHeight = 0, g_topBarH = 0;
static bool g_darkTheme = true;
static bool g_colorfulTiles = true;
static bool g_trayEnabled = false;
static HICON g_appIcon = nullptr;

static const int ROW_H = 54, FEATURED_H = 84, GAP = 8, MARGIN = 16;
static const int CARD_H = 200, CARD_GAP = 12;
static const UINT WM_TRAYICON = WM_APP + 1;
static const UINT ID_TRAY = 1;
static const UINT ID_CARD1 = 800, ID_CARD2 = 801, ID_CARD3 = 802;

// ============================================================================
//  Сведения о системе — берутся из реестра/WinAPI, без WMI (быстро, без COM).
// ============================================================================
static std::wstring RegReadString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buf[512] = {}; DWORD size = sizeof(buf);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return L"";
}
static DWORD RegReadDword(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD def = 0) {
    DWORD data = def, size = sizeof(data);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_DWORD, nullptr, &data, &size) == ERROR_SUCCESS) return data;
    return def;
}
static std::wstring Trim(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t");
    size_t b = s.find_last_not_of(L" \t");
    if (a == std::wstring::npos) return L"";
    return s.substr(a, b - a + 1);
}

struct SysInfo {
    std::wstring winProduct, winDisplayVersion, winBuild;
    double diskUsedFrac = 0; UINT64 diskTotalGB = 0, diskFreeGB = 0;
    std::wstring baseBoardMfr, baseBoardProduct, biosVendor, biosVersion, biosDate, sysMfr, sysProduct;
    std::wstring cpuName; UINT64 ramTotalGB = 0, ramUsedGB = 0; int ramUsedPct = 0;
    std::wstring gpuName; int diskCount = 0; UINT64 disksFreeGB = 0, disksTotalGB = 0;
    bool internetConnected = false;
};
static SysInfo g_sys;

static void GatherSysInfo() {
    const wchar_t* verKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    g_sys.winProduct = RegReadString(HKEY_LOCAL_MACHINE, verKey, L"ProductName");
    g_sys.winDisplayVersion = RegReadString(HKEY_LOCAL_MACHINE, verKey, L"DisplayVersion");
    if (g_sys.winDisplayVersion.empty()) g_sys.winDisplayVersion = RegReadString(HKEY_LOCAL_MACHINE, verKey, L"ReleaseId");
    std::wstring build = RegReadString(HKEY_LOCAL_MACHINE, verKey, L"CurrentBuild");
    DWORD ubr = RegReadDword(HKEY_LOCAL_MACHINE, verKey, L"UBR");
    g_sys.winBuild = build + L"." + std::to_wstring(ubr);

    // Windows частенько не обновляет ProductName в реестре — на многих
    // системах Windows 11 там до сих пор буквально записано "Windows 10 ...".
    // Сборки Windows 11 начинаются с 22000 — проверяем и поправляем.
    long buildNum = _wtol(build.c_str());
    if (buildNum >= 22000) {
        size_t pos = g_sys.winProduct.find(L"Windows 10");
        if (pos != std::wstring::npos) g_sys.winProduct.replace(pos, 10, L"Windows 11");
    }

    ULARGE_INTEGER freeAvail{}, total{}, totalFree{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeAvail, &total, &totalFree)) {
        g_sys.diskTotalGB = total.QuadPart / (1024ULL*1024*1024);
        g_sys.diskFreeGB = totalFree.QuadPart / (1024ULL*1024*1024);
        if (total.QuadPart > 0) g_sys.diskUsedFrac = 1.0 - (double)totalFree.QuadPart / (double)total.QuadPart;
    }

    const wchar_t* biosKey = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    g_sys.baseBoardMfr = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardManufacturer");
    g_sys.baseBoardProduct = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BaseBoardProduct");
    g_sys.biosVendor = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVendor");
    g_sys.biosVersion = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSVersion");
    g_sys.biosDate = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"BIOSReleaseDate");
    g_sys.sysMfr = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"SystemManufacturer");
    g_sys.sysProduct = RegReadString(HKEY_LOCAL_MACHINE, biosKey, L"SystemProductName");

    g_sys.cpuName = Trim(RegReadString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));

    MEMORYSTATUSEX ms = { sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        g_sys.ramTotalGB = ms.ullTotalPhys / (1024ULL*1024*1024);
        UINT64 usedBytes = ms.ullTotalPhys - ms.ullAvailPhys;
        g_sys.ramUsedGB = usedBytes / (1024ULL*1024*1024);
        g_sys.ramUsedPct = (int)ms.dwMemoryLoad;
    }

    DISPLAY_DEVICEW dd = { sizeof(dd) };
    if (EnumDisplayDevicesW(nullptr, 0, &dd, 0)) g_sys.gpuName = dd.DeviceString;

    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1 << i))) continue;
        std::wstring root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        ULARGE_INTEGER fa{}, tot{}, tf{};
        if (GetDiskFreeSpaceExW(root.c_str(), &fa, &tot, &tf)) {
            g_sys.diskCount++;
            g_sys.disksTotalGB += tot.QuadPart / (1024ULL*1024*1024);
            g_sys.disksFreeGB += tf.QuadPart / (1024ULL*1024*1024);
        }
    }

    DWORD flags = 0;
    g_sys.internetConnected = InternetGetConnectedState(&flags, 0);
}

// ---- кольцевая диаграмма (донат) для карточки диска -----------------------
static void DrawDonut(HDC hdc, RECT area, double usedFrac, COLORREF fg, COLORREF bg, const std::wstring& centerText, HFONT font, COLORREF textColor) {
    int size = std::min(area.right - area.left, area.bottom - area.top);
    int cx = area.left + (area.right - area.left) / 2;
    int cy = area.top + (area.bottom - area.top) / 2;
    int r = size / 2 - 8;
    int penW = std::max(6, size / 12);

    HPEN penBg = CreatePen(PS_SOLID, penW, bg);
    HPEN old = (HPEN)SelectObject(hdc, penBg);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    MoveToEx(hdc, cx + r, cy, nullptr); // важно: иначе AngleArc рисует лишнюю диагональ
    AngleArc(hdc, cx, cy, r, 0, 360);
    SelectObject(hdc, old);
    DeleteObject(penBg);

    if (usedFrac > 0.002) {
        HPEN penFg = CreatePen(PS_SOLID, penW, fg);
        old = (HPEN)SelectObject(hdc, penFg);
        MoveToEx(hdc, cx, cy - r, nullptr); // начало дуги — угол 90°
        AngleArc(hdc, cx, cy, r, 90, -(float)(std::min(1.0, usedFrac) * 360.0));
        SelectObject(hdc, old);
        DeleteObject(penFg);
    }

    RECT textRc = { cx - r, cy - 12, cx + r, cy + 12 };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    SelectObject(hdc, font);
    DrawTextW(hdc, centerText.c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ============================================================================
//  Запуск программ / команд
// ============================================================================
static void ShowLaunchError(const std::wstring& what) {
    MessageBoxW(g_hMain, (L"Не удалось запустить: " + what).c_str(), L"AdminPRO", MB_ICONERROR);
}
static std::wstring UserHome() {
    wchar_t buf[MAX_PATH]; ExpandEnvironmentStringsW(L"%USERPROFILE%", buf, MAX_PATH); return buf;
}
static void Launch(const std::wstring& file, const std::wstring& params = L"", const std::wstring& dir = L"") {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", file.c_str(),
        params.empty() ? nullptr : params.c_str(),
        dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) ShowLaunchError(file);
}
static void LaunchAdmin(const std::wstring& file, const std::wstring& params = L"", const std::wstring& dir = L"") {
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", file.c_str(),
        params.empty() ? nullptr : params.c_str(),
        dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) ShowLaunchError(file);
}
static void LaunchPowerShellAdmin(const std::wstring& cmd, bool hidden = true) {
    std::wstring params = (hidden ? L"-NoProfile -WindowStyle Hidden -Command \"" : L"-NoProfile -Command \"") + cmd + L"\"";
    LaunchAdmin(L"powershell.exe", params, UserHome());
}
static bool Confirm(const std::wstring& text) {
    return MessageBoxW(g_hMain, text.c_str(), L"AdminPRO — подтверждение",
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
}

// ---- очистка временных папок ---------------------------------------------
static void DeleteDirectoryContents(const std::wstring& dir) {
    std::wstring search = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { DeleteDirectoryContents(full); RemoveDirectoryW(full.c_str()); }
        else { SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(full.c_str()); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void CleanTemp(const std::wstring& label, const std::wstring& path) {
    if (!Confirm(L"Очистить содержимое папки?\n\n" + path + L"\n\nЗанятые системой файлы будут пропущены."))
        return;
    DeleteDirectoryContents(path);
    MessageBoxW(g_hMain, (L"Готово: " + label).c_str(), L"AdminPRO", MB_ICONINFORMATION);
}
static std::wstring ExpandEnv(const wchar_t* s) { wchar_t buf[MAX_PATH]; ExpandEnvironmentStringsW(s, buf, MAX_PATH); return buf; }

// ---- самоудаление -----------------------------------------------------------
static bool WriteAllBytes(const std::wstring& path, const std::string& content) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written; WriteFile(h, content.data(), (DWORD)content.size(), &written, nullptr);
    CloseHandle(h);
    return true;
}
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}
static void SelfDeleteAndExit() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring batPath = std::wstring(tempDir) + L"adminpro_uninstall.bat";

    std::wstring bat =
        L"@echo off\r\n"
        L":retry\r\n"
        L"del /f /q \"" + std::wstring(exePath) + L"\" >nul 2>nul\r\n"
        L"if exist \"" + std::wstring(exePath) + L"\" (timeout /t 1 /nobreak >nul & goto retry)\r\n"
        L"(goto) 2>nul & del /f /q \"%~f0\"\r\n";

    WriteAllBytes(batPath, WToUtf8(bat));
    ShellExecuteW(nullptr, L"open", L"cmd.exe", (L"/c \"" + batPath + L"\"").c_str(), nullptr, SW_HIDE);
    PostQuitMessage(0);
}

// ============================================================================
//  Список инструментов
// ============================================================================
static void BuildTiles() {
    auto add = [](const wchar_t* title, const wchar_t* why, COLORREF c, IconSpec icon, std::function<void()> fn, bool featured = false) {
        Tile t; t.title = title; t.why = why; t.color = c; t.icon = icon; t.action = fn; t.featured = featured;
        g_tiles.push_back(t);
    };

    const COLORREF CLEAN = RGB(0xE8,0x7A,0x22), SYSTEM_ = RGB(0x1B,0xA1,0xE2), RETRO = RGB(0x7E,0x3F,0x98);
    const COLORREF DANGER = RGB(0xC5,0x1B,0x2B), ACCESS = RGB(0x2E,0xA0,0x44), TERM = RGB(0x3A,0x3A,0x3A);
    const COLORREF FUN = RGB(0xD6,0x3A,0x8C);

    // --- диспетчер задач (теперь обычная строка списка) -----------------------
    add(L"Диспетчер задач", L"Закрыть зависшую программу", SYSTEM_,
        IconResolve(L"taskmgr.exe"), [] { Launch(L"taskmgr.exe"); });

    // --- очистка -------------------------------------------------------------
    add(L"Очистить Temp (системный)", L"Освободить место, ускорить систему", CLEAN, IconResolve(L"explorer.exe"),
        [] { CleanTemp(L"C:\\Windows\\Temp", L"C:\\Windows\\Temp"); });
    add(L"Очистить Temp пользователя", L"Убрать мусор от программ", CLEAN, IconResolve(L"explorer.exe"),
        [] { CleanTemp(L"Temp пользователя", ExpandEnv(L"%LOCALAPPDATA%\\Temp")); });
    add(L"Очистка диска", L"Мастер очистки от Windows", CLEAN, IconResolve(L"cleanmgr.exe"), [] { Launch(L"cleanmgr.exe"); });
    add(L"Открыть диск C:", L"Быстрый доступ к файлам", CLEAN, IconResolve(L"explorer.exe"), [] { Launch(L"C:\\\\"); });

    // --- система ---------------------------------------------------------------
    add(L"Regedit", L"Тонкая настройка системы", SYSTEM_, IconResolve(L"regedit.exe"), [] { Launch(L"regedit.exe"); });
    add(L"Параметры", L"Основные настройки Windows", SYSTEM_, IconResolve(L"explorer.exe"), [] { Launch(L"ms-settings:"); });
    add(L"Диспетчер устройств", L"Проверить драйверы и оборудование", SYSTEM_, IconResolve(L"devmgmt.msc"), [] { Launch(L"devmgmt.msc"); });
    add(L"Управление дисками", L"Разделы, форматирование", SYSTEM_, IconResolve(L"diskmgmt.msc"), [] { Launch(L"diskmgmt.msc"); });
    add(L"Планировщик заданий", L"Автозапуск по расписанию", SYSTEM_, IconResolve(L"taskschd.msc"), [] { Launch(L"taskschd.msc"); });
    add(L"Локальные пользователи", L"Учётные записи (нет в Home)", SYSTEM_, IconResolve(L"lusrmgr.msc"), [] { Launch(L"lusrmgr.msc"); });
    add(L"Групповая политика", L"Расширенные ограничения (нет в Home)", SYSTEM_, IconResolve(L"gpedit.msc"), [] { Launch(L"gpedit.msc"); });
    add(L"Просмотр событий", L"Найти причину сбоя", SYSTEM_, IconResolve(L"eventvwr.msc"), [] { Launch(L"eventvwr.msc"); });
    add(L"Службы", L"Включить/выключить фоновые процессы", SYSTEM_, IconResolve(L"services.msc"), [] { Launch(L"services.msc"); });
    add(L"Сведения о системе", L"Полная информация о ПК", SYSTEM_, IconResolve(L"msinfo32.exe"), [] { Launch(L"msinfo32.exe"); });
    add(L"Диагностика DirectX", L"Проверить видео/звук перед игрой", SYSTEM_, IconResolve(L"dxdiag.exe"), [] { Launch(L"dxdiag.exe"); });
    add(L"Монитор ресурсов", L"Что грузит диск/сеть/CPU", SYSTEM_, IconResolve(L"resmon.exe"), [] { Launch(L"resmon.exe"); });
    add(L"Монитор производительности", L"Графики нагрузки во времени", SYSTEM_, IconResolve(L"perfmon.exe"), [] { Launch(L"perfmon.exe"); });
    add(L"Диагностика памяти", L"Проверить ОЗУ на ошибки", SYSTEM_, IconResolve(L"mdsched.exe"), [] { Launch(L"mdsched.exe"); });
    add(L"Устранение неполадок", L"Автопоиск и исправление проблем", SYSTEM_, IconResolve(L"control.exe"),
        [] { Launch(L"control.exe", L"/name Microsoft.Troubleshooting"); });
    add(L"Автозагрузка", L"Что тормозит запуск Windows", SYSTEM_, IconResolve(L"explorer.exe"),
        [] { Launch(L"ms-settings:startupapps"); });
    add(L"Сетевые подключения", L"Wi-Fi/Ethernet адаптеры", SYSTEM_, IconResolve(L"ncpa.cpl"), [] { Launch(L"ncpa.cpl"); });
    add(L"Свойства системы", L"Имя ПК, характеристики, защита", SYSTEM_, IconResolve(L"sysdm.cpl"), [] { Launch(L"sysdm.cpl"); });
    add(L"Программы и компоненты", L"Удалить установленную программу", SYSTEM_, IconResolve(L"appwiz.cpl"), [] { Launch(L"appwiz.cpl"); });
    add(L"Активация Windows", L"Проверить/ввести лицензионный ключ", SYSTEM_, IconResolve(L"slui.exe"), [] { Launch(L"slui.exe"); });
    add(L"Перезагрузка в BIOS/UEFI", L"Настройки материнской платы", DANGER, IconStock(SIID_WARNING), [] {
        if (Confirm(L"Компьютер перезагрузится прямо сейчас в настройки BIOS/UEFI.\nПродолжить?"))
            Launch(L"shutdown.exe", L"/r /fw /t 0");
    });

    // --- ретро (со времён Vista/7) --------------------------------------------
    add(L"Steps Recorder", L"Записать шаги для техподдержки", RETRO, IconResolve(L"psr.exe"), [] { Launch(L"psr.exe"); });
    add(L"Архивация (Win7-стиль)", L"Старый мастер резервных копий", RETRO, IconResolve(L"sdclt.exe"),
        [] { Launch(L"control.exe", L"/name Microsoft.BackupAndRestore"); });
    add(L"MRT", L"Разовая проверка на вирусы", RETRO, IconResolve(L"mrt.exe"), [] { Launch(L"mrt.exe"); });
    add(L"Архивация паролей", L"Сохранённые пароли Windows", RETRO, IconResolve(L"credwiz.exe"), [] { Launch(L"credwiz.exe"); });
    add(L"Центр мобильности", L"Быстрые настройки (только ноутбуки)", RETRO, IconResolve(L"mblctr.exe"), [] { Launch(L"mblctr.exe"); });
    add(L"IExpress", L"Собрать самораспаковывающийся архив", RETRO, IconResolve(L"iexpress.exe"), [] { Launch(L"iexpress.exe"); });
    add(L"О программе (winver)", L"Какая у тебя версия Windows", RETRO, IconResolve(L"winver.exe"), [] { Launch(L"winver.exe"); });

    // --- точки восстановления -------------------------------------------------
    add(L"Создать точку восстановления", L"Подстраховка перед изменениями", SYSTEM_, IconResolve(L"explorer.exe"), [] {
        if (Confirm(L"Создать новую точку восстановления системы?"))
            LaunchPowerShellAdmin(L"Checkpoint-Computer -Description 'AdminPRO' -RestorePointType MODIFY_SETTINGS");
    });
    add(L"Удалить точки восстановления", L"Освободить место (необратимо)", DANGER, IconStock(SIID_WARNING), [] {
        if (Confirm(L"Это НЕОБРАТИМО удалит ВСЕ точки восстановления на диске C:.\nПродолжить?"))
            LaunchAdmin(L"vssadmin.exe", L"delete shadows /for=C: /all /quiet");
    });

    // --- терминалы -----------------------------------------------------------
    add(L"CMD", L"Обычные права, без админа", TERM, IconResolve(L"cmd.exe"), [] {
        // запуск через explorer.exe гарантированно даёт обычные (не админские)
        // права, даже если сам AdminPRO запущен от администратора.
        Launch(L"explorer.exe", L"cmd.exe");
    });
    add(L"CMD (администратор)", L"С правами админа", TERM, IconResolve(L"cmd.exe"), [] { LaunchAdmin(L"cmd.exe", L"", UserHome()); });
    add(L"PowerShell (администратор)", L"С правами админа", TERM, IconResolve(L"powershell.exe"),
        [] { LaunchAdmin(L"powershell.exe", L"", UserHome()); });

    // --- защитник --------------------------------------------------------------
    add(L"Отключить Защитник", L"Временно, до перезагрузки", DANGER, IconStock(SIID_WARNING), [] {
        if (Confirm(L"Реальное время защиты Защитника Windows будет ВЫКЛЮЧЕНО.\nПродолжить?"))
            LaunchPowerShellAdmin(L"Set-MpPreference -DisableRealtimeMonitoring $true");
    });
    add(L"Включить Защитник обратно", L"Вернуть защиту", ACCESS, IconResolve(L"explorer.exe"),
        [] { LaunchPowerShellAdmin(L"Set-MpPreference -DisableRealtimeMonitoring $false"); });

    // --- спец. возможности и утилиты -------------------------------------------
    add(L"Блокнот", L"Быстрая заметка", ACCESS, IconResolve(L"notepad.exe"), [] { Launch(L"notepad.exe"); });
    add(L"Калькулятор", L"Посчитать", ACCESS, IconResolve(L"calc.exe"), [] { Launch(L"calc.exe"); });
    add(L"Paint", L"Быстро отредактировать картинку", ACCESS, IconResolve(L"mspaint.exe"), [] { Launch(L"mspaint.exe"); });
    add(L"Ножницы", L"Снимок экрана", ACCESS, IconResolve(L"SnippingTool.exe"), [] { Launch(L"SnippingTool.exe"); });
    add(L"Экранная клавиатура", L"Если не работает физическая", ACCESS, IconResolve(L"osk.exe"), [] { Launch(L"osk.exe"); });
    add(L"Экранная лупа", L"Увеличить часть экрана", ACCESS, IconResolve(L"magnify.exe"), [] { Launch(L"magnify.exe"); });
    add(L"Таблица символов", L"Найти редкий символ/эмодзи", ACCESS, IconResolve(L"charmap.exe"), [] { Launch(L"charmap.exe"); });

    // --- приколюшки -------------------------------------------------------------
    add(L"Roll рулит", L"Маленький сюрприз в консоли", FUN, IconResolve(L"cmd.exe"),
        [] { Launch(L"cmd.exe", L"/c \"color 0a && echo. && echo   ROLL RULES && timeout /t 3\"", UserHome()); });
    add(L"Пасхалка: цвет консоли", L"Рандомный цвет CMD", FUN, IconResolve(L"cmd.exe"), [] {
        int c = (rand() % 15) + 1;
        wchar_t buf[64]; swprintf_s(buf, L"/c \"color 0%X && echo Сюрприз! && pause\"", c);
        Launch(L"cmd.exe", buf, UserHome());
    });

    // --- ещё полезное -----------------------------------------------------------
    add(L"Диспетчер печати", L"Очередь печати, принтеры", SYSTEM_, IconResolve(L"printmanagement.msc"),
        [] { Launch(L"printmanagement.msc"); });
    add(L"Оптимизация дисков", L"Дефрагментация/TRIM", SYSTEM_, IconResolve(L"dfrgui.exe"), [] { Launch(L"dfrgui.exe"); });
    add(L"Электропитание", L"Схемы питания, спящий режим", SYSTEM_, IconResolve(L"powercfg.cpl"), [] { Launch(L"powercfg.cpl"); });
    add(L"Bluetooth и устройства", L"Подключить наушники/мышь", SYSTEM_, IconResolve(L"explorer.exe"),
        [] { Launch(L"ms-settings:bluetooth"); });
    add(L"Проверка диска (chkdsk)", L"Найти ошибки на диске C:", SYSTEM_, IconResolve(L"cmd.exe"),
        [] { LaunchAdmin(L"cmd.exe", L"/k chkdsk C:"); });
    add(L"Отчёт о батарее", L"Износ аккумулятора (ноутбуки)", SYSTEM_, IconResolve(L"cmd.exe"), [] {
        std::wstring home = UserHome();
        LaunchAdmin(L"cmd.exe",
            L"/c \"powercfg /batteryreport /output \"" + home + L"\\battery-report.html\" && start \"\" \"" + home + L"\\battery-report.html\"\"");
    });
    add(L"Центр обновления Windows", L"Проверить/поставить обновления", SYSTEM_, IconResolve(L"explorer.exe"),
        [] { Launch(L"ms-settings:windowsupdate"); });
}

// ============================================================================
//  Карточки со сведениями о системе — сверху, не прокручиваются.
// ============================================================================
static HWND g_hCard1, g_hCard2, g_hCard3;
static HWND g_hList; // отдельное дочернее окно со своей прокруткой —
                      // физически ограничено областью НИЖЕ карточек,
                      // поэтому строки списка никогда не наезжают на них.

static void LayoutCards(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int totalW = rc.right - rc.left - MARGIN * 2;
    int cardW = (totalW - CARD_GAP * 2) / 3;
    int y = MARGIN;
    if (g_hCard1) MoveWindow(g_hCard1, MARGIN, y, cardW, CARD_H, TRUE);
    if (g_hCard2) MoveWindow(g_hCard2, MARGIN + cardW + CARD_GAP, y, cardW, CARD_H, TRUE);
    if (g_hCard3) MoveWindow(g_hCard3, MARGIN + (cardW + CARD_GAP) * 2, y, totalW - (cardW + CARD_GAP) * 2, CARD_H, TRUE);
    g_topBarH = MARGIN + CARD_H + MARGIN;

    if (g_hList) MoveWindow(g_hList, 0, g_topBarH, rc.right - rc.left, std::max(0, (int)(rc.bottom - rc.top) - g_topBarH), TRUE);
}

// ============================================================================
//  Раскладка списка (одна колонка, строки) — работает внутри g_hList,
//  своя система координат, начинается с 0 (а не с g_topBarH).
// ============================================================================
static int RowHeight(const Tile& t) { return t.featured ? FEATURED_H : ROW_H; }

static void LayoutTiles(HWND listHwnd) {
    RECT rc; GetClientRect(listHwnd, &rc);
    int w = rc.right - rc.left - MARGIN * 2;
    int y = MARGIN - g_scrollY;
    for (auto& t : g_tiles) {
        if (t.hwnd) MoveWindow(t.hwnd, MARGIN, y, w, RowHeight(t), TRUE);
        y += RowHeight(t) + GAP;
    }
    g_contentHeight = y + g_scrollY;

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, g_contentHeight - 1);
    si.nPage = std::max(1, (int)(rc.bottom - rc.top));
    si.nPos = g_scrollY;
    SetScrollInfo(listHwnd, SB_VERT, &si, TRUE);
}
static void OnScroll(HWND listHwnd, int delta) {
    RECT rc; GetClientRect(listHwnd, &rc);
    int maxScroll = std::max(0, g_contentHeight - (int)(rc.bottom - rc.top));
    g_scrollY = std::max(0, std::min(maxScroll, g_scrollY + delta));
    LayoutTiles(listHwnd);
    InvalidateRect(listHwnd, nullptr, TRUE);
}

// ============================================================================
//  WndProc контейнера списка — сам скроллится, а рисование/клики строк
//  и правый клик просто пересылает в главное окно (там уже есть вся логика).
// ============================================================================
static LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        LayoutTiles(hwnd);
        return 0;
    case WM_VSCROLL: {
        int delta = 0;
        switch (LOWORD(wp)) {
        case SB_LINEUP: delta = -40; break;
        case SB_LINEDOWN: delta = 40; break;
        case SB_PAGEUP: delta = -300; break;
        case SB_PAGEDOWN: delta = 300; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
            GetScrollInfo(hwnd, SB_VERT, &si);
            g_scrollY = si.nTrackPos; LayoutTiles(hwnd); InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }}
        OnScroll(hwnd, delta);
        return 0;
    }
    case WM_MOUSEWHEEL:
        OnScroll(hwnd, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -60 : 60);
        return 0;
    case WM_DRAWITEM:
        return SendMessageW(g_hMain, WM_DRAWITEM, wp, lp);
    case WM_COMMAND:
        return SendMessageW(g_hMain, WM_COMMAND, wp, lp);
    case WM_CONTEXTMENU:
        return SendMessageW(g_hMain, WM_CONTEXTMENU, wp, lp);
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(g_darkTheme ? RGB(24,24,26) : RGB(240,240,242));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================================
//  Отрисовка карточек сведений о системе
// ============================================================================
static void CardHeader(HDC hdc, RECT& rc, const wchar_t* title) {
    RECT titleRc = rc; titleRc.left += 14; titleRc.top += 10; titleRc.right -= 10; titleRc.bottom = titleRc.top + 26;
    SelectObject(hdc, g_fontCardHeader);
    SetTextColor(hdc, g_darkTheme ? RGB(255,255,255) : RGB(20,20,20));
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, title, -1, &titleRc, DT_LEFT | DT_NOPREFIX);
    rc.top = titleRc.bottom + 6;
}
static void CardLine(HDC hdc, RECT& rc, const std::wstring& text) {
    RECT lineRc = rc; lineRc.left += 14; lineRc.right -= 10; lineRc.bottom = lineRc.top + 22;
    SelectObject(hdc, g_fontCard);
    SetTextColor(hdc, g_darkTheme ? RGB(210,210,215) : RGB(60,60,65));
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &lineRc, DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS | DT_SINGLELINE);
    rc.top = lineRc.bottom + 3;
}
static void DrawCardBg(HDC hdc, RECT rc, bool pressed) {
    COLORREF bg = g_darkTheme ? RGB(34,34,38) : RGB(255,255,255);
    if (pressed) bg = RGB(GetRValue(bg)*0.85, GetGValue(bg)*0.85, GetBValue(bg)*0.85);
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

static void DrawCard1(LPDRAWITEMSTRUCT dis) { // О системе
    HDC hdc = dis->hDC; RECT rc = dis->rcItem;
    DrawCardBg(hdc, rc, dis->itemState & ODS_SELECTED);
    RECT textArea = rc; textArea.right -= (rc.bottom - rc.top) - 6; // место под донат справа + запас
    RECT cursor = textArea;
    CardHeader(hdc, cursor, L"О системе");
    CardLine(hdc, cursor, g_sys.winProduct.empty() ? L"Windows" : g_sys.winProduct);
    CardLine(hdc, cursor, L"Версия " + (g_sys.winDisplayVersion.empty() ? L"—" : g_sys.winDisplayVersion));
    CardLine(hdc, cursor, L"Сборка " + g_sys.winBuild);
    CardLine(hdc, cursor, L"Свободно: " + std::to_wstring(g_sys.diskFreeGB) + L" из " + std::to_wstring(g_sys.diskTotalGB) + L" ГБ");

    RECT donutRc = rc; donutRc.left = textArea.right + 10;
    wchar_t pct[8]; swprintf_s(pct, L"%d%%", (int)(g_sys.diskUsedFrac * 100));
    DrawDonut(hdc, donutRc, g_sys.diskUsedFrac, RGB(0x1B,0xA1,0xE2),
        g_darkTheme ? RGB(60,60,64) : RGB(225,225,228), pct, g_fontCardHeader,
        g_darkTheme ? RGB(255,255,255) : RGB(20,20,20));
}
static void DrawCard2(LPDRAWITEMSTRUCT dis) { // О BIOS / материнке
    HDC hdc = dis->hDC; RECT rc = dis->rcItem;
    DrawCardBg(hdc, rc, dis->itemState & ODS_SELECTED);
    RECT cursor = rc;
    CardHeader(hdc, cursor, L"О BIOS и плате");
    CardLine(hdc, cursor, (g_sys.sysMfr.empty() ? L"" : g_sys.sysMfr + L" ") + (g_sys.sysProduct.empty() ? L"" : g_sys.sysProduct));
    CardLine(hdc, cursor, L"Плата: " + (g_sys.baseBoardMfr.empty() ? L"—" : g_sys.baseBoardMfr));
    CardLine(hdc, cursor, (g_sys.baseBoardProduct.empty() ? L"" : g_sys.baseBoardProduct));
    CardLine(hdc, cursor, L"BIOS: " + (g_sys.biosVendor.empty() ? L"—" : g_sys.biosVendor));
    CardLine(hdc, cursor, L"Версия " + (g_sys.biosVersion.empty() ? L"—" : g_sys.biosVersion) + L", " + g_sys.biosDate);
}
static void DrawCard3(LPDRAWITEMSTRUCT dis) { // О компьютере
    HDC hdc = dis->hDC; RECT rc = dis->rcItem;
    DrawCardBg(hdc, rc, dis->itemState & ODS_SELECTED);
    RECT cursor = rc;
    CardHeader(hdc, cursor, L"О компьютере");
    CardLine(hdc, cursor, g_sys.cpuName.empty() ? L"CPU: —" : g_sys.cpuName);
    CardLine(hdc, cursor, L"ОЗУ: " + std::to_wstring(g_sys.ramUsedGB) + L" / " + std::to_wstring(g_sys.ramTotalGB) +
        L" ГБ (" + std::to_wstring(g_sys.ramUsedPct) + L"%)");
    CardLine(hdc, cursor, g_sys.gpuName.empty() ? L"GPU: —" : g_sys.gpuName);
    CardLine(hdc, cursor, L"Диски: " + std::to_wstring(g_sys.diskCount) + L", свободно " + std::to_wstring(g_sys.disksFreeGB) + L" ГБ");
    CardLine(hdc, cursor, g_sys.internetConnected ? L"Интернет: подключен" : L"Интернет: нет подключения");
}

// ============================================================================
//  Отрисовка строки
// ============================================================================
static void DrawTile(LPDRAWITEMSTRUCT dis, const Tile& t) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF bg = g_darkTheme ? RGB(38,38,40) : RGB(255,255,255);
    COLORREF txt = g_darkTheme ? RGB(240,240,240) : RGB(20,20,20);
    COLORREF sub = g_darkTheme ? RGB(170,170,175) : RGB(90,90,95);

    if (t.featured) bg = g_colorfulTiles ? t.color : (g_darkTheme ? RGB(50,50,54) : RGB(230,230,230));
    if (pressed) bg = RGB(GetRValue(bg)*0.8, GetGValue(bg)*0.8, GetBValue(bg)*0.8);

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    if (g_colorfulTiles) {
        RECT accent = rc; accent.right = accent.left + (t.featured ? 6 : 4);
        HBRUSH ab = CreateSolidBrush(t.color);
        FillRect(hdc, &accent, ab);
        DeleteObject(ab);
    }

    if (t.featured) txt = RGB(255,255,255);

    int iconSize = t.featured ? 40 : 30;
    int iconX = rc.left + 18, iconY = rc.top + (rc.bottom - rc.top - iconSize) / 2;
    if (t.hIcon) DrawIconEx(hdc, iconX, iconY, t.hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);

    SetBkMode(hdc, TRANSPARENT);

    RECT whyRc = rc; whyRc.right -= 16; whyRc.left = whyRc.right - 220;
    SelectObject(hdc, g_fontSub);
    SetTextColor(hdc, t.featured ? RGB(240,240,240) : sub);
    DrawTextW(hdc, t.why.c_str(), -1, &whyRc, DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER | DT_SINGLELINE);

    RECT titleRc = rc;
    titleRc.left = iconX + iconSize + 14;
    titleRc.right = whyRc.left - 10;
    SelectObject(hdc, t.featured ? g_fontFeatured : g_fontTitle);
    SetTextColor(hdc, txt);
    DrawTextW(hdc, t.title.c_str(), -1, &titleRc, DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS | DT_VCENTER | DT_SINGLELINE);
}

// ============================================================================
//  Диалог "О программе"
// ============================================================================
static void ShowAbout() {
    MessageBoxW(g_hMain,
        L"AdminPRO\n\nВерсия: BETA\nТип: Установщик\n\nЛаунчер системных инструментов Windows.",
        L"О программе", MB_ICONINFORMATION | MB_OK);
}

// ============================================================================
//  Окно настроек (вкладки: Вид / Настройки)
// ============================================================================
static HWND g_tab, g_rbLight, g_rbDark, g_cbColorful, g_btnUninstall;
static HWND g_viewPanel, g_settingsPanel;

static void ApplySettingsFromControls() {
    g_darkTheme = (Button_GetCheck(g_rbDark) == BST_CHECKED);
    g_colorfulTiles = (Button_GetCheck(g_cbColorful) == BST_CHECKED);
    InvalidateRect(g_hMain, nullptr, TRUE);
}

static void ShowSettingsTab(int idx) {
    ShowWindow(g_viewPanel, idx == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_settingsPanel, idx == 1 ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE,
            10, 10, 360, 260, hwnd, nullptr, g_hInstReal, nullptr);
        SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        TCITEMW tie = {}; tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)L"Вид";      TabCtrl_InsertItem(g_tab, 0, &tie);
        tie.pszText = (LPWSTR)L"Настройки"; TabCtrl_InsertItem(g_tab, 1, &tie);

        g_viewPanel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            20, 44, 340, 210, hwnd, nullptr, g_hInstReal, nullptr);
        g_rbLight = CreateWindowExW(0, L"BUTTON", L"Светлая тема",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 0, 200, 24, g_viewPanel, nullptr, g_hInstReal, nullptr);
        g_rbDark = CreateWindowExW(0, L"BUTTON", L"Тёмная тема",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 30, 200, 24, g_viewPanel, nullptr, g_hInstReal, nullptr);
        g_cbColorful = CreateWindowExW(0, L"BUTTON", L"Цветные плитки",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 70, 250, 24, g_viewPanel, nullptr, g_hInstReal, nullptr);
        SendMessageW(g_rbLight, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(g_rbDark, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        SendMessageW(g_cbColorful, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        Button_SetCheck(g_darkTheme ? g_rbDark : g_rbLight, BST_CHECKED);
        Button_SetCheck(g_cbColorful, g_colorfulTiles ? BST_CHECKED : BST_UNCHECKED);

        g_settingsPanel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
            20, 44, 340, 210, hwnd, nullptr, g_hInstReal, nullptr);
        HWND lbl = CreateWindowExW(0, L"STATIC",
            L"Удаление AdminPRO необратимо и потребует\nдвойного подтверждения.",
            WS_CHILD | WS_VISIBLE, 0, 0, 320, 40, g_settingsPanel, nullptr, g_hInstReal, nullptr);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        g_btnUninstall = CreateWindowExW(0, L"BUTTON", L"Удалить программу",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 50, 200, 32, g_settingsPanel, (HMENU)1, g_hInstReal, nullptr);
        SendMessageW(g_btnUninstall, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

        ShowSettingsTab(0);
        return 0;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->hwndFrom == g_tab && nm->code == TCN_SELCHANGE)
            ShowSettingsTab(TabCtrl_GetCurSel(g_tab));
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (HIWORD(wp) == BN_CLICKED) {
            if (id == 1) { // удалить программу
                if (Confirm(L"Точно удалить AdminPRO?\nЭто действие необратимо.") &&
                    Confirm(L"Последнее подтверждение: удалить программу СЕЙЧАС?\nПрограмма закроется и файл будет стёрт."))
                {
                    DestroyWindow(hwnd);
                    SelfDeleteAndExit();
                }
            } else {
                ApplySettingsFromControls();
            }
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_hSettings = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenSettings() {
    if (g_hSettings) { SetForegroundWindow(g_hSettings); return; }
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = g_hInstReal;
    wc.lpszClassName = L"AdminProSettingsClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    g_hSettings = CreateWindowExW(0, L"AdminProSettingsClass", L"Дополнительные параметры — AdminPRO",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 320, g_hMain, nullptr, g_hInstReal, nullptr);
    ShowWindow(g_hSettings, SW_SHOW);
}

// ============================================================================
//  Трей
// ============================================================================
static void AddTray() {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = g_hMain; nid.uID = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"AdminPRO");
    Shell_NotifyIconW(NIM_ADD, &nid);
}
static void RemoveTray() {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = g_hMain; nid.uID = ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ============================================================================
//  Главное окно
// ============================================================================
static void LoadAppIcon() {
    wchar_t exeDir[MAX_PATH];
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    std::wstring p(exeDir);
    size_t pos = p.find_last_of(L"\\/");
    std::wstring icoPath = (pos == std::wstring::npos ? L"." : p.substr(0, pos)) + L"\\AdminPRO.ico";
    g_appIcon = (HICON)LoadImageW(nullptr, icoPath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        LoadAppIcon();
        if (g_appIcon) { SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon); SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIcon); }

        g_fontTitle = CreateFontW(-16, 0,0,0, FW_SEMIBOLD, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        g_fontFeatured = CreateFontW(-19, 0,0,0, FW_BOLD, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        g_fontSub = CreateFontW(-12, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        g_fontCardHeader = CreateFontW(-22, 0,0,0, FW_BOLD, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        g_fontCard = CreateFontW(-15, 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        g_fontUI = g_fontTitle;

        GatherSysInfo();
        g_hCard1 = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,0,10,10, hwnd, (HMENU)(UINT_PTR)ID_CARD1, g_hInstReal, nullptr);
        g_hCard2 = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,0,10,10, hwnd, (HMENU)(UINT_PTR)ID_CARD2, g_hInstReal, nullptr);
        g_hCard3 = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,0,10,10, hwnd, (HMENU)(UINT_PTR)ID_CARD3, g_hInstReal, nullptr);

        WNDCLASSEXW lwc = { sizeof(lwc) };
        lwc.lpfnWndProc = ListProc;
        lwc.hInstance = g_hInstReal;
        lwc.lpszClassName = L"AdminProListClass";
        lwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&lwc);
        g_hList = CreateWindowExW(0, L"AdminProListClass", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL,
            0, 0, 10, 10, hwnd, nullptr, g_hInstReal, nullptr);

        BuildTiles();
        for (size_t i = 0; i < g_tiles.size(); ++i) {
            g_tiles[i].hIcon = LoadTileIcon(g_tiles[i].icon);
            g_tiles[i].hwnd = CreateWindowExW(0, L"BUTTON", g_tiles[i].title.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, g_hList, (HMENU)(UINT_PTR)(1000 + i), g_hInstReal, nullptr);
        }
        LayoutCards(hwnd);
        LayoutTiles(g_hList);
        return 0;
    }

    case WM_SIZE:
        LayoutCards(hwnd); // сама пересчитывает и переставляет g_hList
        return 0;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->CtlID == ID_CARD1) { DrawCard1(dis); return TRUE; }
        if (dis->CtlID == ID_CARD2) { DrawCard2(dis); return TRUE; }
        if (dis->CtlID == ID_CARD3) { DrawCard3(dis); return TRUE; }
        int idx = (int)dis->CtlID - 1000;
        if (idx >= 0 && idx < (int)g_tiles.size()) DrawTile(dis, g_tiles[idx]);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 2001 && HIWORD(wp) == 0) { ShowAbout(); return 0; }         // из контекстного меню
        if (id == 2002 && HIWORD(wp) == 0) { OpenSettings(); return 0; }
        if (id == 2003 && HIWORD(wp) == 0) {
            g_trayEnabled = !g_trayEnabled;
            if (g_trayEnabled) AddTray(); else RemoveTray();
            return 0;
        }
        if (id == 2004 && HIWORD(wp) == 0) { DestroyWindow(hwnd); return 0; }
        if (HIWORD(wp) == BN_CLICKED) {
            if (id == ID_CARD1) { Launch(L"ms-settings:windowsupdate"); return 0; }
            if (id == ID_CARD2) { Launch(L"msinfo32.exe"); return 0; }
            if (id == ID_CARD3) { Launch(L"taskmgr.exe"); return 0; }
        }
        int idx = id - 1000;
        if (HIWORD(wp) == BN_CLICKED && idx >= 0 && idx < (int)g_tiles.size()) g_tiles[idx].action();
        return 0;
    }

    case WM_CONTEXTMENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 2001, L"О программе");
        AppendMenuW(menu, MF_STRING, 2002, L"Дополнительные параметры");
        AppendMenuW(menu, MF_STRING | (g_trayEnabled ? MF_CHECKED : MF_UNCHECKED), 2003, L"Добавить в трей");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 2004, L"Выход");
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x == -1 && pt.y == -1) { RECT rc; GetWindowRect(hwnd, &rc); pt = { rc.left + 20, rc.top + 20 }; }
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        return 0;
    }

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_LBUTTONUP) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
        return 0;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE && g_trayEnabled) { ShowWindow(hwnd, SW_HIDE); return 0; }
        break;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(g_darkTheme ? RGB(24,24,26) : RGB(240,240,242));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    case WM_CLOSE:
        if (g_trayEnabled) { ShowWindow(hwnd, SW_HIDE); return 0; }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        RemoveTray();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInstReal = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AdminProMainClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hMain = CreateWindowExW(0, L"AdminProMainClass", L"AdminPRO",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1020, 760,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hMain, SW_SHOW);
    UpdateWindow(g_hMain);

    if (wcsstr(GetCommandLineW(), L"/tray") != nullptr) {
        g_trayEnabled = true;
        AddTray();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!g_hSettings || !IsDialogMessageW(g_hSettings, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
