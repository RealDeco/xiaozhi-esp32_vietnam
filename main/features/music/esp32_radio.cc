#include "esp32_radio.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <thread>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cJSON.h>
#include "settings.h"

#define TAG "Esp32Radio"

// The built-in LVGL text fonts (font_puhui_basic_*) contain no Cyrillic glyphs,
// so Russian station names would render as blank boxes on the LCD.
// Transliterate Cyrillic to Latin for on-screen text only (web UI and voice
// control keep using the original Russian names).
static std::string ToDisplayAsciiImpl(const std::string& in) {
    static const char* kUpper[32] = {
        "A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L", "M", "N",
        "O", "P", "R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Shch",
        "", "Y", "", "E", "Yu", "Ya"
    };
    static const char* kLower[32] = {
        "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n",
        "o", "p", "r", "s", "t", "u", "f", "kh", "ts", "ch", "sh", "shch",
        "", "y", "", "e", "yu", "ya"
    };

    std::string out;
    out.reserve(in.size() + 16);
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == 0xD0 && i + 1 < in.size()) {
            unsigned char d = static_cast<unsigned char>(in[i + 1]);
            if (d == 0x81) { out += "Yo"; i += 2; continue; }          // Ё
            if (d >= 0x90 && d <= 0xAF) { out += kUpper[d - 0x90]; i += 2; continue; }
        } else if (c == 0xD1 && i + 1 < in.size()) {
            unsigned char d = static_cast<unsigned char>(in[i + 1]);
            if (d == 0x91) { out += "yo"; i += 2; continue; }          // ё
            if (d >= 0x80 && d <= 0xAF) { out += kLower[d - 0x80]; i += 2; continue; }
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        }
        // Non-ASCII characters without a glyph in the built-in font are dropped.
        ++i;
    }
    return out;
}

std::string Esp32Radio::ToDisplayAscii(const std::string& text) {
    return ToDisplayAsciiImpl(text);
}

Esp32Radio::Esp32Radio()
    : current_station_name_()
    , current_station_url_()
    , station_name_displayed_(false)
    , current_station_volume_(1.0f)
    , current_station_codec_(STATION_CODEC_MP3)
    , radio_stations_()
    , display_mode_(DISPLAY_MODE_SPECTRUM)
    , is_playing_(false)
    , is_downloading_(false)
    , play_thread_()
    , download_thread_()
    , audio_buffer_()
    , buffer_mutex_()
    , buffer_cv_()
    , buffer_size_(0)
    , decoder_(nullptr)
    , dec_info_()
    , decoder_initialized_(false)
    , dec_info_ready_(false)
    , dec_out_buffer_() {
}

Esp32Radio::~Esp32Radio() {
    ESP_LOGI(TAG, "Destroying radio player - stopping all operations");

    is_downloading_ = false;
    is_playing_ = false;

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish");
        download_thread_.join();
    }

    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish");
        play_thread_.join();
    }

    ClearAudioBuffer();
    CleanupDecoder();

    ESP_LOGI(TAG, "Radio player destroyed successfully");
}

void Esp32Radio::Initialize() {
    ESP_LOGI(TAG, "Radio player initialized with MP3 decoder support");
    InitializeRadioStations();
}

