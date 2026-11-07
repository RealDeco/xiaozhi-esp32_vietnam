#include "radio_web_server.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "application.h"
#include "board.h"
#include "display.h"
#include "features/music/esp32_radio.h"
#include "settings.h"

#include <string>
#include <cstring>

namespace radio {
namespace {

const char* kTag = "RADIO_WEB";

// ---------------------------------------------------------------------------
// HTML page (Russian)
// ---------------------------------------------------------------------------
const char* kRadioPageHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Радиостанции — XiaoZhi</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
         margin: 0; padding: 16px; background: #f5f5f5; color: #222; }
  h1 { font-size: 20px; margin: 0 0 16px; color: #1a1a1a; }
  .card { background: #fff; border-radius: 12px; padding: 16px; margin-bottom: 16px;
          box-shadow: 0 2px 6px rgba(0,0,0,0.08); }
  .row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  label { font-size: 13px; color: #666; display: block; margin-bottom: 6px; }
  input[type="text"], input[type="url"], select {
    width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 8px;
    font-size: 14px; margin-bottom: 12px; }
  input[type="range"] { width: 100%; margin: 8px 0; }
  .btn { padding: 10px 16px; border: none; border-radius: 8px; font-size: 14px;
         cursor: pointer; font-weight: 600; }
  .btn-primary { background: #2563eb; color: #fff; }
  .btn-primary:hover { background: #1d4ed8; }
  .btn-secondary { background: #e5e7eb; color: #374151; }
  .btn-secondary:hover { background: #d1d5db; }
  .btn-danger { background: #dc2626; color: #fff; }
  .btn-danger:hover { background: #b91c1c; }
  .btn-small { padding: 6px 12px; font-size: 12px; margin-left: 6px; }
  .station { border: 1px solid #e5e7eb; border-radius: 10px; padding: 12px 14px;
             margin-bottom: 10px; background: #fff; display: flex;
             align-items: center; justify-content: space-between; gap: 10px; }
  .station-info { flex: 1; min-width: 0; }
  .station-name { font-weight: 700; font-size: 15px; color: #111; }
  .station-url { font-size: 12px; color: #666; word-break: break-all; margin-top: 2px; }
  .station-meta { font-size: 12px; color: #9ca3af; margin-top: 4px; }
  .actions { display: flex; gap: 6px; flex-shrink: 0; }
  .hidden { display: none; }
  .badge { display: inline-block; font-size: 11px; padding: 2px 8px; border-radius: 10px;
           background: #e0e7ff; color: #3730a3; margin-left: 8px; }
  .badge.custom { background: #dcfce7; color: #166534; }
  .msg { margin-top: 12px; padding: 10px; border-radius: 8px; font-size: 14px; }
  .msg.ok { background: #dcfce7; color: #166534; border: 1px solid #86efac; }
  .msg.err { background: #fee2e2; color: #991b1b; border: 1px solid #fca5a5; }
  .section-title { font-size: 15px; font-weight: 700; margin: 0 0 10px; color: #111; }
</style>
</head>
<body>

<h1>📻 Радиостанции</h1>

<div class="card">
  <div class="section-title">Список станций</div>
  <div id="stationList"></div>
</div>

<div class="card">
  <div class="section-title" id="formTitle">Добавить станцию</div>
  <form id="stationForm">
    <input type="hidden" id="editKey">
    <label>Название</label>
    <input type="text" id="inpName" placeholder="Например: Маруся FM" required>

    <label>Ссылка на поток (URL)</label>
    <input type="url" id="inpUrl" placeholder="https://example.com/stream.mp3" required>

    <label>Жанр</label>
    <input type="text" id="inpGenre" placeholder="Pop, Rock, Jazz…">

    <label>Громкость (кратность)</label>
    <input type="range" id="inpVolume" min="0" max="2" step="0.1" value="1">
    <span id="volVal">1.0</span>

    <label>Кодек</label>
    <select id="inpCodec">
      <option value="0">MP3</option>
      <option value="1">AAC</option>
    </select>

    <div class="row">
      <button type="submit" class="btn btn-primary" id="formBtn">Сохранить</button>
      <button type="button" class="btn btn-secondary" id="cancelBtn">Отмена</button>
    </div>
  </form>
  <div id="msg" class="msg hidden"></div>
</div>

<div class="card">
  <div class="section-title">🔄 Обновление прошивки</div>
  <label>Адрес OTA-сервера (проверка версии)</label>
  <input type="url" id="otaUrl" placeholder="http://192.168.3.2:8090/ota/">
  <div class="row">
    <button type="button" class="btn btn-primary" id="otaSaveBtn">Сохранить</button>
    <button type="button" class="btn btn-secondary" id="otaResetBtn">Сбросить</button>
  </div>
  <div style="font-size:12px;color:#6b7280;margin-top:8px">
    Адрес локального OTA-сервера (см. tools/ota_server). Применяется после
    перезагрузки устройства. Пустое поле — обновление только вручную через
    <code>http://IP-устройства/ota</code>.
  </div>
  <div id="otaMsg" class="msg hidden"></div>
</div>

<div class="card">
  <div class="section-title">🖥️ Поворот экрана</div>
  <div class="row" id="rotationRow">
    <button type="button" class="btn btn-secondary rot-btn" data-deg="0">0°</button>
    <button type="button" class="btn btn-secondary rot-btn" data-deg="90">90° ↻</button>
    <button type="button" class="btn btn-secondary rot-btn" data-deg="180">180°</button>
    <button type="button" class="btn btn-secondary rot-btn" data-deg="270">270° ↺</button>
  </div>
  <div style="font-size:12px;color:#6b7280;margin-top:8px">
    Поворот интерфейса на экране устройства (0/90/180/270). Настройка сохраняется
    в NVS и применяется при загрузке; после смены угла плата перезагрузится.
  </div>
  <div id="rotMsg" class="msg hidden"></div>
</div>

<div class="card">
  <div class="section-title">🌤️ Погода на экране ожидания</div>
  <label>Город (OpenWeatherMap)</label>
  <input type="text" id="weatherCity" placeholder="Краснодар">
  <label>API-ключ OpenWeatherMap (необязательно)</label>
  <input type="text" id="weatherApiKey" placeholder="оставьте пустым, чтобы не менять">
  <div class="row">
    <button type="button" class="btn btn-primary" id="weatherSaveBtn">Сохранить</button>
    <button type="button" class="btn btn-secondary" id="weatherAutoBtn">Авто (по IP)</button>
  </div>
  <div style="font-size:12px;color:#6b7280;margin-top:8px">
    Город задаёт местоположение для погоды в режиме ожидания (например: Краснодар).
    «Авто» — определение по IP. Изменения применятся при следующем обновлении погоды
    или после перезагрузки устройства.
  </div>
  <div id="weatherMsg" class="msg hidden"></div>
</div>

<script>
let stations = [];
let editingKey = null;

function showMsg(text, ok) {
  const el = document.getElementById('msg');
  el.textContent = text;
  el.className = 'msg ' + (ok ? 'ok' : 'err');
  el.classList.remove('hidden');
  setTimeout(() => el.classList.add('hidden'), 4000);
}

async function loadStations() {
  try {
    const res = await fetch('/radio/api/stations');
    stations = await res.json();
    render();
  } catch (e) {
    showMsg('Не удалось загрузить станции: ' + e.message, false);
  }
}

function render() {
  const list = document.getElementById('stationList');
  list.innerHTML = '';
  if (stations.length === 0) {
    list.innerHTML = '<div style="color:#666">Станций нет. Добавьте первую.</div>';
    return;
  }
  for (const s of stations) {
    const el = document.createElement('div');
    el.className = 'station';
    const isCustom = (s.key || '').startsWith('CUSTOM_');
    el.innerHTML =
      '<div class="station-info">' +
      '<div class="station-name">' + escapeHtml(s.name) +
      (isCustom ? '<span class="badge custom">своя</span>' : '') + '</div>' +
      '<div class="station-url">' + escapeHtml(s.url) + '</div>' +
      '<div class="station-meta">Жанр: ' + escapeHtml(s.genre || '—') +
      ' &middot; Кодек: ' + (s.codec === 1 ? 'AAC' : 'MP3') +
      ' &middot; Громкость: ' + parseFloat(s.volume || 1).toFixed(1) + '</div>' +
      '</div>' +
      '<div class="actions">' +
      '<button class="btn btn-secondary btn-small" onclick="playStation(\'' + escapeJs(s.key) + '\')">Играть</button>' +
      '<button class="btn btn-secondary btn-small" onclick="editStation(' + JSON.stringify(s) + ')">Редактировать</button>' +
      (isCustom ? '<button class="btn btn-danger btn-small" onclick="deleteStation(\'' + escapeJs(s.key) + '\', \'' + escapeJs(s.name) + '\')">Удалить</button>' : '') +
      '</div>';
    list.appendChild(el);
  }
}

function escapeHtml(t) {
  return String(t).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":"&#39;"}[c]));
}
function escapeJs(t) {
  return String(t).replace(/\\/g, '\\\\').replace(/'/g, '\\\'');
}

async function playStation(key) {
  try {
    const res = await fetch('/radio/api/stations/' + encodeURIComponent(key) + '/play', { method: 'POST' });
    const data = await res.json().catch(() => ({}));
    if (res.ok && data.ok) showMsg('Включаю: ' + key, true);
    else showMsg('Не удалось переключить станцию', false);
  } catch (e) { showMsg('Ошибка: ' + e.message, false); }
}

function editStation(s) {
  editingKey = s.key;
  document.getElementById('editKey').value = s.key;
  document.getElementById('inpName').value = s.name;
  document.getElementById('inpUrl').value = s.url;
  document.getElementById('inpGenre').value = s.genre || '';
  document.getElementById('inpVolume').value = s.volume || 1;
  document.getElementById('volVal').textContent = parseFloat(s.volume || 1).toFixed(1);
  document.getElementById('inpCodec').value = s.codec === 1 ? '1' : '0';
  document.getElementById('formTitle').textContent = 'Редактировать станцию';
  document.getElementById('formBtn').textContent = 'Сохранить изменения';
  document.getElementById('cancelBtn').classList.remove('hidden');
}

document.getElementById('cancelBtn').addEventListener('click', () => {
  editingKey = null;
  document.getElementById('stationForm').reset();
  document.getElementById('volVal').textContent = '1.0';
  document.getElementById('formTitle').textContent = 'Добавить станцию';
  document.getElementById('formBtn').textContent = 'Сохранить';
  document.getElementById('cancelBtn').classList.add('hidden');
});

document.getElementById('inpVolume').addEventListener('input', (e) => {
  document.getElementById('volVal').textContent = parseFloat(e.target.value).toFixed(1);
});

document.getElementById('stationForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const key = editingKey || ('CUSTOM_' + Date.now());
  const body = {
    key: key,
    name: document.getElementById('inpName').value.trim(),
    url: document.getElementById('inpUrl').value.trim(),
    genre: document.getElementById('inpGenre').value.trim(),
    volume: parseFloat(document.getElementById('inpVolume').value),
    codec: parseInt(document.getElementById('inpCodec').value, 10)
  };
  try {
    const res = await fetch('/radio/api/stations', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    showMsg(editingKey ? 'Станция обновлена' : 'Станция добавлена', true);
    editingKey = null;
    document.getElementById('stationForm').reset();
    document.getElementById('volVal').textContent = '1.0';
    document.getElementById('formTitle').textContent = 'Добавить станцию';
    document.getElementById('formBtn').textContent = 'Сохранить';
    document.getElementById('cancelBtn').classList.add('hidden');
    loadStations();
  } catch (err) {
    showMsg('Ошибка сохранения: ' + err.message, false);
  }
});

async function deleteStation(key, name) {
  if (!confirm('Удалить станцию "' + name + '"?')) return;
  try {
    const res = await fetch('/radio/api/stations/' + encodeURIComponent(key), { method: 'DELETE' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    showMsg('Станция удалена', true);
    loadStations();
  } catch (e) { showMsg('Ошибка удаления: ' + e.message, false); }
}

// --- OTA update server -------------------------------------------------------
function showOtaMsg(text, ok) {
  const el = document.getElementById('otaMsg');
  el.textContent = text;
  el.className = 'msg ' + (ok ? 'ok' : 'err');
  el.classList.remove('hidden');
  setTimeout(() => el.classList.add('hidden'), 6000);
}

async function loadOtaUrl() {
  try {
    const res = await fetch('/radio/api/ota');
    const data = await res.json();
    document.getElementById('otaUrl').value = data.url || '';
  } catch (e) { /* сервер ещё не готов */ }
}

async function saveOtaUrl(url) {
  try {
    const res = await fetch('/radio/api/ota', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url })
    });
    const data = await res.json();
    if (!res.ok || !data.success) throw new Error(data.error || ('HTTP ' + res.status));
    showOtaMsg(url ? 'Адрес сохранён. Перезагрузите устройство — оно проверит обновление.'
                   : 'Адрес сброшен. Перезагрузите устройство.', true);
    loadOtaUrl();
  } catch (e) { showOtaMsg('Ошибка: ' + e.message, false); }
}

document.getElementById('otaSaveBtn').addEventListener('click',
  () => saveOtaUrl(document.getElementById('otaUrl').value.trim()));
document.getElementById('otaResetBtn').addEventListener('click', () => saveOtaUrl(''));

// --- Screen rotation ---------------------------------------------------------
function showRotMsg(text, ok) {
  const el = document.getElementById('rotMsg');
  el.textContent = text;
  el.className = 'msg ' + (ok ? 'ok' : 'err');
  el.classList.remove('hidden');
  setTimeout(() => el.classList.add('hidden'), 4000);
}

function highlightRotation(deg) {
  document.querySelectorAll('.rot-btn').forEach((btn) => {
    const active = Number(btn.dataset.deg) === deg;
    btn.className = 'btn rot-btn ' + (active ? 'btn-primary' : 'btn-secondary');
  });
}

async function loadRotation() {
  try {
    const res = await fetch('/radio/api/rotation');
    const data = await res.json();
    highlightRotation(data.rotation || 0);
  } catch (e) { /* сервер ещё не готов */ }
}

async function setRotation(deg) {
  try {
    const res = await fetch('/radio/api/rotation', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ rotation: deg })
    });
    const data = await res.json();
    if (!res.ok || !data.success) throw new Error(data.error || ('HTTP ' + res.status));
    highlightRotation(data.rotation);
    showRotMsg(data.reboot_required
      ? 'Поворот: ' + data.rotation + '°. Устройство перезагружается…'
      : 'Поворот экрана: ' + data.rotation + '°', true);
  } catch (e) { showRotMsg('Ошибка: ' + e.message, false); }
}

document.querySelectorAll('.rot-btn').forEach((btn) => {
  btn.addEventListener('click', () => setRotation(Number(btn.dataset.deg)));
});

// --- Weather -----------------------------------------------------------------
function showWeatherMsg(text, ok) {
  const el = document.getElementById('weatherMsg');
  el.textContent = text;
  el.className = 'msg ' + (ok ? 'ok' : 'err');
  el.classList.remove('hidden');
  setTimeout(() => el.classList.add('hidden'), 4000);
}

async function loadWeather() {
  try {
    const res = await fetch('/radio/api/weather');
    const data = await res.json();
    document.getElementById('weatherCity').value = data.city || '';
    document.getElementById('weatherApiKey').value = '';
    document.getElementById('weatherApiKey').placeholder = data.has_api_key
      ? 'ключ сохранён (введите новый, чтобы заменить)' : 'оставьте пустым, чтобы не менять';
  } catch (e) { /* сервер не готов */ }
}

async function saveWeather(city, apiKey) {
  try {
    const body = { city };
    if (apiKey && apiKey.length) body.api_key = apiKey;
    const res = await fetch('/radio/api/weather', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const data = await res.json();
    if (!res.ok || !data.success) throw new Error(data.error || ('HTTP ' + res.status));
    showWeatherMsg('Сохранено: ' + (data.city || 'авто по IP') + '. Обновится при следующем запросе погоды или после перезагрузки.', true);
    loadWeather();
  } catch (e) { showWeatherMsg('Ошибка: ' + e.message, false); }
}

document.getElementById('weatherSaveBtn').addEventListener('click', () =>
  saveWeather(document.getElementById('weatherCity').value, document.getElementById('weatherApiKey').value));
document.getElementById('weatherAutoBtn').addEventListener('click', () => saveWeather('', ''));

loadStations();
loadOtaUrl();
loadRotation();
loadWeather();
</script>

</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static esp_err_t SendJson(httpd_req_t* req, cJSON* root) {
  char* json = cJSON_PrintUnformatted(root);
  if (!json) return ESP_ERR_NO_MEM;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t err = httpd_resp_send(req, json, strlen(json));
  cJSON_free(json);
  return err;
}

static esp_err_t SendText(httpd_req_t* req, const char* body) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, body, strlen(body));
}

// Read request body into a null-terminated string
static std::string ReadBody(httpd_req_t* req) {
  size_t len = req->content_len;
  if (len == 0) return "";
  std::string buf;
  buf.resize(len + 1);
  int received = httpd_req_recv(req, &buf[0], len);
  if (received <= 0) return "";
  buf.resize(received);
  buf[received] = '\0';
  return buf;
}

static esp_err_t SendOtaError(httpd_req_t* req, const char* message) {
  cJSON* out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "success", false);
  cJSON_AddStringToObject(out, "error", message);
  esp_err_t r = SendJson(req, out);
  cJSON_Delete(out);
  return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

RadioWebServer& RadioWebServer::GetInstance() {
  static RadioWebServer instance;
  return instance;
}

RadioWebServer::~RadioWebServer() {
  Stop();
}

esp_err_t RadioWebServer::Start(int port) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.ctrl_port = port + 1024;  // avoid clash with OTA
  config.stack_size = 8192;
  config.lru_purge_enable = true;  // close least-recently-used socket instead of failing
  config.max_uri_handlers = 12;     // index + stations*4 + ota*2 + rotation + weather

  esp_err_t err = httpd_start(&server_handle_, &config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to start HTTP server on port %d: %d", port, err);
    return err;
  }

  httpd_uri_t page_get = {
    .uri = "/radio",
    .method = HTTP_GET,
    .handler = HandlePageGet,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &page_get);

  httpd_uri_t stations_get = {
    .uri = "/radio/api/stations",
    .method = HTTP_GET,
    .handler = HandleStationsGet,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &stations_get);

  httpd_uri_t stations_post = {
    .uri = "/radio/api/stations",
    .method = HTTP_POST,
    .handler = HandleStationsPost,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &stations_post);

  httpd_uri_t station_play = {
    .uri = "/radio/api/stations/*",
    .method = HTTP_POST,
    .handler = HandleStationPlay,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &station_play);

  httpd_uri_t station_delete = {
    .uri = "/radio/api/stations/*",
    .method = HTTP_DELETE,
    .handler = HandleStationDelete,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &station_delete);

  httpd_uri_t ota_url_get = {
    .uri = "/radio/api/ota",
    .method = HTTP_GET,
    .handler = HandleOtaUrlGet,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &ota_url_get);

  httpd_uri_t ota_url_post = {
    .uri = "/radio/api/ota",
    .method = HTTP_POST,
    .handler = HandleOtaUrlPost,
    .user_ctx = nullptr
  };
  httpd_register_uri_handler(server_handle_, &ota_url_post);

  // esp_http_server allows CONFIG_HTTPD_MAX_URI_HANDLERS (8 by default)
  // registrations; GET and POST share one handler to stay within the limit.
  httpd_uri_t rotation = {
    .uri = "/radio/api/rotation",
    .method = (httpd_method_t)HTTP_ANY,
    .handler = HandleRotationAny,
    .user_ctx = nullptr
  };
  esp_err_t rot_err = httpd_register_uri_handler(server_handle_, &rotation);
  if (rot_err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to register /radio/api/rotation: %s", esp_err_to_name(rot_err));
  }

  httpd_uri_t weather = {
    .uri = "/radio/api/weather",
    .method = (httpd_method_t)HTTP_ANY,
    .handler = HandleWeatherAny,
    .user_ctx = nullptr
  };
  esp_err_t w_err = httpd_register_uri_handler(server_handle_, &weather);
  if (w_err != ESP_OK) {
    ESP_LOGE(kTag, "Failed to register /radio/api/weather: %s", esp_err_to_name(w_err));
  }

  ESP_LOGI(kTag, "Radio web server started on port %d", port);
  return ESP_OK;
}

void RadioWebServer::Stop() {
  if (server_handle_) {
    httpd_stop(server_handle_);
    server_handle_ = nullptr;
    ESP_LOGI(kTag, "Radio web server stopped");
  }
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

esp_err_t RadioWebServer::HandlePageGet(httpd_req_t* req) {
  return SendText(req, kRadioPageHtml);
}

esp_err_t RadioWebServer::HandleOtaUrlGet(httpd_req_t* req) {
  Settings settings("wifi", false);
  std::string url = settings.GetString("ota_url");
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "url", url.c_str());
  cJSON_AddBoolToObject(root, "enabled", !url.empty());
  esp_err_t r = SendJson(req, root);
  cJSON_Delete(root);
  return r;
}

esp_err_t RadioWebServer::HandleOtaUrlPost(httpd_req_t* req) {
  std::string body = ReadBody(req);
  cJSON* root = cJSON_Parse(body.c_str());
  cJSON* url_item = root ? cJSON_GetObjectItem(root, "url") : nullptr;
  if (!cJSON_IsString(url_item)) {
    if (root) cJSON_Delete(root);
    return SendOtaError(req, "expected JSON body: {\"url\": \"http://...\"}");
  }

  std::string value = url_item->valuestring;
  cJSON_Delete(root);

  // Trim surrounding whitespace.
  auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!value.empty() && is_space(value.front())) value.erase(value.begin());
  while (!value.empty() && is_space(value.back())) value.pop_back();

  bool reset = value.empty() || value == "default" || value == "reset";
  if (!reset && value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) {
    return SendOtaError(req, "URL must start with http:// or https://");
  }

  {
    Settings settings("wifi", true);
    if (reset) {
      settings.SetString("ota_url", "");
      ESP_LOGI(kTag, "OTA url reset (manual updates only)");
    } else {
      settings.SetString("ota_url", value);
      ESP_LOGI(kTag, "OTA url set to %s (used after reboot)", value.c_str());
    }
  }

  cJSON* out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "success", true);
  cJSON_AddStringToObject(out, "url", reset ? "" : value.c_str());
  cJSON_AddBoolToObject(out, "reboot_required", true);
  esp_err_t r = SendJson(req, out);
  cJSON_Delete(out);
  return r;
}

esp_err_t RadioWebServer::HandleRotationAny(httpd_req_t* req) {
  switch (req->method) {
    case HTTP_GET:
      return HandleRotationGet(req);
    case HTTP_POST:
      return HandleRotationPost(req);
    default:
      return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "method not allowed");
  }
}

esp_err_t RadioWebServer::HandleRotationGet(httpd_req_t* req) {
  Settings settings("display", false);
  int degree = settings.GetInt("rotation_degree", 0);
  cJSON* root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "rotation", degree);
  cJSON_AddBoolToObject(root, "supported", Board::GetInstance().GetDisplay() != nullptr);
  esp_err_t r = SendJson(req, root);
  cJSON_Delete(root);
  return r;
}

esp_err_t RadioWebServer::HandleRotationPost(httpd_req_t* req) {
  std::string body = ReadBody(req);
  cJSON* root = cJSON_Parse(body.c_str());
  cJSON* item = root ? cJSON_GetObjectItem(root, "rotation") : nullptr;
  if (!cJSON_IsNumber(item)) {
    if (root) cJSON_Delete(root);
    return SendOtaError(req, "expected JSON body: {\"rotation\": 0|90|180|270}");
  }

  int degree = item->valueint;
  cJSON_Delete(root);

  if (degree != 0 && degree != 90 && degree != 180 && degree != 270) {
    return SendOtaError(req, "rotation must be 0, 90, 180 or 270");
  }

  auto display = Board::GetInstance().GetDisplay();
  if (display == nullptr) {
    return SendOtaError(req, "display not available");
  }

  Settings prev_settings("display", false);
  int previous = prev_settings.GetInt("rotation_degree", 0);

  if (!display->SetRotation(degree, true)) {
    return SendOtaError(req, "rotation is not supported by this display");
  }

  ESP_LOGI(kTag, "Screen rotation set to %d degrees (saved)", degree);

  // The UI containers are laid out for the current logical resolution, so a
  // rotation change needs a reboot to rebuild the layout correctly.
  bool reboot_required = (previous != degree);
  if (reboot_required) {
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = [](void*) { esp_restart(); };
    timer_args.name = "rotation_reboot";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
      esp_timer_start_once(timer, 1500 * 1000);
    }
  }

  cJSON* out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "success", true);
  cJSON_AddNumberToObject(out, "rotation", degree);
  cJSON_AddBoolToObject(out, "reboot_required", reboot_required);
  esp_err_t r = SendJson(req, out);
  cJSON_Delete(out);
  return r;
}

esp_err_t RadioWebServer::HandleWeatherAny(httpd_req_t* req) {
  switch (req->method) {
    case HTTP_GET:
      return HandleWeatherGet(req);
    case HTTP_POST:
      return HandleWeatherPost(req);
    default:
      return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "method not allowed");
  }
}

esp_err_t RadioWebServer::HandleWeatherGet(httpd_req_t* req) {
  Settings settings("wifi", false);
  std::string city = settings.GetString("weather_city", "");
  std::string api_key = settings.GetString("weather_api_key", "");
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "city", city.c_str());
  cJSON_AddBoolToObject(root, "has_api_key", !api_key.empty());
#if CONFIG_WEATHER_IDLE_DISPLAY_ENABLE
  cJSON_AddBoolToObject(root, "supported", true);
#else
  cJSON_AddBoolToObject(root, "supported", false);
#endif
  esp_err_t r = SendJson(req, root);
  cJSON_Delete(root);
  return r;
}

esp_err_t RadioWebServer::HandleWeatherPost(httpd_req_t* req) {
  std::string body = ReadBody(req);
  cJSON* root = cJSON_Parse(body.c_str());
  cJSON* city_item = root ? cJSON_GetObjectItem(root, "city") : nullptr;
  cJSON* key_item = root ? cJSON_GetObjectItem(root, "api_key") : nullptr;

  std::string city = cJSON_IsString(city_item) ? city_item->valuestring : "";
  bool set_key = cJSON_IsString(key_item);
  std::string api_key = set_key ? key_item->valuestring : "";
  if (root) cJSON_Delete(root);

  auto trim = [](std::string& v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
  };
  trim(city);
  trim(api_key);

  {
    Settings settings("wifi", true);
    settings.SetString("weather_city", city);
    if (set_key) {
      settings.SetString("weather_api_key", api_key);
    }
  }

  ESP_LOGI(kTag, "Weather settings saved: city='%s'%s", city.c_str(), set_key ? ", api_key updated" : "");

  cJSON* out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "success", true);
  cJSON_AddStringToObject(out, "city", city.c_str());
  cJSON_AddBoolToObject(out, "has_api_key", !api_key.empty() || !set_key);
  esp_err_t r = SendJson(req, out);
  cJSON_Delete(out);
  return r;
}

esp_err_t RadioWebServer::HandleStationsGet(httpd_req_t* req) {
  auto* radio = Application::GetInstance().GetRadio();
  if (!radio) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "radio not available");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }
  std::string json = radio->GetStationsJson();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t RadioWebServer::HandleStationsPost(httpd_req_t* req) {
  auto* radio = Application::GetInstance().GetRadio();
  if (!radio) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "radio not available");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }

