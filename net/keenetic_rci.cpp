#include "keenetic_rci.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>

static constexpr uint32_t RCI_HTTP_TIMEOUT_MS = 2500;

static String    g_sessionCookie;
static IPAddress g_sessionRouterIp;
static String    g_sessionLogin;
static uint32_t  g_sessionPasswordFingerprint = 0;
static bool      g_sessionValid = false;

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

static uint32_t fingerprintText(const char *value) {
  uint32_t hash = 2166136261UL;
  while (value && *value) {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

void keeneticRciResetSession() {
  g_sessionCookie = "";
  g_sessionRouterIp = IPAddress();
  g_sessionLogin = "";
  g_sessionPasswordFingerprint = 0;
  g_sessionValid = false;
}

static bool sessionMatches(IPAddress routerIp,
                           const char *login,
                           const char *password) {
  return g_sessionValid &&
         g_sessionRouterIp == routerIp &&
         g_sessionLogin == login &&
         g_sessionPasswordFingerprint == fingerprintText(password) &&
         g_sessionCookie.length() > 0;
}

static void storeSession(IPAddress routerIp,
                         const char *login,
                         const char *password,
                         const String &cookie) {
  g_sessionRouterIp = routerIp;
  g_sessionLogin = login;
  g_sessionPasswordFingerprint = fingerprintText(password);
  g_sessionCookie = cookie;
  g_sessionValid = true;
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

static bool ensureAuthenticated(IPAddress routerIp,
                                const char *login,
                                const char *password) {
  if (sessionMatches(routerIp, login, password)) {
    return true;
  }

  keeneticRciResetSession();
  String cookie;
  if (!authenticate(routerIp, login, password, cookie)) {
    return false;
  }
  storeSession(routerIp, login, password, cookie);
  return true;
}

static int fetchRciValue(IPAddress routerIp,
                         const String &cookie,
                         const char *path,
                         String &body) {
  body = "";
  String url = "http://" + routerIp.toString() + path;
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(RCI_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    return -1;
  }
  http.addHeader("Cookie", cookie);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    body = http.getString();
  }
  http.end();

  if (code != HTTP_CODE_OK) {
    Serial.printf("[RCI] %s request failed: http=%d\n", path, code);
    return code;
  }
  body.trim();
  return code;
}

static bool fetchRciValueWithSession(IPAddress routerIp,
                                     const char *login,
                                     const char *password,
                                     const char *path,
                                     String &body) {
  if (!ensureAuthenticated(routerIp, login, password)) {
    return false;
  }

  int code = fetchRciValue(routerIp, g_sessionCookie, path, body);
  if (code == HTTP_CODE_OK) {
    return body.length() > 0;
  }
  if (code != HTTP_CODE_UNAUTHORIZED && code != HTTP_CODE_FORBIDDEN) {
    return false;
  }

  keeneticRciResetSession();
  if (!ensureAuthenticated(routerIp, login, password)) {
    return false;
  }
  code = fetchRciValue(routerIp, g_sessionCookie, path, body);
  return code == HTTP_CODE_OK && body.length() > 0;
}

static bool parseUptimeValue(JsonVariantConst value, uint32_t &uptimeSec) {
  if (value.is<uint32_t>()) {
    uptimeSec = value.as<uint32_t>();
    return true;
  }

  const char *text = value.as<const char *>();
  if (!text || text[0] == '\0') {
    return false;
  }

  char *end = nullptr;
  unsigned long uptime = strtoul(text, &end, 10);
  if (end == text || *end != '\0') return false;
  uptimeSec = static_cast<uint32_t>(uptime);
  return true;
}

static bool parseWanData(const String &body, KeeneticRciData &out) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error || !doc.is<JsonObject>()) {
    Serial.printf("[RCI] aggregate response is invalid: %s\n",
                  error ? error.c_str() : "not an object");
    return false;
  }

  const char *state = doc["connection-state"] | "";
  if (state[0] == '\0') {
    Serial.println("[RCI] aggregate response has no connection-state");
    return false;
  }
  strncpy(out.wanConnectionState, state,
          sizeof(out.wanConnectionState) - 1);
  out.wanConnectionState[sizeof(out.wanConnectionState) - 1] = '\0';
  out.wanConnectionStateValid = true;

  uint32_t uptimeSec = 0;
  if (parseUptimeValue(doc["uptime"], uptimeSec)) {
    out.wanUptimeSec = uptimeSec;
    out.wanUptimeValid = true;
  } else {
    Serial.println("[RCI] aggregate response has invalid uptime");
  }
  return true;
}

bool keeneticRciFetchWanData(IPAddress routerIp,
                             const char *login,
                             const char *password,
                             KeeneticRciData &out) {
  out = KeeneticRciData{};
  if (!hasText(login) || !hasText(password)) {
    keeneticRciResetSession();
    return false;
  }

  String body;
  static constexpr const char *WAN_INTERFACE_PATH =
      "/rci/show/interface/UsbLte0";
  if (!fetchRciValueWithSession(routerIp, login, password,
                                WAN_INTERFACE_PATH, body)) {
    return false;
  }
  return parseWanData(body, out);
}
