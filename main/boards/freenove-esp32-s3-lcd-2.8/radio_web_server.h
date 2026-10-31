#ifndef RADIO_WEB_SERVER_H_
#define RADIO_WEB_SERVER_H_

#include "esp_err.h"
#include "esp_http_server.h"

#include <string>

namespace radio {

// Web server for managing radio stations (add / edit / delete).
// Runs on port 8080 (OTA server uses port 80).
class RadioWebServer {
 public:
  static RadioWebServer& GetInstance();

  RadioWebServer(const RadioWebServer&) = delete;
  RadioWebServer& operator=(const RadioWebServer&) = delete;

  // Start the HTTP server on the given port.
  esp_err_t Start(int port = 8080);

  // Stop the HTTP server.
  void Stop();

  bool IsRunning() const { return server_handle_ != nullptr; }

 private:
  RadioWebServer() = default;
  ~RadioWebServer();

  httpd_handle_t server_handle_ = nullptr;

  // GET /radio  -> HTML page (Russian)
  static esp_err_t HandlePageGet(httpd_req_t* req);

  // GET /radio/api/stations  -> JSON list of all stations
  static esp_err_t HandleStationsGet(httpd_req_t* req);

  // POST /radio/api/stations  -> add / update station (JSON body)
  static esp_err_t HandleStationsPost(httpd_req_t* req);

  // POST /radio/api/stations/<key>/play  -> start playing the station
  static esp_err_t HandleStationPlay(httpd_req_t* req);

  // DELETE /radio/api/stations/<key>  -> delete station
  static esp_err_t HandleStationDelete(httpd_req_t* req);

  // GET  /radio/api/ota  -> current OTA version-check URL
  // POST /radio/api/ota  -> set OTA version-check URL (JSON body {"url": "..."})
  static esp_err_t HandleOtaUrlGet(httpd_req_t* req);
  static esp_err_t HandleOtaUrlPost(httpd_req_t* req);
};

}  // namespace radio

#endif  // RADIO_WEB_SERVER_H_
