# AdminPRO

Наверху — 3 карточки со сведениями о системе (О системе / О BIOS и плате /
О компьютере), ниже — список инструментов (иконка своей программы у каждого,
объяснение "зачем это надо" справа). Клик по карточке 1 открывает Центр
обновлений, по карточке 2 — msinfo32 (подробности), по карточке 3 —
Диспетчер задач.

## Файлы

- `AdminPro.cpp` — весь код.
- `AdminPro.manifest` — просит права администратора при запуске.
- `AdminPro_resource.rc` — вшивает манифест в exe.
- `AdminPRO.ico` — иконка программы (положи рядом с exe).

## Сборка (MSYS2 MinGW64)

```bat
windres AdminPro_resource.rc -O coff -o resource.o
g++ -std=c++17 -municode -mwindows -O2 -static -static-libgcc -static-libstdc++ AdminPro.cpp resource.o -o AdminPRO.exe -lshell32 -lgdi32 -luser32 -lole32 -lcomctl32 -lwininet
```

Обрати внимание: добавился новый флаг **`-lwininet`** в конце команды —
он нужен для проверки подключения к интернету в карточке "О компьютере".

## Откуда берутся данные карточек

Без WMI/COM — быстрее и проще:
- Windows (версия/сборка/edition) — реестр `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion`
- Диск C: — `GetDiskFreeSpaceExW`
- BIOS/плата/производитель ПК — реестр `HKLM\HARDWARE\DESCRIPTION\System\BIOS`
  (эти значения Windows сама заполняет из SMBIOS при загрузке)
- CPU — реестр `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0`
- ОЗУ — `GlobalMemoryStatusEx`
- GPU — `EnumDisplayDevicesW` (только основной адаптер)
- Диски (общее) — перебор `GetLogicalDrives` + `GetDiskFreeSpaceExW`
- Интернет — `InternetGetConnectedState` (просто да/нет, без деталей адаптера)

Упрощение: "доступны ли обновления" не проверяется по-настоящему (это
требует Windows Update API/COM, тяжеловесно для такой программы) — карточка
просто открывает Центр обновлений Windows по клику.

## Про установщик

`AdminPRO_setup.iss` — открой в Inno Setup Compiler, Build → Compile.
Кастомный визард на 4 экрана: Добро пожаловать (галочка "сразу в трей") →
Место установки → Лицензия MIT (Cosmoboyn) → Готово к установке → ...
на странице "Готово" кнопка "Отмена" удаляет только что установленное
(с предупреждением), "OK" закрывает и запускает AdminPRO при отмеченной
галочке.