void Esp32Radio::InitializeRadioStations() {
    radio_stations_.clear();

    // Danmarks Radio (DR) MP3 streams
radio_stations_["P1"]             = RadioStation("Danish P1",           "http://live-icy.gss.dr.dk:8000/A/A03L.mp3",              "Danmarks Radio P1",       "News/Talk",      1.0f);
radio_stations_["P2"]             = RadioStation("Danish P2",           "http://live-icy.gss.dr.dk:8000/A/A04L.mp3",              "Danmarks Radio P2",       "Culture/Music",  1.0f);
radio_stations_["P3"]             = RadioStation("Danish P3",           "http://live-icy.gss.dr.dk:8000/A/A05L.mp3",              "Danmarks Radio P3",       "Pop",            1.0f);
radio_stations_["P4"]             = RadioStation("Danish P4",           "http://live-icy.gss.dr.dk:8000/A/A08L.mp3",              "Danmarks Radio P4",       "Regional",       1.0f);
radio_stations_["P5"]             = RadioStation("Danish P5",           "http://live-icy.gss.dr.dk:8000/A/A25L.mp3",              "Danmarks Radio P5",       "News/Music",     1.0f);

    // Other Danish Channels
radio_stations_["THE_VOICE"]      = RadioStation("The Voice",           "http://live-bauerdk.sharp-stream.com/voice128.mp3",      "The Voice Denmark",       "Pop",            1.0f);
radio_stations_["NOVA_FM"]        = RadioStation("Nova FM",             "http://live-bauerdk.sharp-stream.com/nova128.mp3",       "Nova FM Denmark",         "Pop",            1.0f);
radio_stations_["RADIO_100"]      = RadioStation("Radio 100",           "https://live-bauerdk.sharp-stream.com/radio100_dk_mp3",  "Radio 100 Denmark",       "Pop",            1.0f);
radio_stations_["RADIO_BOOST"]    = RadioStation("Radio Boost",         "http://6434.cloudrad.io:8066/live",                      "De Unges Pust",           "Electronic",     1.0f);

    // UK MP3 streams
radio_stations_["RADIO_CAROLINE"] = RadioStation("Radio Caroline",      "http://sc6.radiocaroline.net:8040/mp3",                  "Radio Caroline UK",       "Classic Rock",   1.0f);
radio_stations_["CAPITAL_FM"]     = RadioStation("Capital FM UK",       "https://ice-sov.musicradio.com/CapitalMP3",              "Capital FM London",       "Pop",            1.0f);
radio_stations_["CAPITAL_DANCE"]  = RadioStation("Capital Dance",       "https://icecast.thisisdax.com/CapitalDanceMP3",          "Capital Dance",           "Dance",          1.0f);
radio_stations_["SMOOTH_RADIO"]   = RadioStation("Smooth Radio",        "https://icecast.thisisdax.com/SmoothUKMP3",              "Smooth Radio UK",         "Easy Listening", 1.0f);
radio_stations_["SMOOTH_CHILL"]   = RadioStation("Smooth Chill",        "https://ice-sov.musicradio.com/SmoothChillMP3",          "Smooth Chill",            "Chillout",       1.0f);
radio_stations_["Q_RADIO"]        = RadioStation("Q Radio",             "https://edge-audio-04-thn.sharp-stream.com/qr1029mobile.mp3", "Better Music, Less Talk", "Pop",       1.0f);

    // Swedish MP3 streams
radio_stations_["NRJ_FM"]           = RadioStation("Swedish NRJ FM",    "https://edge-bauerse-02-thn.sharp-stream.com/nrj_instreamtest_se_mp3", "Swedish NRJ Radio", "Pop/Dance", 1.0f);
radio_stations_["SRP1"]           = RadioStation("Swedish P1",          "https://http-live.sr.se/p1-mp3-192",                     "Swedish Radio P1",        "News/Talk",      1.0f);
radio_stations_["SRP3"]           = RadioStation("Swedish P3",          "https://http-live.sr.se/p3-mp3-192",                     "Swedish Radio P3",        "Pop",            1.0f);
radio_stations_["SRP4"]           = RadioStation("Swedish P4",          "https://http-live.sr.se/p4malmo-mp3-192",                "Swedish Radio P4 Malmö",  "Regional",       1.0f);
radio_stations_["RIX"]            = RadioStation("Swedish Rix FM",      "https://fm01-ice.stream.khz.se/fm01_mp3",                "Rix FM Sweden",           "Pop",            1.0f);
radio_stations_["RETRO"]          = RadioStation("Swedish Retro FM",    "https://live-bauerse-fm.sharp-stream.com/retrofm_mp3",   "Retro FM Sweden",         "Classic Hits",   1.0f);

   // International MP3 streams
radio_stations_["1MIX"]           = RadioStation("1Mix Radio",          "http://fr1.1mix.co.uk:8060/128b",                        "1Mix Radio",              "Electronic",     1.0f);
radio_stations_["PSYRADIO"]       = RadioStation("Psyradio",            "http://komplex2.psyradio.org:8040/stream",               "Psyradio",                "Psytrance",      1.0f);
radio_stations_["BRIGADA"]        = RadioStation("Brigada News",        "https://makatistream.brigadanews.ph",        "Brigada News FM Philippines Makati",  "News/Talk",      1.5f);
radio_stations_["K-POP"]        = RadioStation("Only Hit - KPop",        "https://ais-sa3.cdnstream1.com/2630_128.mp3",        "K-POP",  "Pop",      1.5f);

    // Russian MP3 streams
    radio_stations_["MARUSYAFM"]      = RadioStation("Маруся FM",           "https://msk3.radio-holding.ru/marusya_default",          "Маруся FM",               "Pop/Dance",      1.2f);
    radio_stations_["RUSSKOE_RADIO"]  = RadioStation("Русское Радио",       "https://rusradio.hostingradio.ru/rusradio96.aacp",     "Русское Радио",           "Pop",            1.0f, STATION_CODEC_AAC);
    radio_stations_["EUROPA_PLUS"]    = RadioStation("Европа Плюс",         "https://ep256.hostingradio.ru:8052/europaplus256.mp3",   "Европа Плюс",             "Pop/Dance",      1.0f);


    ESP_LOGI(TAG, "Initialized %d radio stations (MP3)", (int)radio_stations_.size());

    // Load user-defined (custom) stations stored in NVS
    LoadCustomStationsFromNvs();
}