  std::string body = ReadBody(req);
  cJSON* json = cJSON_Parse(body.c_str());
  if (!json || !cJSON_IsObject(json)) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "invalid JSON");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    if (json) cJSON_Delete(json);
    return r;
  }

  cJSON* key = cJSON_GetObjectItem(json, "key");
  cJSON* name = cJSON_GetObjectItem(json, "name");
  cJSON* url = cJSON_GetObjectItem(json, "url");
  cJSON* genre = cJSON_GetObjectItem(json, "genre");
  cJSON* volume = cJSON_GetObjectItem(json, "volume");
  cJSON* codec = cJSON_GetObjectItem(json, "codec");

  if (!name || !cJSON_IsString(name) || !url || !cJSON_IsString(url)) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "name and url are required");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    cJSON_Delete(json);
    return r;
  }

  std::string k = key && cJSON_IsString(key) ? key->valuestring :
                  ("CUSTOM_" + std::string(name->valuestring));
  // sanitize key: replace spaces with underscore
  for (auto& ch : k) if (ch == ' ') ch = '_';

  bool ok = radio->AddOrUpdateStation(
      k, name->valuestring, url->valuestring,
      genre && cJSON_IsString(genre) ? genre->valuestring : "",
      volume && cJSON_IsNumber(volume) ? (float)volume->valuedouble : 1.0f,
      codec && cJSON_IsNumber(codec) ? codec->valueint : 0);

  cJSON* resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "ok", ok);
  cJSON_AddStringToObject(resp, "key", k.c_str());
  esp_err_t r = SendJson(req, resp);
  cJSON_Delete(resp);
  cJSON_Delete(json);
  return r;
}

