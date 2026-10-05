# Event API: ExteraGram compatibility и integration-проверка

MoeGram SDK **0.5.0**, DTO API **1**. Контракт сверён с [ExteraGram Plugin Class](https://plugins.exteragram.app/docs/plugin-class) и официальными `base_plugin.pyi` из [SDK 1.4.5.7 stable](https://github.com/exteraSquad/plugins-pysdk-builds/releases/tag/sdk-v1.4.5.7-stable-37208006129-1), asset `stubs.zip` → `pyi-files.zip`. Stubs исследованы как декларации, код Android SDK не выполнялся и не копировался в клиент.

## Контракт ExteraGram

`add_hook(name: str, match_substring: bool = False, priority: int = 0)` регистрирует hook; `remove_hook(name: str)` удаляет регистрацию. Callback: `on_update_hook(self, update_name: str, account: int, update: Any) -> HookResult`. Имена — `TL_updateNewMessage`, `TL_updateNewChannelMessage`, `TL_updateEditMessage`, `TL_updateEditChannelMessage`, `TL_updateDeleteMessages`, `TL_updateDeleteChannelMessages` в данном subset. Android передаёт Java/TL объект и account index.

`HookResult` содержит `strategy`, `request`, `response`, `update`, `updates`, `error`, `params`. Именованные варианты `HookStrategy`: `DEFAULT`, `CANCEL`, `MODIFY`, `MODIFY_FINAL`. Android engine может отменять update или заменять объект; это не является контрактом observation API MoeGram.

## Что совместимо 1:1

| Поверхность | Граница совместимости |
| --- | --- |
| Import `BasePlugin`, `HookResult`, `HookStrategy` из `base_plugin` | Совпадают имена |
| `__id__`, `__name__`, `__version__` | Тот же малый контракт metadata предыдущего PoC |
| `on_plugin_load()`, `on_plugin_unload()` | Совпадают сигнатуры |
| `add_hook(name, match_substring=False, priority=0)`, `remove_hook(name)` | Совпадают сигнатуры и exact/substring matching для поддерживаемых update names |
| `on_update_hook(update_name, account, update)` | Совпадают имя, порядок аргументов, keyword names |
| `HookResult()` и `HookResult(strategy=HookStrategy.DEFAULT)` | Обычный read-only handler переносится без изменения возврата |
| Поля `HookResult`, символические имена `HookStrategy` | Совпадают имена; числовые/строковые `.value` enum не обещаны |
| Чтение `update.message.id`, `.message`, `.out`, `.peer_id.user_id/chat_id/channel_id`, `.from_id` | Совпадает форма небольшого TL subset; отсутствующий sender — `None` |
| Чтение `update.messages`, `update.channel_id`, `update.pts`, `update.pts_count` | Соответствующие значения native new/edit/delete updates; контейнер ID здесь immutable tuple |

Это source compatibility ограниченного read-only subset, не двоичная или полная Android SDK compatibility. Stubs не определяют точный scheduling/tie-break order, поэтому собственный порядок MoeGram описан ниже отдельно.

## MoeGram extensions и различия

- `Update` и `Message` — frozen dataclasses со slots. Ни Java objects, ни C++/Qt pointers, ни сессии не экспортируются. Значения можно сохранять после callback/unload. Logging bridge также не экспортирует `PyCapsule` с C++ data pointer: Python видит builtin function без native object в `__self__`.
- `Update.message_snapshots` — immutable tuple общих snapshots. `sequence`, `source`, `account_id`, `api_version` — MoeGram extensions. `Message.message_id`, `chat_id`, `sender_id`, `text`, `outgoing` — основной desktop-контракт.
- `account` — стабильный user ID с test DC shift (`Main::Session::uniqueId()`), не Android slot index. Операций с аккаунтом SDK не предоставляет.
- `chat_id` и `sender_id` используют signed Bot API convention: user `id`, group `-id`, channel `-1000000000000-id`. TL peer wrappers содержат bare positive ID. Старый `Message.getDialogId()` продолжает возвращать canonical `chat_id`; Android channel dialog ID отличается.
- `on_message`, `on_message_edit`, `on_message_delete` — MoeGram extensions. `on_message` получает только incoming; hook получает native incoming **и outgoing**. Edits/deletes включают оба направления.
- Raw entities/media, произвольные TL fields/classes, reflection, container hooks, UI renderer/independent send/network API отсутствуют. Отдельно добавлен text outgoing hook для исходного hello_command, см. [DEVELOPMENT.md](../DEVELOPMENT.md#совместимость-hello_command-текстовый-send-hook). Request subset описан в [REQUEST_HOOKS.md](REQUEST_HOOKS.md). Service messages исключены.
- В read-only update hook `HookStrategy.CANCEL/MODIFY/MODIFY_FINAL` принимаются как именованные значения, но диагностируются как неподдерживаемые; Telegram update и другие callbacks продолжаются. `HookResult.update` не заменяет DTO. `None` допускается как default observation result.
- Для short incoming, восстановления new messages через difference, direct local delete responses и short sent acknowledgement публикуется нормализованное model-событие (`source="model"`). Имя сведено к одной из шести форм; исходного short/container объекта нет, `pts`/`pts_count` могут быть `None`. Это не 1:1 wire interception.
- Для неизвестного delete chat/sender/text/outgoing могут быть `None`. Channel chat ID известен из update. Delete snapshots — последняя локальная копия, а не данные server TL delete. Sender канального поста может отсутствовать; он не подменяется произвольным user ID.

## Одна canonical конверсия и порядок

`plugins/telegram_event_adapter.cpp` копирует native update после принятия его порядковой информации TDesktop, до изменения/удаления модели. `Api::Updates::applyUpdateNoPtsCheck()` создаёт scoped suppression: тот же update не публикуется второй раз через Data::Session. Для fallback model paths сохраняются узкие подключения в Data::Session. При загрузке истории обработка existing-message edits не публикует события.

C++ передаёт только значения одного `UpdateEvent` в ограниченную FIFO очередь. Worker один раз строит canonical `Update` и tuple `Message`; всем hooks передаётся тот же `Update`, а специализированным callbacks — те же экземпляры `Message`, а не повторные преобразования.

Для каждого update:

1. Hook handlers: убывающий priority, при равенстве — порядок загрузки по имени файла. Несколько совпавших регистраций одного plugin дают один вызов с максимальным matching priority.
2. Специализированные handlers: порядок загрузки; внутри delete — порядок ID исходного списка.

Если plugin реализует оба API, он получает оба вида уведомлений намеренно. Exception одного вызова не отменяет следующий hook, специализированный callback или plugin. Stop закрывает queue, удаляет pending updates, завершает текущую доставку, затем очищает hooks и вызывает unload.

Подавление повторов ограничено текущим Host и окнами по 4096 ключей: native name/account/chat/IDs/pts/pts_count, создание по account/chat/message ID, удаление по account/channel-or-nonchannel/message ID. Это также coalesces local model notifications и их native echoes. При смешанном delete batch canonical hook сохраняет оригинальный список; специализированные delete callbacks пропускают уже доставленные IDs. После restart, вытеснения из окна или overflow durable exactly-once/replay не обещается. Очередь ограничена 4096 **updates**, не количеством ID внутри одного batch.

## Тесты

```bash
cmake --build out/arch-debug --target Telegram moegram_compatibility_smoke moegram_event_smoke moegram_plugin_smoke --parallel 8
out/arch-debug/Telegram/SourceFiles/plugins/moegram_compatibility_smoke "$PWD/plugins/compatibility_logger.py"
out/arch-debug/Telegram/SourceFiles/plugins/moegram_event_smoke "$PWD/plugins/event_logger.py"
out/arch-debug/Telegram/SourceFiles/plugins/moegram_plugin_smoke
```

Compatibility smoke подаёт синтетические updates через реальный C++ embedded Host. Проверяет private/group/channel IDs, incoming/outgoing, оба edits, single/batch delete, native duplicates и model echoes, object identity между plugins/hook/specialized callback, immutability, registration/removal, priorities, exception isolation, unsupported strategy, unload. Это не замена реальному Telegram integration-тесту.

### Выполненные проверки

На Arch Linux успешно собран актуальный Debug target `Telegram`. Compatibility smoke проверил 12 canonical updates с повторами, включая все шесть update names, оба направления, channel edits, mixed-direction channel batch delete, model echoes до последнего контрольного события, scoped suppression и отсутствие C++ data capsule у logging function. Event/lifecycle regression tests прошли; compatibility и Event smoke повторены по 10 раз. Проверка log manifest прошла на синтетических fixtures и отвергла намеренный дубль и неверный sender ID. Новый клиент через Qt offscreen загрузил примеры plugins и завершился через штатный `-quit` с кодом 0 в обычном сценарии и после ошибки lifecycle другого plugin. Эти запуски использовали неавторизованные временные workdirs.

## Реальная integration-проверка: состояние

Подробные локальные integration-отчёты хранятся только в игнорируемом `.dev-notes/`.

`TDESKTOP_API_TEST=ON` выбирает API credentials; само по себе оно не доказывает подключение к Telegram test DC. Realm авторизованного аккаунта отдельно не проверялся.

После получения тестового workdir:

1. Использовать только согласованные тестовые чаты и действия; для incoming/edit incoming нужен второй тестовый участник. Saved Messages не заменяет incoming.
2. Штатно закрыть только тестовый экземпляр перед изменением plugin directory, сохранить существующие plugins. Добавить `event_logger.py` и `compatibility_logger.py`, запустить Debug клиент с этим `-workdir`. Для настоящего test DC подтвердить его в конфигурации сессии; не считать production аккаунт test DC автоматически.
3. В private chat отправить уникальный текст, получить другой уникальный текст от второго участника, отредактировать каждый на отличающийся текст. Удалить одно сообщение, затем два других одной операцией. Повторить доступные операции для group/channel; недоступный тип чата отметить `N/A` с причиной.
4. Записать ожидаемые account/chat/sender/message IDs и text/outgoing из независимого тестового участника/клиента, не только из проверяемого plugin log. Проверить отсутствие fake local negative message IDs у outgoing server acknowledgement.
5. `compatibility_logger.py` пишет `COMPAT` JSON один раз на canonical update и `SPEC` JSON для каждого специализированного callback, включая общий sequence. Проверить каждый сценарий и возвращённые поля; batch delete должен иметь два ID в одном hook и два специализированных вызова.
6. Закрыть тестовый клиент штатно, сохранить ограниченный тестовым запуском log и manifest под игнорируемым `out/`. Вернуть plugins в исходное состояние. Не сохранять session keys/tdata в отчёт или Git.

Формат manifest: `{"cases": [{"name": "private incoming", "account": 123, "update": "TL_updateNewMessage", "messages": [{"message_id": 42, "chat_id": 777, "sender_id": 789, "outgoing": false, "text": "unique fixture text"}], "specialized": "on_message"}]}`. Числа здесь примеры, не результаты. Для outgoing new `specialized` — `null`; для edit/delete — соответствующий callback. Для batch `messages` содержит оба snapshots в порядке update.

```bash
python Telegram/SourceFiles/plugins/tests/check_integration_log.py out/event-integration/log.txt out/event-integration/expected.json
```

Checker проверяет ровно одно совпадение каждого ожидаемого canonical update, полный набор полей, sequence/order, число специализированных callbacks и отсутствие ошибки самого compatibility plugin. Передавайте log одного запуска (sequence сбрасывается при restart). Если сервер разделил операцию на несколько updates, manifest должен отражать реальную wire batching; это нужно явно отметить в отчёте, а не искусственно объединять записи.

## Проверка опубликованного source

Неизменённый внешний lifecycle plugin проверен через embedded Host и native Debug клиент: 0 изменённых строк, 0 shims. Подробный локальный отчёт хранится в `.dev-notes/`. Этот учебный plugin не проверяет Event hooks.
