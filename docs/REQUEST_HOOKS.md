# Request hooks — SDK 0.5.0, API 1

Сигнатуры ExteraGram: `add_hook(name, match_substring=False, priority=0)`,
`remove_hook(name)`, `pre_request_hook(request_name, account, request)` и
`post_request_hook(request_name, account, response, error)`.
Контракт: [Plugin Class](https://plugins.exteragram.app/docs/plugin-class).

## Поддерживаемый subset

| Request name | Detached Python DTO | Разрешённые изменения |
| --- | --- | --- |
| `TL_messages_setTyping` | `TL_messages_setTyping` без TL fields | CANCEL |
| `TL_account_updateStatus` | `TL_account_updateStatus(offline: bool)` | CANCEL или MODIFY `offline` |

`TL_messages_setEncryptedTyping` зарегистрировать можно, но TDesktop не
поддерживает secret chats и не создаёт такие запросы. Прочие requests идут по
обычному native пути и не вызывают эти hooks. Произвольного request API нет.

Каждый callback получает отдельную копию DTO. DEFAULT/None сохраняет предыдущий
результат; MODIFY применяет валидный DTO того же типа; MODIFY_FINAL применяет
его и завершает chain; CANCEL прекращает chain и не отправляет запрос.
Постоянная невидимость аккаунта не входит в этот контракт.
Локальному caller доставляется RPC error 400 `PLUGIN_REQUEST_CANCELLED`;
post hook для отменённого запроса не вызывается. Неверный результат или
исключение, включая SystemExit, сохраняет предыдущий результат и позволяет
следующему plugin продолжить обработку.

Post hooks — read-only уведомления для поддержанных requests. Успех:
`BoolResponse(value: bool)`, `error=None`. Ошибка: `response=None`,
`RpcError(code: int, text: str)`. Это immutable value DTOs; Java TL objects,
reflection, замена response/error и CANCEL в post hook не поддерживаются.
Недефолтная post strategy логируется и игнорируется. RPC retry может дать
несколько error notifications; это попытки одного запроса, не dedup events.

Callbacks выполняются на существующем Python worker. Native продолжение —
в Qt owner thread с проверкой lifetime/cancellation запроса. Priority убывает,
равный priority разрешается порядком загрузки файлов; несколько совпадающих
patterns одного plugin вызывают его один раз. Remove/unload очищает регистрацию;
после shutdown host requests обходят Python. Нет raw C++ pointers в DTO.
При недоступном host или переполнении очереди запрос проходит без фильтра.
Доверенный embedded Python не имеет hard timeout или crash isolation.

`account` — стабильный Telegram user ID MoeGram, а не Android slot.
Android `__sdk_version__` / `__app_version__` не являются desktop version gates.
`ghost_mode.py` использует `ui.settings.Switch(key, text, default)`.
Native renderer, сохранение значений и текущий совместимый subset
`ui.settings` описаны в [DEVELOPMENT.md](../DEVELOPMENT.md).

## Автоматические проверки

```bash
python -B Telegram/ThirdParty/moegram-plugin-engine/tests/check_request_hooks.py
cmake --build out/arch-debug --target moegram_request_smoke
out/arch-debug/Telegram/SourceFiles/plugins/moegram_request_smoke \
  "$PWD/Telegram/ThirdParty/moegram-plugin-engine/tests/fixtures/exteragram_ghost_mode/ghost_mode.py"
```

Tests используют опубликованный неизменённый MIT fixture с pinned provenance;
C++ smoke не обращается к Telegram servers. Локальные integration-отчёты
хранятся только в игнорируемом `.dev-notes/`.