// Extract "<key>" from "/radio/api/stations/<key>[/play]" and URL-decode it.
static std::string ExtractStationKey(const char* uri, const char* prefix) {
  const char* start = strstr(uri, prefix);
  if (!start) {
    return std::string();
  }
  std::string key = start + strlen(prefix);

  // Strip optional trailing "/play"
  const std::string play_suffix = "/play";
  if (key.size() >= play_suffix.size() &&
      key.compare(key.size() - play_suffix.size(), play_suffix.size(), play_suffix) == 0) {
    key.erase(key.size() - play_suffix.size());
  }

  // Percent-decode (%XX) — the web page sends encodeURIComponent()
  std::string decoded;
  decoded.reserve(key.size());
  for (size_t i = 0; i < key.size(); ++i) {
    if (key[i] == '%' && i + 2 < key.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = hex(key[i + 1]);
      int lo = hex(key[i + 2]);
      if (hi >= 0 && lo >= 0) {
        decoded.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if (key[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(key[i]);
    }
  }
  return decoded;
}

esp_err_t RadioWebServer::HandleStationPlay(httpd_req_t* req) {
  auto* radio = Application::GetInstance().GetRadio();
  if (!radio) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "radio not available");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }

  std::string key = ExtractStationKey(req->uri, "/radio/api/stations/");
  if (key.empty() || key.find('/') != std::string::npos) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "invalid URI");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }

  bool ok = radio->PlayStation(key);

  cJSON* resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "ok", ok);
  cJSON_AddStringToObject(resp, "key", key.c_str());
  if (!ok) {
    cJSON_AddStringToObject(resp, "error", "station not found");
  }
  esp_err_t r = SendJson(req, resp);
  cJSON_Delete(resp);
  return r;
}

esp_err_t RadioWebServer::HandleStationDelete(httpd_req_t* req) {
  auto* radio = Application::GetInstance().GetRadio();
  if (!radio) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "radio not available");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }

  // URI is /radio/api/stations/<key>
  const char* uri = req->uri;
  const char* prefix = "/radio/api/stations/";
  const char* key_start = strstr(uri, prefix);
  if (!key_start) {
    cJSON* err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "invalid URI");
    esp_err_t r = SendJson(req, err);
    cJSON_Delete(err);
    return r;
  }
  key_start += strlen(prefix);
  std::string key = key_start;

  bool ok = radio->DeleteStation(key);

  cJSON* resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "ok", ok);
  esp_err_t r = SendJson(req, resp);
  cJSON_Delete(resp);
  return r;
}

}  // namespace radio