bool Esp32Radio::PlayStation(const std::string& station_name) {
    ESP_LOGI(TAG, "Request to play radio station: %s", station_name.c_str());

    std::string lower_input = station_name;
    std::transform(lower_input.begin(), lower_input.end(), lower_input.begin(), ::tolower);

    // Match by display name (partial)
    for (const auto& station : radio_stations_) {
        std::string lower_station_name = station.second.name;
        std::transform(lower_station_name.begin(), lower_station_name.end(), lower_station_name.begin(), ::tolower);

        if (lower_station_name.find(lower_input) != std::string::npos ||
            lower_input.find(lower_station_name) != std::string::npos) {
            ESP_LOGI(TAG, "Found station by display name: '%s' -> %s (volume: %.1fx)",
                     station_name.c_str(), station.second.name.c_str(), station.second.volume);
            current_station_volume_ = station.second.volume;
            current_station_codec_ = station.second.codec;
            return PlayUrl(station.second.url, station.second.name);
        }
    }

    // Match by key exact
    auto it = radio_stations_.find(station_name);
    if (it != radio_stations_.end()) {
        ESP_LOGI(TAG, "Found station by key: '%s' -> %s (volume: %.1fx)",
                 station_name.c_str(), it->second.name.c_str(), it->second.volume);
        current_station_volume_ = it->second.volume;
        current_station_codec_ = it->second.codec;
        return PlayUrl(it->second.url, it->second.name);
    }

    // Match by key case-insensitive
    for (const auto& station : radio_stations_) {
        std::string lower_key = station.first;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        if (lower_key == lower_input) {
            ESP_LOGI(TAG, "Found station by key (case-insensitive): '%s' -> %s (volume: %.1fx)",
                     station_name.c_str(), station.second.name.c_str(), station.second.volume);
            current_station_volume_ = station.second.volume;
            current_station_codec_ = station.second.codec;
            return PlayUrl(station.second.url, station.second.name);
        }
    }

    ESP_LOGE(TAG, "Radio station not found: %s", station_name.c_str());
    return false;
}

bool Esp32Radio::NextStation() {
    if (radio_stations_.empty()) {
        ESP_LOGW(TAG, "No stations available");
        return false;
    }

    // Build an ordered list of stations (map is ordered by key)
    std::vector<std::map<std::string, RadioStation>::const_iterator> stations;
    stations.reserve(radio_stations_.size());
    for (auto it = radio_stations_.begin(); it != radio_stations_.end(); ++it) {
        stations.push_back(it);
    }

    // Find the currently playing station by URL
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < stations.size(); ++i) {
        if (stations[i]->second.url == current_station_url_) {
            index = i;
            found = true;
            break;
        }
    }

    size_t next = found ? (index + 1) % stations.size() : 0;
    const auto& st = stations[next]->second;
    ESP_LOGI(TAG, "Next station: %s (%s)", st.name.c_str(), st.url.c_str());

    current_station_volume_ = st.volume;
    current_station_codec_ = st.codec;
    return PlayUrl(st.url, st.name);
}

bool Esp32Radio::PreviousStation() {
    if (radio_stations_.empty()) {
        ESP_LOGW(TAG, "No stations available");
        return false;
    }

    std::vector<std::map<std::string, RadioStation>::const_iterator> stations;
    stations.reserve(radio_stations_.size());
    for (auto it = radio_stations_.begin(); it != radio_stations_.end(); ++it) {
        stations.push_back(it);
    }

    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < stations.size(); ++i) {
        if (stations[i]->second.url == current_station_url_) {
            index = i;
            found = true;
            break;
        }
    }

    size_t prev = found ? (index + stations.size() - 1) % stations.size() : 0;
    const auto& st = stations[prev]->second;
    ESP_LOGI(TAG, "Previous station: %s (%s)", st.name.c_str(), st.url.c_str());

    current_station_volume_ = st.volume;
    current_station_codec_ = st.codec;
    return PlayUrl(st.url, st.name);
}

