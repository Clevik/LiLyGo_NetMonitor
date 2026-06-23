#include "keenetic_rci.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>

static constexpr uint32_t RCI_HTTP_TIMEOUT_MS = 2500;

static bool hasText(const char *value) {
  if (!value) return false;
  while (*value) {
    if (*value != ' ' && *value != '\t' && *value != '\r' && *value != '\n') {
      return true;
    }
    value++;
  }
  return false;
}

static String extractQuoted(const String &source, const char *key) {
  String needle = String(key) + "=\"";
  int start = source.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  int end = source.indexOf('"', start);
  if (end < 0) return "";
  return source.substring(start, end);
}

static String firstCookiePair(const String &setCookie) {
  int end = setCookie.indexOf(';');
  String cookie = end >= 0 ? setCookie.substring(0, end) : setCookie;
  cookie.trim();
  return cookie;
}

static void bytesToHex(const uint8_t *bytes, size_t len, char *out) {
  static constexpr char HEX_CHARS[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = HEX_CHARS[(bytes[i] >> 4) & 0x0F];
    out[i * 2 + 1] = HEX_CHARS[bytes[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

static bool md5Hex(const String &input, char out[33]) {
  uint8_t digest[16];
  int rc = mbedtls_md5_ret(reinterpret_cast<const uint8_t *>(input.c_str()),
                           input.length(), digest);
  if (rc != 0) return false;
  bytesToHex(digest, sizeof(digest), out);
  return true;
}

static bool sha256Hex(const String &input, char out[65]) {
  uint8_t digest[32];
  int rc = mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(input.c_str()),
                              input.length(), digest, 0);
  if (rc != 0) return false;
  bytesToHex(digest, sizeof(digest), out);
  return true;
}

static bool authenticate(IPAddress routerIp,
                         const char *login,
                         const char *password,
                         String &cookie) {
  String baseUrl = "http://" + routerIp.toString();
  const char *headers[] = {
    "WWW-Authenticate",
    "Set-Cookie",
    "X-NDM-Realm",
    "X-NDM-Challenge",
  };

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(RCI_HTTP_TIMEOUT_MS);
  http.collectHeaders(headers, 4);
  if (!http.begin(client, baseUrl + "/auth")) {
    return false;
  }
  int code = http.GET();
  String www = http.header("WWW-Authenticate");
  String setCookie = http.header("Set-Cookie");
  String realm = http.header("X-NDM-Realm");
  String challenge = http.header("X-NDM-Challenge");
  http.end();

  if (code != HTTP_CODE_UNAUTHORIZED) {
    Serial.printf("[RCI] auth challenge failed: http=%d\n", code);
    return false;
  }

  if (realm.length() == 0) realm = extractQuoted(www, "realm");
  if (challenge.length() == 0) challenge = extractQuoted(www, "challenge");
  cookie = firstCookiePair(setCookie);
  if (cookie.length() == 0) {
    String cookieName = extractQuoted(www, "session_cookie");
    String sessionId = extractQuoted(www, "session_id");
    if (cookieName.length() > 0 && sessionId.length() > 0) {
      cookie = cookieName + "=" + sessionId;
    }
  }

  if (realm.length() == 0 || challenge.length() == 0 || cookie.length() == 0) {
    Serial.println("[RCI] auth challenge is incomplete");
    return false;
  }

  char md5[33];
  char sha[65];
  if (!md5Hex(String(login) + ":" + realm + ":" + password, md5)) {
    Serial.println("[RCI] md5 failed");
    return false;
  }
  if (!sha256Hex(challenge + md5, sha)) {
    Serial.println("[RCI] sha256 failed");
    return false;
  }

  JsonDocument doc;
  doc["login"] = login;
  doc["password"] = sha;
  String body;
  serializeJson(doc, body);

  WiFiClient postClient;
  HTTPClient post;
  post.setTimeout(RCI_HTTP_TIMEOUT_MS);
  if (!post.begin(postClient, baseUrl + "/auth")) {
    return false;
  }
  post.addHeader("Content-Type", "application/json");
  post.addHeader("Cookie", cookie);
  code = post.POST(body);
  post.end();

  if (code != HTTP_CODE_OK) {
    Serial.printf("[RCI] login failed: http=%d\n", code);
    return false;
  }
  return true;
}

static bool fetchRciValue(IPAddress routerIp,
                          const String &cookie,
                          const char *path,
                          String &body) {
  String url = "http://" + routerIp.toString() + path;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(RCI_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Cookie", cookie);
  int code = http.GET();
  body = http.getString();
  http.end();

  if (code != HTTP_CODE_OK) {
    Serial.printf("[RCI] %s request failed: http=%d\n", path, code);
    return false;
  }
  body.trim();
  return body.length() > 0;
}

static bool parseJsonStringValue(const String &body, char *out, size_t outLen) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  const char *value = doc.as<const char *>();
  if (!value || value[0] == '\0') {
    return false;
  }
  strncpy(out, value, outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

static bool parseUptimeValue(const String &body, uint32_t &uptimeSec) {
  char *end = nullptr;
  unsigned long uptime = strtoul(body.c_str(), &end, 10);
  if (end == body.c_str()) {
    return false;
  }
  uptimeSec = static_cast<uint32_t>(uptime);
  return true;
}

bool keeneticRciFetchWanData(IPAddress routerIp,
                             const char *login,
                             const char *password,
                             KeeneticRciData &out) {
  out = KeeneticRciData{};
  if (!hasText(login) || !hasText(password)) {
    return false;
  }

  String cookie;
  if (!authenticate(routerIp, login, password, cookie)) {
    return false;
  }

  String body;
  if (!fetchRciValue(routerIp, cookie,
                     "/rci/show/interface/UsbLte0/connection-state",
                     body)) {
    return false;
  }
  if (!parseJsonStringValue(body, out.wanConnectionState,
                            sizeof(out.wanConnectionState))) {
    Serial.println("[RCI] connection-state response is invalid");
    return false;
  }
  out.wanConnectionStateValid = true;

  if (fetchRciValue(routerIp, cookie,
                    "/rci/show/interface/UsbLte0/uptime",
                    body)) {
    uint32_t uptimeSec = 0;
    if (parseUptimeValue(body, uptimeSec)) {
      out.wanUptimeSec = uptimeSec;
      out.wanUptimeValid = true;
    } else {
      Serial.println("[RCI] uptime response is not a number");
    }
  }

  return true;
}
