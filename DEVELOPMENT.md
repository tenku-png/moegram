# Разработка MoeGram

MoeGram основан на нативном AyuGram Desktop: C++ и Qt, CMake, собственный код Telegram Desktop и MTProto. Минимальный Plugin PoC встраивает CPython в процесс клиента; его контракт включает metadata, lifecycle, logging и минимальный Event API.

## Исходная база и обновление AyuGram

Базовая ветка — `dev` из [`AyuGram/AyuGramDesktop`](https://github.com/AyuGram/AyuGramDesktop). При подготовке репозитория она указывала на `db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a` (`7.0.9`). `upstream` указывает на AyuGram, `origin` — на `https://github.com/tenku-png/moegram.git`. Основная ветка MoeGram — `main`, отслеживающая `origin/main`; обновления AyuGram приходят из `upstream/dev`.

При обычной настройке своего клона:

```bash
git clone --recurse-submodules https://github.com/tenku-png/moegram.git moegram
cd moegram
git remote add upstream https://github.com/AyuGram/AyuGramDesktop.git
git config remote.pushDefault origin
```

История `main` начинается с самостоятельного snapshot MoeGram без родителей из AyuGram/TDesktop. Зафиксированная базовая версия AyuGram: `db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a` (`7.0.9`). Полная upstream-история остаётся доступна локально через `upstream`, но не входит в историю MoeGram. Не делайте `merge upstream/dev` в ветки MoeGram и не отправляйте upstream/backup-ветки или upstream-теги в `origin`; используйте явный push нужной ветки, без `--all`, `--mirror` и `--tags`.

Обновление переносит только diff между зафиксированной базой и новым upstream SHA. Начинайте с чистого `git status`, включая подмодули:

```bash
git switch main
git pull --ff-only origin main
git fetch --no-tags upstream dev:refs/remotes/upstream/dev
base_sha=db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a
new_sha=$(git rev-parse upstream/dev)
git cat-file -e "$base_sha^{commit}"
git switch -c codex/update-ayugram main
patch_path=$(mktemp /tmp/moegram-ayugram-update.XXXXXX.patch)
git diff --binary --full-index --no-renames --submodule=short \
  "$base_sha" "$new_sha" -- . > "$patch_path"
git apply --3way --index "$patch_path"
```

В свежем клоне, если базового объекта нет после fetch, получите его явно: `git fetch --no-tags upstream "$base_sha"`. Patch находится вне репозитория; сохраните его до окончания проверки. `git apply --3way` использует исходные blobs из upstream и сохраняет собственные изменения при успешном разрешении. При конфликтах проверьте `git status`, вручную разрешите каждый конфликт и выполните `git add` только для проверенных файлов. Не повторяйте apply поверх частично применённого patch. Для отмены на созданной ветке при исходно чистом дереве используйте `git restore --source=HEAD --staged --worktree .`, затем восстановите подмодули командой ниже; не удаляйте локальные untracked-файлы.

```bash
git submodule sync --recursive
git submodule update --init --recursive
git diff --cached --check
git diff --cached --stat
git status --short
```

Проверьте diff, сборку и необходимые smoke-тесты. Затем замените зафиксированную базовую версию и значение `base_sha` в этой инструкции на полный `$new_sha` (а номер версии — на соответствующую upstream-версию), добавьте `DEVELOPMENT.md` и создайте обычный commit обновления. Новый upstream SHA становится базой только вместе с успешно перенесённым обновлением. Влейте проверенную ветку в `main` и отправьте явно `git push origin main:main`. Родители такого commit относятся только к истории MoeGram.

Подмодули сохраняются как gitlink-коммиты из snapshot; их собственная история живёт в отдельных репозиториях и не становится историей корневого `main`. Обновляйте их по gitlink через `git submodule update --init --recursive`. Не используйте `git submodule update --remote`: это передвинет зависимости независимо от версий AyuGram. После проверки удалите временный patch: `rm -- "$patch_path"`.

Локальная страховочная ветка перед переходом на snapshot — `backup/main-before-snapshot-20261005`. Она содержит прежнюю полную историю и остаётся только локально. Для обычной разработки создавайте ветки от нового `main`:

```bash
git switch -c codex/<task> main
git push -u origin codex/<task>
```

## Сборка на Arch Linux

Официальный AyuGram [Linux guide](https://github.com/AyuGram/AyuGramDesktop/blob/dev/docs/building-linux.md) описывает сборку в Docker. Для нативной сборки на Arch ориентируемся на CMake-команды и зависимости из [PKGBUILD AyuGram](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=ayugram-desktop). Список пакетов меняется вслед за CMake и подмодулями, поэтому перед сборкой сверяйте его с актуальным PKGBUILD.

### Пакеты

Для Plugin PoC дополнительно нужны CPython 3.11+ с заголовками, библиотекой для embedding и стандартной библиотекой той же версии. Arch-пакет `python` предоставляет их вместе. Теперь Python нужен также при запуске клиента; после обновления Arch на другую minor-версию Python клиент нужно пересобрать. CMake ищет `Python3::Python` через `Development.Embed`. На Windows/macOS поставка CPython и stdlib вместе с приложением потребует отдельной упаковки; эти платформы в данном PoC не проверены.

Базовые инструменты и пакеты сборки, перечисленные в Arch PKGBUILD для этой версии:

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf python gperf \
  boost boost-libs glib2-devel gobject-introspection qt6-shadertools \
  libtg_owt microsoft-gsl range-v3 tl-expected vulkan-headers
```

Приложению также нужны Qt и системные библиотеки. В частности: `qt6-base`, `qt6-svg`, `qt6-imageformats`, `qt6-wayland`, `ffmpeg`, `glib2`, `hunspell`, `kcoreaddons`, `libfido2`, `libpipewire`, `libsrtp`, `libxcb`, `libxcomposite`, `libxdamage`, `libxext`, `libxfixes`, `libxkbcommon`, `libxrandr`, `libxtst`, `lz4`, `minizip`, `openal`, `openh264`, `openssl`, `pipewire`, `protobuf`, `rnnoise`, `xxhash`, `zlib`, `abseil-cpp`, `ada`, `cmark-gfm`, `libavif`, `libheif`, `libjpeg-turbo` и `libjxl`. PKGBUILD содержит полный runtime-список и актуальную разбивку пакетов.

### Подготовить `tde2e`

В текущем Arch PKGBUILD библиотека `tde2e` собирается из зафиксированного там коммита TDLib и передаётся основной сборке через `tde2e_DIR`. Ниже — тот же подход; коммит следует сверять с PKGBUILD используемой версии AyuGram.

```bash
mkdir -p out/arch/deps/tdlib
git -C out/arch/deps/tdlib init
git -C out/arch/deps/tdlib remote add upstream https://github.com/tdlib/td.git
git -C out/arch/deps/tdlib fetch --depth=1 upstream 022d60202e446ad1287b9fb68e687c8a0760788b
git -C out/arch/deps/tdlib checkout --detach FETCH_HEAD

cmake -S out/arch/deps/tdlib -B out/arch/deps/tdlib-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/out/arch/deps/tdlib-install" \
  -DTD_E2E_ONLY=ON
cmake --build out/arch/deps/tdlib-build --parallel 8
cmake --install out/arch/deps/tdlib-build
```

### Настроить и собрать клиент

Из корня репозитория:

```bash
git submodule update --init --recursive

cmake -S . -B out/arch-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$PWD/out/arch-debug/install" \
  -DDESKTOP_APP_USE_PACKAGED=ON \
  -DTDESKTOP_API_TEST=ON \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON \
  -Dtde2e_DIR="$PWD/out/arch/deps/tdlib-install/lib/cmake/tde2e"

cmake --build out/arch-debug --target Telegram --parallel 8
cmake --install out/arch-debug
```

`TDESKTOP_API_TEST=ON` использует тестовые ключи Telegram, подходящие для локальной разработки. Для сборки с собственными ключами зарегистрируйте `api_id` и `api_hash`, уберите `-DTDESKTOP_API_TEST=ON` из команды CMake и вместо него передайте `-DTDESKTOP_API_ID=YOUR_API_ID -DTDESKTOP_API_HASH=YOUR_API_HASH` как параметры CMake, заменив примеры своими значениями. Не коммитьте эти значения. Официальные пояснения: [`docs/api_credentials.md`](https://github.com/AyuGram/AyuGramDesktop/blob/dev/docs/api_credentials.md) и [Telegram API credentials](https://core.telegram.org/api/obtaining_api_id).

Каталог `out/` уже исключён из Git. После настройки CMake повторные сборки выполняются командой `cmake --build out/arch-debug --target Telegram --parallel 8`. При ограниченной памяти уменьшите число workers; если сборка завершилась ошибкой поиска зависимости, проверьте Arch-пакеты из актуального PKGBUILD и наличие всех submodules, не меняя их версии вручную.

## Минимальный embedded Plugin PoC

`Telegram/SourceFiles/plugins/` собирается отдельной библиотекой `moegram_plugins`. `Core::Application::run()` запускает host после создания окна; деструктор приложения останавливает его до уничтожения сервисов. C++ `Host` владеет CPython, GIL и ссылками на runtime. `runtime.py` встраивается в бинарник при сборке и предоставляет модуль `base_plugin`, загрузчик и logging bridge в обычный журнал `log.txt` клиента. Lifecycle Host вызывается на потоке приложения; события обрабатываются на отдельном последовательном worker-потоке с GIL.

Каталог плагинов — `<cWorkingDir>/plugins/`, рядом с `tdata`, но вне него. Его можно явно выбрать с `-workdir`: пример ниже использует отдельные тестовые данные и не открывает рабочий аккаунт.

```bash
mkdir -p out/plugin-poc/plugins
cp plugins/hello_world.py out/plugin-poc/plugins/
out/arch-debug/bin/AyuGram -workdir "$PWD/out/plugin-poc" -noupdate
```

При старте host создаёт каталог, находит обычные `*.py` файлы в нём (без рекурсии, symlink и имён с `_`) и загружает их в порядке имён файлов. Файлы автоматически включены: менеджера включения пока нет. Нельзя класть туда недоверенные скрипты. Host использует isolated configuration CPython: игнорирует `PYTHONPATH`/`PYTHONHOME`, user site и импорт `site`, не добавляет рабочий каталог или каталог плагинов в `sys.path`, не устанавливает обработчики сигналов Python и не пишет `.pyc`. Изолированная конфигурация управляет окружением интерпретатора и **не является security sandbox**.

Плагин объявляет строковые литералы `__id__`, `__name__`, `__version__` и ровно один класс-наследник `BasePlugin` с конструктором без аргументов. Метаданные читаются через AST до выполнения файла; ID соответствует `[a-zA-Z][a-zA-Z0-9_-]{1,31}`, повторный ID отклоняется до импорта. Версия при отсутствии равна `1.0`, как в ExteraGram. Присваивание display name `__name__` извлекается из исполняемого AST: Python-модуль сохраняет уникальное техническое имя для `sys.modules`, а display name доступен через `self.name`. До `on_plugin_load()` host назначает `self.id`, `self.name`, `self.version` и `self.logger`; доступны обычные `debug/info/warning/error/exception` Python logging и legacy `self.log(message)`.

Пример — [`plugins/hello_world.py`](plugins/hello_world.py). Успешная загрузка вызывает `on_plugin_load()` ровно один раз; завершение приложения вызывает `on_plugin_unload()` в обратном порядке. При исключении в load вызывается best-effort unload для частичной очистки, экземпляр не сохраняется как загруженный. Ошибки AST/импорта/конструктора/load/unload записываются с traceback, host продолжает работу. Перехватывается `BaseException`, включая `SystemExit`, без `PyErr_Print`, способного завершить процесс.

Это общий interpreter для доверенных плагинов и синхронные lifecycle-вызовы на UI-потоке. Здесь нет ограничений времени/памяти и изоляции между плагинами: бесконечный цикл в lifecycle блокирует клиент, `os._exit()`, native extensions или повреждение памяти могут завершить весь процесс. Потоки, фоновые задачи, зависимости pip, package imports и hot reload не входят в контракт PoC. Python имеет обычный доступ ОС к файлам и сети, хотя MoeGram не предоставляет network API. Для независимой crash isolation позже нужен другой host backend; публичные имена метаданных, `BasePlugin` и lifecycle можно сохранить.

Совместимость с ExteraGram сейчас относится только к этому небольшому контракту, не к всему Android SDK. Отдельные send/UI/permissions/store API отсутствуют. Узкий text send hook, settings descriptors и read-only get_setting для опубликованного hello_command описаны ниже; полной Android settings/UI системы нет. Сопоставление API: [metadata и lifecycle ExteraGram](https://plugins.exteragram.app/docs/plugin-class), [CPython configuration](https://docs.python.org/3/c-api/init_config.html).

### Проверки PoC

Добавьте `-DMOEGRAM_PLUGIN_TESTS=ON` к конфигурации Debug и выполните:

```bash
cmake --build out/arch-debug --target moegram_plugin_smoke --parallel 8
out/arch-debug/Telegram/SourceFiles/plugins/moegram_plugin_smoke
out/arch-debug/Telegram/SourceFiles/plugins/moegram_plugin_smoke "$PWD/plugins"
```

Первый запуск проверяет ошибки импорта, конструктора, load/unload, синтаксиса, `SystemExit`, отклонение metadata/duplicate ID до исполнения, продолжение загрузки/выгрузки, защиту от второго владельца CPython и повторный запуск после остановки. Второй загружает настоящий `hello_world.py` через тот же C++ embedded host. Для проверки в клиенте временно добавьте `raise RuntimeError("intentional PoC exception")` после logging в копию `out/plugin-poc/plugins/hello_world.py` и перезапустите клиент. В `out/plugin-poc/log.txt` должны появиться traceback и cleanup unload, а клиент продолжит работать. Закройте тестовый экземпляр штатно:

```bash
out/arch-debug/bin/AyuGram -workdir "$PWD/out/plugin-poc" -quit
```

Ошибка Python проверяет обработку исключений, а не устойчивость к native crash. Тестовые аккаунты, логи, бинарники и временные плагины остаются под игнорируемым `out/`.

При проверке shutdown обнаружен use-after-free в существующей обработке IPC `-quit`: синхронный `Quit()` закрывал приложение и очищал `_localClients`, пока `Sandbox::readClients()` ещё обращался к его итератору. Поэтому `Sandbox::execExternal()` ставит quit в Qt-очередь: обработчик успевает отправить ответ и завершить обход до teardown. Это узкое исправление lifecycle, необходимое для надёжного штатного завершения проверяемого клиента.

PoC проверен на Arch Linux: Debug-сборка с Qt 6.11.2 и CPython 3.14.7, smoke-тест embedded host, реальные запуски клиента через Wayland и Qt offscreen. В обоих режимах проверены load/unload `hello_world`, намеренный `RuntimeError` в его копии, загрузка следующего плагина после ошибки и завершение через `-quit` с кодом `0`.

## Минимальный Event API (SDK 0.4.0, API 1)

`from base_plugin import BasePlugin, Message, SDK_VERSION, API_VERSION`. Metadata `__moegram_api_version__ = "1"` фиксирует major-контракт MoeGram; если поле отсутствует, используется API 1. Другая версия отклоняется через AST до исполнения файла. `SDK_VERSION` описывает версию desktop SDK, `API_VERSION` — совместимость DTO/callback. Несовместимые изменения требуют нового major API. Это отдельная нумерация: она не означает поддержку Android SDK ExteraGram 1.4.x; его `__sdk_version__` не является проверкой совместимости MoeGram.

Ровно три уведомления: `on_message(self, message: Message)`, `on_message_edit(self, message: Message)`, `on_message_delete(self, message: Message)`. Возвращаемое значение игнорируется; события не могут менять или отменять Telegram updates.

| Поле `Message` | Контракт |
| --- | --- |
| `api_version` | `1` |
| `account_id` | `Main::Session::uniqueId()`: ID пользователя со сдвигом test DC; это не Android account index |
| `message_id` | Telegram message ID; уникален в контексте аккаунта и чата |
| `chat_id` | User: положительный ID; basic group: отрицательный ID; channel/supergroup: `-1000000000000 - id` |
| `sender_id` | Такой же формат ID для пользователя/чата; `None`, если sender отсутствует |
| `text` | Текст/подпись, без entities и native объектов |
| `outgoing` | Признак исходящего сообщения |

Для delete неизвестного сообщения `text`, `sender_id`, `outgoing` равны `None`. Channel delete содержит `chat_id`, даже если сообщение не загружено; non-channel delete без известного сообщения не содержит chat ID, поэтому он тоже `None`. Один callback на каждый ID в порядке серверного списка. Для известного сообщения delete содержит последнюю локальную копию текста до удаления; AyuGram может сохранить само сообщение согласно своим настройкам, но серверное удаление всё равно публикуется.

`Message` — frozen dataclass со slots: только Python числа/строки/`None`, без C++ pointers, Qt wrappers или ссылок на сессию. DTO можно сохранять после возврата callback, edits создают новый snapshot. Это защита от случайной мутации, а не sandbox против намеренного вмешательства Python в runtime.

Native new/edit/delete подключены через `plugins/telegram_event_adapter.cpp` в `Api::Updates::applyUpdateNoPtsCheck()`. Scoped suppression предотвращает повторную публикацию из модели. `data/data_session.cpp` сохраняет fallback для incoming short/difference и local delete responses; short sent acknowledgement нормализуется из подтверждённого server message. `on_message` по-прежнему incoming-only, compatibility hook видит оба направления. Подробный контракт, batched delete, source semantics и dedup описаны в [EVENT_API_COMPATIBILITY.md](docs/EVENT_API_COMPATIBILITY.md).

C++ `Publish` вызывается на потоке приложения и отправляет DTO в FIFO очередь Host без Python/GIL. Один worker выполняет события и вызывает плагины в порядке отсортированных имён файлов. На каждый callback перехватывается `BaseException`, включая `SystemExit` и `KeyboardInterrupt`; следующий plugin продолжает получать то же событие. Очередь ограничена 4096 updates: при переполнении новое событие отклоняется, клиент пишет предупреждение. Гарантии durable/replay доставки нет.

При `Host::stop()` приём закрывается, ожидающая очередь очищается, текущая доставка завершается, worker присоединяется, затем вызываются unload callbacks и финализируется CPython. После `on_plugin_unload()` событий нет; restart создаёт новый worker и пустую очередь. Selective unload/hot reload пока не предоставляется. Lifecycle остаётся на main thread, event callbacks — на worker; Python-плагинам нельзя обращаться к Qt/UI. Поток защищает основной event loop от медленного обычного callback, но доверенный embedded Python всё ещё способен завершить процесс, держать GIL, работать с ОС или зависнуть. Зависший callback задержит shutdown; безопасно убить Python thread нельзя. Crash isolation и жёсткие timeouts требуют будущего отдельного процесса.

### Сопоставление с ExteraGram

SDK 0.3.0 добавляет совместимую сигнатуру `on_update_hook`, регистрацию `add_hook` / `remove_hook` и `HookResult` для read-only наблюдения. Один canonical immutable Update используется hook и существующими специализированными callback. Сигнатуры, MoeGram extensions, ограничения и smoke-команды: [EVENT_API_COMPATIBILITY.md](docs/EVENT_API_COMPATIBILITY.md).

### Smoke-тесты событий

Пример: `plugins/event_logger.py`. Он логирует create/edit/delete со всеми полями; копируйте его в `<workdir>/plugins/` аналогично `hello_world.py`. Логи содержат текст сообщений, поэтому используйте собственные тестовые данные.

```bash
cmake --build out/arch-debug --target moegram_event_smoke moegram_plugin_smoke --parallel 8
out/arch-debug/Telegram/SourceFiles/plugins/moegram_event_smoke "$PWD/plugins/event_logger.py"
out/arch-debug/Telegram/SourceFiles/plugins/moegram_plugin_smoke
```

Event smoke подаёт синтетические DTO через настоящий C++ embedded Host: отдельно create/edit/delete, unknown delete, Unicode/NUL, сохранение snapshot, immutability, major version validation до исполнения, deterministic callback order, продолжение после RuntimeError/SystemExit/KeyboardInterrupt, работа Qt timer во время callback, очистка pending событий и запрет post после unload. Эти проверки не требуют аккаунта и не отправляют Telegram сообщения; реальную доставку сетевых updates нужно дополнительно проверять на авторизованном тестовом аккаунте.

Проверено на Arch Linux: полный incremental Debug target `Telegram`, lifecycle smoke, Event smoke с настоящим `event_logger.py` (включая лимит очереди; 10 повторов прошли), запуск нового клиента через Qt offscreen и штатный `-quit` с exit code 0 в обычном сценарии и после ошибки lifecycle другого плагина. Подробные локальные integration-отчёты хранятся в игнорируемом `.dev-notes/`.

## Совместимость hello_command: текстовый send hook

Опубликованный исходник ExteraGram использует следующий ограниченный контракт adapters.

`BasePlugin.add_on_send_message_hook(priority=0)` регистрирует `on_send_message_hook(account, params)`. Перед обычной текстовой отправкой ApiWrap передаёт detached params.message на существующий Python worker; Qt loop продолжает работу. DEFAULT сохраняет текст, MODIFY применяет новый, MODIFY_FINAL завершает chain, CANCEL отменяет отправку. Exceptions отдельного hook не мешают следующим. При изменении текста прежние entities/preview очищаются. Поля SendMessageParams кроме message пока не моделируются. Из plugins нельзя самостоятельно инициировать Telegram send/request.

`ui.settings` предоставляет только data descriptors Header/Input/Text для используемых keyword fields. Native settings renderer не реализован. `get_setting(key, default)` читает per-plugin JSON `<workdir>/plugin-settings/<id>.json` при load или возвращает default. Setter и management UI отсутствуют. Это storage convention MoeGram, не Android file format.

```bash
python -B Telegram/SourceFiles/plugins/tests/check_hello_command.py
cmake --build out/arch-debug --target moegram_send_smoke --parallel 8
out/arch-debug/Telegram/SourceFiles/plugins/moegram_send_smoke \
  "$PWD/Telegram/SourceFiles/plugins/tests/fixtures/exteragram_hello_command/hello_command.py"
```

Smoke не отправляет Telegram сообщения. Подробные локальные integration-отчёты хранятся только в игнорируемом `.dev-notes/`.

## Request hooks (SDK 0.4.0, API 1)

Поддержаны pre-request CANCEL для typing и MODIFY offline для updateStatus; post-request callbacks получают read-only Bool/RPC error DTOs. `ui.settings.Switch` — data descriptor без UI. Сигнатуры, порядок, unload, границы subset и команды тестов: [REQUEST_HOOKS.md](docs/REQUEST_HOOKS.md).


## Структура AyuGram и Telegram Desktop

Проект сохраняет общую архитектуру Telegram Desktop. Корневой `CMakeLists.txt` создаёт CMake-проект и подключает `Telegram/`; `Telegram/CMakeLists.txt` собирает приложение `Telegram`, его ресурсы, generated code и библиотеки. Значительная часть библиотек Desktop Toolkit подключена отдельными Git submodules в `Telegram/lib_*`, `Telegram/ThirdParty/*` и `cmake`.

| Каталог | Ответственность |
| --- | --- |
| `Telegram/SourceFiles/mtproto/`, `api/` | MTProto-соединение, Telegram-запросы и прикладная логика клиента |
| `Telegram/SourceFiles/data/`, `main/`, `storage/` | Модели данных, сессии, аккаунты, локальное хранилище |
| `Telegram/SourceFiles/ui/`, `window/`, `settings/` | Qt-виджеты, окно, навигация и системные настройки |
| `Telegram/Resources/`, `Telegram/SourceFiles/lang/` | UI-ресурсы и локализация |
| `Telegram/SourceFiles/ayu/` | AyuGram-функции, собственные настройки, данные, UI и платформенные помощники |
| `Telegram/lib_*`, `Telegram/ThirdParty/*`, `cmake/` | Внутренние библиотеки, зависимости, CMake-макросы и codegen |

AyuGram добавляет отдельный слой в `Telegram/SourceFiles/ayu/` и подключает его в `Telegram/CMakeLists.txt`, включая собственную `lib_icu`. В нём находятся Ayu-настройки, фильтры, история сообщений, режим невидимки, переводчик, streamer mode, темы и UI-компоненты. При этом форк не является независимой надстройкой поверх неизменённого Telegram Desktop: отдельные Ayu-функции интегрированы в общие участки приложения — например, настройки, отправку и обработку сообщений, сессии, меню и окно. Это места с повышенным риском конфликтов при обновлении TDesktop/AyuGram.

Практическое правило: новую самостоятельную функцию добавлять отдельными файлами/target'ом и делать как можно меньше изменений в общих файлах TDesktop. `Telegram/CMakeLists.txt` и точки подключения настроек неизбежно потребуют узких интеграционных изменений. Не редактируйте сгенерированные файлы вместо исходных схем, шаблонов или локализаций.

## Куда встроить Plugin Manager и Plugin API позже

Это направление архитектуры, а не текущая реализация.

1. **Изолированный модуль** — начать с `Telegram/SourceFiles/plugins/` и отдельной CMake-библиотеки/target'а. Разделить lifecycle/registry/permissions и host transport с одной стороны и стабильные API-фасады — с другой. Узкие связи с приложением оставить в нескольких явных местах `Telegram/CMakeLists.txt`, старта/остановки приложения и настроек.
2. **Plugin Manager UI** — собственная Qt-страница в `Telegram/SourceFiles/plugins/ui/`; открыть её из настроек AyuGram через текущую точку построения разделов `Telegram/SourceFiles/ayu/ui/settings/`. Страница управляет импортом, включением, версиями, разрешениями, логами и отключением плагинов; не размещать runtime-логику внутри UI.
3. **Plugin Host** — для будущей crash isolation host manager внутри клиента управляет жизненным циклом, а runtime каждого включённого плагина запускается в отдельном процессе с версионированным IPC-контрактом. Это не даёт падению одного плагина завершить остальные; цена — дополнительные процессы и память. Хост получает декларативные команды и типизированные DTO, а не C++-указатели, Qt-объекты, прямой доступ к сессии или внутренним структурам MTProto. Нынешний embedded CPython доказывает lifecycle/SDK-контракт; отдельный transport backend и упаковка runtime относятся к следующим этапам.
4. **Event API** — версионированные события с явной подпиской в manifest, документированным порядком вызовов, пределом времени, правилами ошибок и точной семантикой отмены/изменения. По умолчанию события не содержат лишние данные; медиа и полный текст требуют отдельного доступа.
5. **Telegram API** — небольшой набор типизированных операций поверх прикладных сервисов клиента (получить разрешённую информацию, отправить/ответить на сообщение и т. п.). Не экспортировать сырой MTProto-слой, ключи сессии и доступ к произвольным внутренним объектам. Проверять разрешение и account scope при каждом вызове.
6. **UI API** — ограниченная декларативная поверхность: команды, меню, страницы настроек, уведомления и поддерживаемые формы. Qt строит и уничтожает виджеты на UI-потоке; plugin process получает стабильные handle/DTO, но не произвольные `QWidget*`.
7. **Settings API** — типизированная схема с namespace плагина, версией и миграциями; экспорт/импорт с проверкой схемы. Явно выбрать scope настроек (глобальный или на аккаунт), сохранять атомарно и не считать обычные настройки безопасным хранилищем секретов.
8. **Permissions** — manifest декларирует запрашиваемые возможности отдельно: чтение метаданных/текста сообщений, отправка, медиа/файлы, сетевой доступ, UI и другие чувствительные операции. Разрешения закрыты по умолчанию, показываются пользователю понятным списком, могут быть отозваны и проверяются посредником, а не самим плагином.
9. **Plugin storage** — отдельный каталог по стабильному ID плагина, с квотой, атомарной записью, миграцией формата и запретом выхода через `..`/symlink. Не смешивать его с `tdata`, данными клиента или хранилищем авторизационных ключей.
10. **Crash isolation** — ограниченный IPC с request ID, размером сообщений и таймаутами; supervisor перезапускает только упавший процесс плагина, удаляет его подписки и UI-эффекты, фиксирует причину сбоя и может поместить его в quarantine до ручного включения. Отдельный процесс отделяет падение расширения от падения UI, но сам по себе не является security sandbox: границы файловой системы, сети и ресурсов требуют отдельного OS-level решения.

Для упрощения переноса diff обновлений upstream держать будущий API/host/UI вне широких правок `mainwidget.cpp`, `api_sending.cpp`, `api_updates.cpp`, `window_session_controller.cpp` и других общих точек. Если интеграция неизбежна, направлять её через несколько небольших adapter/service точек.

## Что показали ExteraGram и ExteraGramDesktop

- **Android exteraGram** предоставляет Python SDK с `BasePlugin`, событиями/хуками, Telegram helper'ами и UI-настройками. Его runtime построен вокруг Chaquopy и Android/Java-интеграции, включая Xposed-style hooks и reflection ([документация SDK](https://plugins.exteragram.app/docs)). Эта модель зависит от Android-классов и не переносится напрямую в C++/Qt.
- **ExteraGramDesktop** — независимый экспериментальный форк AyuGram, ближе к нашей платформе ([описание API 1](https://github.com/airgram-real/ExteraGramDesktop/blob/dev/docs/extera-plugins.md)). Qt-приложение общается с отдельным Python-процессом через newline-delimited JSON; там есть ограничение размера сообщения и ожидания ответа, crash marker и локальное хранилище. API намеренно уже Android-версии: в документации этой реализации указано, что Telegram request/update hooks и отправка сообщений ещё отсутствуют.
- Полезные идеи для будущего: отдельный host, bounded/versioned IPC, проверка метаданных без выполнения импортируемого кода, явная desktop-версия API, очистка эффектов после сбоя и состояние плагина вне UI. Ограничения тоже важны: один общий host связывает сбой с остановкой всего engine, а процесс Python с доступом ОС к сети и файлам не является sandbox. Поэтому эту реализацию изучаем как прототип и источник опыта, а не как готовую архитектуру для копирования.

## Источники

- [AyuGram Desktop, ветка `dev`](https://github.com/AyuGram/AyuGramDesktop/tree/dev)
- [AyuGram: Linux build guide](https://github.com/AyuGram/AyuGramDesktop/blob/dev/docs/building-linux.md)
- [AyuGram: Arch PKGBUILD](https://aur.archlinux.org/cgit/aur.git/tree/PKGBUILD?h=ayugram-desktop)
- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [exteraGram Plugin SDK](https://plugins.exteragram.app/docs)
- [ExteraGramDesktop API 1 notes](https://github.com/airgram-real/ExteraGramDesktop/blob/dev/docs/extera-plugins.md)