void Esp32Radio::LoadCustomStationsFromNvs() {
    Settings settings("radio", true);
    std::string keys_json = settings.GetString("station_keys", "[]");
    cJSON* keys_arr = cJSON_Parse(keys_json.c_str());
    if (!keys_arr || !cJSON_IsArray(keys_arr)) {
        if (keys_arr) cJSON_Delete(keys_arr);
        ESP_LOGW(TAG, "Failed to parse station keys from NVS");
        return;
    }

    int count = cJSON_GetArraySize(keys_arr);
    for (int i = 0; i < count; ++i) {
        cJSON* key_item = cJSON_GetArrayItem(keys_arr, i);
        if (!key_item || !cJSON_IsString(key_item)) continue;
        std::string key = key_item->valuestring;

        std::string station_json = settings.GetString("station_" + key, "{}");
        cJSON* st = cJSON_Parse(station_json.c_str());
        if (!st || !cJSON_IsObject(st)) {
            if (st) cJSON_Delete(st);
            continue;
        }

        cJSON* name = cJSON_GetObjectItem(st, "name");
        cJSON* url = cJSON_GetObjectItem(st, "url");
        cJSON* genre = cJSON_GetObjectItem(st, "genre");
        cJSON* volume = cJSON_GetObjectItem(st, "volume");
        cJSON* codec = cJSON_GetObjectItem(st, "codec");

        if (name && url && cJSON_IsString(name) && cJSON_IsString(url)) {
            radio_stations_[key] = RadioStation(
                name->valuestring,
                url->valuestring,
                genre && cJSON_IsString(genre) ? genre->valuestring : "",
                "",
                volume && cJSON_IsNumber(volume) ? (float)volume->valuedouble : 1.0f,
                codec && cJSON_IsNumber(codec) ? codec->valueint : STATION_CODEC_MP3);
            saved_station_keys_.insert(key);
            ESP_LOGI(TAG, "Loaded custom station: %s (%s)", name->valuestring, url->valuestring);
        }
        cJSON_Delete(st);
    }
    cJSON_Delete(keys_arr);
    ESP_LOGI(TAG, "Loaded %d custom stations from NVS", count);
}

std::string Esp32Radio::GetStationsJson() const {
    cJSON* root = cJSON_CreateArray();
    for (std::map<std::string, RadioStation>::const_iterator it = radio_stations_.begin();
         it != radio_stations_.end(); ++it) {
        const std::string& key = it->first;
        const RadioStation& station = it->second;
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", key.c_str());
        cJSON_AddStringToObject(item, "name", station.name.c_str());
        cJSON_AddStringToObject(item, "url", station.url.c_str());
        cJSON_AddStringToObject(item, "genre", station.genre.c_str());
        cJSON_AddNumberToObject(item, "volume", station.volume);
        cJSON_AddNumberToObject(item, "codec", station.codec);
        cJSON_AddItemToArray(root, item);
    }
    char* json = cJSON_PrintUnformatted(root);
    std::string result = json ? json : "[]";
    if (json) cJSON_free(json);
    cJSON_Delete(root);
    return result;
}

bool Esp32Radio::AddOrUpdateStation(const std::string& key, const std::string& name,
                                    const std::string& url, const std::string& genre,
                                    float volume, int codec) {
    if (key.empty() || name.empty() || url.empty()) {
        ESP_LOGE(TAG, "Invalid station parameters (key/name/url empty)");
        return false;
    }

    // Update in-memory map
    radio_stations_[key] = RadioStation(name, url, "", genre, volume, codec);

    // Persist to NVS
    Settings settings("radio", true);

    cJSON* st = cJSON_CreateObject();
    cJSON_AddStringToObject(st, "name", name.c_str());
    cJSON_AddStringToObject(st, "url", url.c_str());
    cJSON_AddStringToObject(st, "genre", genre.c_str());
    cJSON_AddNumberToObject(st, "volume", volume);
    cJSON_AddNumberToObject(st, "codec", codec);
    char* st_json = cJSON_PrintUnformatted(st);
    settings.SetString("station_" + key, st_json ? st_json : "{}");
    if (st_json) cJSON_free(st_json);
    cJSON_Delete(st);

    // Remember that this station has an NVS override (so edits to built-in
    // stations also survive a reboot).
    saved_station_keys_.insert(key);
    SaveStationKeysToNvs();

    ESP_LOGI(TAG, "Station '%s' saved to NVS", key.c_str());
    return true;
}

