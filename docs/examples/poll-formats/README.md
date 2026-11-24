# Poll response formats

This runnable ABI 3 example requires firmware 0.1.100 or later. Its endpoints
use a reserved example domain, and the simulator supplies the responses.

| Poll | Request | Response handling |
| --- | --- | --- |
| MIME GET | GET | A JSON or XML MIME type delivers a complete document. The two tests exercise both. |
| XML POST | POST with an XML query body | The explicit XML format overrides the fixture's incorrect text/plain MIME type. |
| JSON pick | GET | Explicit JSON also overrides text/plain. The existing JSON path reaches app_on_picked(). |
| Untyped | GET | application/octet-stream reaches app_on_data() as raw bytes. |

The XML and JSON bodies resemble Plex and Jellyfin responses. The POST queries
a test service. Its request body and Content-Type are separate from the
response format. Scheduled POST requests can also happen after boot or
settings changes, so choose an operation that is safe to repeat.

Set format at application level to apply a default to scheduled and on-demand
requests. A poll can override it. With the field omitted at both levels,
Perinkpheral OS uses the response MIME type. Explicit raw forces byte streaming even
if the server labels the body as JSON or XML. For example, a download-only app
can set `"format": "raw"` in its manifest.

The example registers a 1,024-byte buffer and receives complete documents in
app_on_document(). Because the example also exports app_on_data(), it receives
chunks before completion and ignores them except for the Untyped poll.
Polls using JSON pick call only app_on_picked().
Applications that do not register a document buffer can keep processing
recognized formats with their own streaming parser in app_on_data().

From the perinkpheral-os checkout, set WASI_SDK_PATH and run:

```sh
tools/perinkpheral build docs/examples/poll-formats/poll-formats --out build/poll-formats
tools/perinkpheral sim-test docs/examples/poll-formats/tests
```

## Форматы ответов опросов

Для примера с ABI 3 нужна прошивка 0.1.100 или новее. Адреса используют
зарезервированный домен, ответы подставляет симулятор.

- **MIME GET** — GET принимает JSON или XML согласно MIME-типу ответа. Два
  сценария проверяют оба формата.
- **XML POST** — POST отправляет XML-запрос на чтение. Явное поле format
  со значением xml переопределяет неверный text/plain в тестовом ответе.
- **JSON pick** — GET извлекает значение по пути JSON. Явное поле format
  со значением json переопределяет text/plain. Данные поступают в app_on_picked().
- **Untyped** — GET получает application/octet-stream. Байты приходят в
  app_on_data() без разбора.

Ответы похожи на XML Plex и JSON Jellyfin. POST обращается к тестовому сервису.
Тип запроса и его тело задаются отдельно от формата ответа. Для опроса POST
выбирайте операцию, которую безопасно повторять при запуске или сохранении
настроек.

Поле format приложения задаёт формат по умолчанию для опросов и запросов из
кода. Опрос может задать свой формат. Если поле опущено на обоих уровнях,
Perinkpheral OS использует MIME-тип ответа. Явное значение raw сохраняет поток байтов даже для ответа,
помеченного как JSON или XML.

Пример регистрирует буфер на 1 024 байта и получает целые документы через
app_on_document(). Фрагменты также приходят в app_on_data() перед завершением
ответа. Пример обрабатывает их только для Untyped. Опрос JSON с pick вызывает
только app_on_picked().
Приложение без зарегистрированного буфера может обрабатывать распознанный
формат собственным потоковым разборщиком в app_on_data().

Команды сборки и проверки приведены выше. Запускайте их из корня
perinkpheral-os с заданным WASI_SDK_PATH.