// Rebuild the list of station keys that must be restored from NVS on boot.
void Esp32Radio::SaveStationKeysToNvs() {
    Settings settings("radio", true);
    cJSON* keys_arr = cJSON_CreateArray();
    for (const auto& k : saved_station_keys_) {
        if (radio_stations_.find(k) != radio_stations_.end()) {
            cJSON_AddItemToArray(keys_arr, cJSON_CreateString(k.c_str()));
        }
    }
    char* keys_json = cJSON_PrintUnformatted(keys_arr);
    settings.SetString("station_keys", keys_json ? keys_json : "[]");
    if (keys_json) cJSON_free(keys_json);
    cJSON_Delete(keys_arr);
}

bool Esp32Radio::DeleteStation(const std::string& key) {
    if (key.rfind("CUSTOM_", 0) != 0) {
        ESP_LOGW(TAG, "Cannot delete built-in station: %s", key.c_str());
        return false;
    }

    auto it = radio_stations_.find(key);
    if (it == radio_stations_.end()) {
        return false;
    }

    radio_stations_.erase(it);
    saved_station_keys_.erase(key);

    Settings settings("radio", true);
    settings.EraseKey("station_" + key);
    SaveStationKeysToNvs();

    ESP_LOGI(TAG, "Station '%s' deleted", key.c_str());
    return true;
}

bool Esp32Radio::PlayUrl(const std::string& radio_url, const std::string& station_name) {
    if (radio_url.empty()) {
        ESP_LOGE(TAG, "Radio URL is empty");
        return false;
    }

    ESP_LOGI(TAG, "Starting radio stream: %s (%s)",
             station_name.empty() ? "Custom URL" : station_name.c_str(),
             radio_url.c_str());

    Stop();

    // Clean display RAM before starting radio
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->StopFFT();
        display->ReleaseAudioBuffFFT();
        display->SetMusicInfo(nullptr);
        ESP_LOGI(TAG, "[PATCH] Display memory released before starting radio");
    }

    current_station_url_ = radio_url;
    current_station_name_ = station_name.empty() ? "Custom Radio" : station_name;
    station_name_displayed_ = false;

    if (current_station_volume_ <= 0.0f) {
        current_station_volume_ = 1.0f; // sensible default
    }

    // Keep the radio at the assistant's current volume. The station volume
    // multiplier is NOT applied any more: it used to be applied both to the codec
    // volume and to the PCM samples, which made the radio much louder than the
    // assistant and also overwrote the saved assistant volume.
    auto codec_for_volume = Board::GetInstance().GetAudioCodec();
    if (codec_for_volume && codec_for_volume->output_volume() == 0) {
        // Playback is resuming: restore the level that was active before muting.
        codec_for_volume->Unmute();
        ESP_LOGI(TAG, "Unmuted before radio playback, restored volume %d",
                 codec_for_volume->output_volume());
    }

    ClearAudioBuffer();

    // Configure pthread - use a generous stack size for the streaming threads
    // (MP3 decoding + HTTPS download buffering) to avoid stack overflow panics.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 1024 * 10;
    cfg.prio = 5;
    cfg.thread_name = "radio_stream";
    esp_pthread_set_cfg(&cfg);

    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Radio::DownloadRadioStream, this, radio_url);

    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Radio::PlayRadioStream, this);

    ESP_LOGI(TAG, "Radio streaming threads started successfully");
    return true;
}

bool Esp32Radio::Stop() {
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress to stop");
        return true;
    }

    ESP_LOGI(TAG, "Stopping radio streaming - downloading=%d, playing=%d",
             is_downloading_.load(), is_playing_.load());

    ResetSampleRate();

    is_downloading_ = false;
    is_playing_ = false;

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetMusicInfo("");
        ESP_LOGI(TAG, "Cleared radio station display");
    }

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (download_thread_.joinable()) {
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread joined in Stop");
    }

    if (play_thread_.joinable()) {
        play_thread_.join();
        ESP_LOGI(TAG, "Play thread joined in Stop");
    }

    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
        display->StopFFT();
        ESP_LOGI(TAG, "Stopped FFT display in Stop (spectrum mode)");
    }

    ESP_LOGI(TAG, "Radio streaming stopped successfully");
    return true;
}

std::vector<std::string> Esp32Radio::GetStationList() const {
    std::vector<std::string> station_list;
    for (const auto& station : radio_stations_) {
        station_list.push_back(station.first + " - " + station.second.name);
    }
    return station_list;
}

void Esp32Radio::DownloadRadioStream(const std::string& radio_url) {
    ESP_LOGD(TAG, "Starting radio stream download from: %s", radio_url.c_str());

    if (radio_url.empty() || radio_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL format: %s", radio_url.c_str());
        is_downloading_ = false;
        return;
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);

    http->SetHeader("User-Agent", "ESP32-Radio/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");

    // Important for ICY streams: ask server NOT to inject metadata into the stream
    http->SetHeader("Icy-MetaData", "0");

    bool is_https = (radio_url.find("https://") == 0);
    ESP_LOGI(TAG, "Connecting to %s stream: %s", is_https ? "HTTPS" : "HTTP", radio_url.c_str());

    auto display = Board::GetInstance().GetDisplay();

    // Resolve HTTP redirects (many radio providers hand out a load-balancer URL
    // that answers 307 to the real stream host).
    std::string stream_url = radio_url;
    for (int hop = 0; hop < 5; ++hop) {
        if (!http->Open("GET", stream_url)) {
            ESP_LOGE(TAG, "Failed to connect to radio stream URL: %s", stream_url.c_str());
            is_downloading_ = false;
            if (display) display->SetMusicInfo("Radio connection error");
            return;
        }

        int hop_status = http->GetStatusCode();
        if (hop_status < 300 || hop_status >= 400) {
            break;  // final response, connection stays open
        }

        std::string location = http->GetResponseHeader("Location");
        http->Close();
        if (location.empty()) {
            ESP_LOGE(TAG, "HTTP %d redirect without Location header", hop_status);
            is_downloading_ = false;
            return;
        }

        if (location.rfind("http", 0) != 0) {
            // Relative redirect: rebuild absolute URL from the current one
            size_t scheme_end = stream_url.find("://");
            size_t host_end = (scheme_end == std::string::npos)
                                  ? std::string::npos
                                  : stream_url.find('/', scheme_end + 3);
            std::string base = (host_end == std::string::npos) ? stream_url
                                                               : stream_url.substr(0, host_end);
            location = base + (location[0] == '/' ? "" : "/") + location;
        }

        ESP_LOGW(TAG, "HTTP %d redirect -> %s", hop_status, location.c_str());
        stream_url = location;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }

    ESP_LOGI(TAG, "Started downloading radio stream, status: %d", status_code);

    const size_t chunk_size = 4096;
    char* buffer = new char[chunk_size];
    size_t total_downloaded = 0;
    size_t total_print_bytes = 0;
    const int kMaxReconnectAttempts = 3;
    int reconnect_attempts = 0;

    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(buffer, chunk_size);

        if (bytes_read <= 0) {
            reconnect_attempts++;
            ESP_LOGW(TAG, "Stream lost (bytes_read=%d), reconnect (%d/%d)...",
                     bytes_read, reconnect_attempts, kMaxReconnectAttempts);

            if (display) {
                display->SetMusicInfo("Radio disconnected...\nRetrying...");
            }

            if (reconnect_attempts > kMaxReconnectAttempts) {
                ESP_LOGE(TAG, "Exceeded max reconnect attempts");
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1500));
            http->Close();

            if (!http->Open("GET", stream_url)) {
                ESP_LOGE(TAG, "Reconnect failed at attempt %d", reconnect_attempts);
                continue;
            } else {
                ESP_LOGI(TAG, "Reconnect success at attempt %d", reconnect_attempts);
                continue;
            }
        }

        reconnect_attempts = 0;

        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for radio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);

        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });

            if (is_downloading_) {
                audio_buffer_.push(RadioAudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                total_print_bytes += bytes_read;
                buffer_cv_.notify_one();

                if (total_print_bytes >= (128 * 1024)) {
                    total_print_bytes = 0;
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d",
                             (int)total_downloaded, (int)buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }

    delete[] buffer;
    http->Close();

    is_downloading_ = false;

    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    if (total_downloaded < 1024 && display) {
        display->SetMusicInfo("Cannot connect radio.");
    }

    ESP_LOGI(TAG, "Radio stream download thread finished");
}

void Esp32Radio::PlayRadioStream() {
    ESP_LOGI(TAG, "Starting radio stream playback with MP3 decoder");

    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        return;
    }

    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }

    if (!InitializeDecoder()) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        is_playing_ = false;
        return;
    }

    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] {
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty());
        });
    }

    ESP_LOGI(TAG, "Starting radio playback with buffer size: %d", (int)buffer_size_);

    const int INPUT_BUF_SIZE = 8192;
    uint8_t* input_buffer = (uint8_t*)heap_caps_malloc(INPUT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate input buffer");
        is_playing_ = false;
        return;
    }

    size_t total_played_bytes = 0;
    size_t total_print_bytes = 0;

    int bytes_left = 0;
    uint8_t* read_ptr = input_buffer;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    while (is_playing_) {
        // only play when idle
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();

        if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
            app.ToggleChatState();
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        } else if (current_state != kDeviceStateIdle) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Display station once
        if (!station_name_displayed_ && !current_station_name_.empty()) {
            if (display) {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                    display->StartFFT();
                }
            }

            if (display) {
                std::string formatted =
                    "Radio: " + ToDisplayAscii(current_station_name_) + "\nPlaying...";
                display->SetMusicInfo(formatted.c_str());
                station_name_displayed_ = true;
            }
        }

        // Need more input data?
        if (bytes_left < 4096) {
            RadioAudioChunk chunk;

            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_) {
                        ESP_LOGI(TAG, "Radio stream ended, total played: %d bytes", (int)total_played_bytes);
                        break;
                    }
                    buffer_cv_.wait(lock, [this] { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }

                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                total_played_bytes += chunk.size;
                total_print_bytes += chunk.size;

                buffer_cv_.notify_one();
            }

            if (chunk.data && chunk.size > 0) {
                // Move remaining data to start
                if (bytes_left > 0 && read_ptr != input_buffer) {
                    memmove(input_buffer, read_ptr, bytes_left);
                }
                read_ptr = input_buffer;

                // Copy new data
                size_t space_available = (size_t)(INPUT_BUF_SIZE - bytes_left);
                size_t copy_size = std::min(chunk.size, space_available);
                memcpy(input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += (int)copy_size;

                heap_caps_free(chunk.data);

                // Skip ID3v2 tag if present
                if (bytes_left >= 10 && memcmp(read_ptr, "ID3", 3) == 0) {
                    size_t skip = SkipId3Tag(read_ptr, (size_t)bytes_left);
                    read_ptr += skip;
                    bytes_left -= (int)skip;
                }
            }
        }

        if (bytes_left <= 0) {
            continue;
        }

        bool input_eos = (!is_downloading_ && audio_buffer_.empty());

        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = read_ptr;
        raw.len = (uint32_t)bytes_left;
        raw.eos = input_eos;

        esp_audio_simple_dec_out_t out_frame = {};
        out_frame.buffer = dec_out_buffer_.data();
        out_frame.len = (uint32_t)dec_out_buffer_.size();

        while (raw.len > 0 && is_playing_) {
            esp_audio_err_t dec_ret = esp_audio_simple_dec_process(decoder_, &raw, &out_frame);

            if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                dec_out_buffer_.resize(out_frame.needed_size);
                out_frame.buffer = dec_out_buffer_.data();
                out_frame.len = (uint32_t)dec_out_buffer_.size();
                continue;
            }

            if (dec_ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGE(TAG, "MP3 decode error: %d", dec_ret);
                is_playing_ = false;
                break;
            }

            if (out_frame.decoded_size > 0) {
                if (!dec_info_ready_) {
                    if (esp_audio_simple_dec_get_info(decoder_, &dec_info_) == ESP_AUDIO_ERR_OK) {
                        dec_info_ready_ = true;
                        ESP_LOGI(TAG, "MP3 stream info: %d Hz, %d bits, %d ch",
                                 (int)dec_info_.sample_rate,
                                 (int)dec_info_.bits_per_sample,
                                 (int)dec_info_.channel);

                        if (display) {
                            std::ostringstream oss;
                            oss << "RADIO: " << ToDisplayAscii(current_station_name_) << "\n"
                                << "MP3 " << dec_info_.sample_rate << "Hz  "
                                << (int)dec_info_.bits_per_sample << "bit  "
                                << (int)dec_info_.channel << "ch";
                            display->SetMusicInfo(oss.str().c_str());
                        }
                    }
                }

                int bits_per_sample = (dec_info_.bits_per_sample > 0) ? dec_info_.bits_per_sample : 16;
                int bytes_per_sample = bits_per_sample / 8;
                int channels = (dec_info_.channel > 0) ? dec_info_.channel : 2;

                int total_samples = (int)(out_frame.decoded_size / (uint32_t)bytes_per_sample);
                int samples_per_channel = (channels > 0) ? (total_samples / channels) : total_samples;

                int16_t* pcm_in = reinterpret_cast<int16_t*>(out_frame.buffer);
                std::vector<int16_t> mono_buffer;
                int16_t* final_pcm_data = nullptr;
                int final_sample_count = 0;

                if (channels == 2) {
                    mono_buffer.resize(samples_per_channel);
                    for (int i = 0; i < samples_per_channel; ++i) {
                        int left = pcm_in[i * 2];
                        int right = pcm_in[i * 2 + 1];
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = samples_per_channel;
                } else {
                    final_pcm_data = pcm_in;
                    final_sample_count = total_samples;
                }

                // No extra station amplification: the radio plays at the same
                // level as the assistant (the codec volume already reflects it).
                AudioStreamPacket packet;
                packet.sample_rate = dec_info_.sample_rate;
                packet.frame_duration = 60;
                packet.timestamp = 0;

                size_t pcm_size_bytes = (size_t)final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
                    final_pcm_data_fft = display->MakeAudioBuffFFT(pcm_size_bytes);
                    display->FeedAudioDataFFT(final_pcm_data, pcm_size_bytes);
                }

                app.AddAudioData(std::move(packet));

                if (total_print_bytes >= (128 * 1024)) {
                    total_print_bytes = 0;
                    ESP_LOGI(TAG, "MP3: Played %d bytes, buffer size: %d",
                             (int)total_played_bytes, (int)buffer_size_);
                }
            }

            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;
        }

        bytes_left = (int)raw.len;
        read_ptr = raw.buffer;

        if (input_eos && bytes_left == 0) {
            ESP_LOGI(TAG, "MP3 radio stream ended");
            break;
        }
    }

    heap_caps_free(input_buffer);

    if (is_playing_) {
        ClearAudioBuffer();
        ResetSampleRate();
    }

    CleanupDecoder();

    is_playing_ = false;

    if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
        if (display) {
            display->StopFFT();
            display->ReleaseAudioBuffFFT();
        }
    }

    ESP_LOGI(TAG, "Radio playback thread finished");
}

void Esp32Radio::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    while (!audio_buffer_.empty()) {
        RadioAudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }

    buffer_size_ = 0;
    ESP_LOGI(TAG, "Radio audio buffer cleared");
}

void Esp32Radio::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 &&
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
        ESP_LOGI(TAG, "Resetting sample rate: from %d Hz back to original %d Hz",
                 codec->output_sample_rate(), codec->original_output_sample_rate());
        codec->SetOutputSampleRate(-1);
    }
}

size_t Esp32Radio::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) return 0;
    if (memcmp(data, "ID3", 3) != 0) return 0;

    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));

    size_t total_skip = 10 + tag_size;
    if (total_skip > size) total_skip = size;

    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

bool Esp32Radio::InitializeDecoder() {
    if (decoder_initialized_) {
        ESP_LOGW(TAG, "Decoder already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing MP3/AAC Simple Decoder for radio streams");

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    esp_audio_simple_dec_cfg_t cfg = {};
    cfg.dec_cfg = nullptr;
    cfg.cfg_size = 0;
    cfg.use_frame_dec = false;

    // Select decoder based on the station's known codec (with fallback).
    bool prefer_aac = (current_station_codec_ == STATION_CODEC_AAC);
    cfg.dec_type = prefer_aac ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC
                              : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    esp_audio_err_t ret = esp_audio_simple_dec_open(&cfg, &decoder_);
    if (ret != ESP_AUDIO_ERR_OK || !decoder_) {
        ESP_LOGW(TAG, "Failed to open %s decoder, trying %s...",
                 prefer_aac ? "AAC" : "MP3", prefer_aac ? "MP3" : "AAC");
        cfg.dec_type = prefer_aac ? ESP_AUDIO_SIMPLE_DEC_TYPE_MP3
                                  : ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        ret = esp_audio_simple_dec_open(&cfg, &decoder_);
        if (ret != ESP_AUDIO_ERR_OK || !decoder_) {
            ESP_LOGE(TAG, "Failed to open fallback decoder, ret=%d", ret);
            esp_audio_simple_dec_unregister_default();
            esp_audio_dec_unregister_default();
            return false;
        }
        ESP_LOGI(TAG, "%s Simple Decoder initialized (fallback)", prefer_aac ? "MP3" : "AAC");
    } else {
        ESP_LOGI(TAG, "%s Simple Decoder initialized", prefer_aac ? "AAC" : "MP3");
    }

    dec_out_buffer_.resize(4096);
    dec_info_ready_ = false;
    decoder_initialized_ = true;

    return true;
}

void Esp32Radio::CleanupDecoder() {
    if (!decoder_initialized_) return;

    if (decoder_) {
        esp_audio_simple_dec_close(decoder_);
        decoder_ = nullptr;
    }

    esp_audio_simple_dec_unregister_default();
    esp_audio_dec_unregister_default();

    dec_out_buffer_.clear();
    dec_info_ready_ = false;
    decoder_initialized_ = false;

    ESP_LOGI(TAG, "MP3 Simple Decoder cleaned up");
}

void Esp32Radio::SetDisplayMode(DisplayMode mode) {
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;

    ESP_LOGI(TAG, "Display mode changed from %s to %s",
             (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "INFO",
             (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "INFO");
}
