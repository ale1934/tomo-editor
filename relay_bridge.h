#ifndef RELAY_BRIDGE_H
#define RELAY_BRIDGE_H

// Tiny bridge between the Tomo editor and the Tomo relay (relay/server.py).
//
// Unlike ai/ai.py (which talks to a cloud chat API directly), this path
// talks to *your own Tomo agent* through a small relay server you run:
//   Ctrl+T sends the current buffer + an instruction as a relay "request"
//   F4    polls the relay for Tomo's "response" and applies any edit
//
// The round trip is asynchronous: Tomo checks the relay on a schedule
// (roughly once a minute), so expect a short wait between Ctrl+T and F4.
//
// This header depends only on the C++ standard library plus ai_bridge.h
// (for the JSON helpers). Configuration comes from the environment:
//   TOMO_RELAY_URL     e.g. https://tomo-relay.example.com
//   TOMO_RELAY_SECRET  the shared secret your relay was started with

#include "ai_bridge.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct RelaySendResult {
  bool ok = false;
  std::string id;     // relay message id of our request
  std::string error;
};

struct RelayPollResult {
  bool ok = false;
  std::string error;
  int count = 0;           // outstanding replies from Tomo
  bool hasReply = false;   // true when the fields below are filled in
  std::string inReplyTo;   // request id this answers
  std::string message;     // Tomo's summary
  std::string newCode;     // complete replacement file content
  bool hasEdit = false;    // true when newCode should replace the buffer
};

inline bool RelayConfigured() {
  const char *url = std::getenv("TOMO_RELAY_URL");
  const char *secret = std::getenv("TOMO_RELAY_SECRET");
  return url && *url && secret && *secret;
}

// Extract a top-level integer field like `"count": 12`.
inline bool RelayExtractInt(const std::string &json, const std::string &key,
                            int &value) {
  std::string quoted = "\"" + key + "\"";
  size_t pos = json.find(quoted);
  if (pos == std::string::npos)
    return false;
  pos = json.find(':', pos + quoted.size());
  if (pos == std::string::npos)
    return false;
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    pos++;
  if (pos >= json.size() || json[pos] < '0' || json[pos] > '9')
    return false;
  value = 0;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    value = value * 10 + (json[pos] - '0');
    pos++;
  }
  return true;
}

// Send the current buffer + instruction to Tomo via the relay.
// Returns the relay message id; poll RelayCheck() later for the reply.
inline RelaySendResult RelayAsk(const std::string &instruction,
                                const std::string &filename, int curLine,
                                int curCol,
                                const std::vector<std::string> &document) {
  RelaySendResult r;

  std::string code;
  for (size_t i = 0; i < document.size(); i++) {
    if (i > 0)
      code += '\n';
    code += document[i];
  }

  std::ostringstream req;
  req << "{\"kind\":\"request\",\"body\":{"
      << "\"instruction\":\"" << AiJsonEscape(instruction) << "\","
      << "\"file\":\"" << AiJsonEscape(filename) << "\","
      << "\"language\":\"" << AiLanguageForFile(filename) << "\","
      << "\"cursor\":{\"line\":" << curLine << ",\"col\":" << curCol << "},"
      << "\"code\":\"" << AiJsonEscape(code) << "\"}}";

  const char *reqPath = "/tmp/tomo_relay_request.json";
  const char *errPath = "/tmp/tomo_relay_stderr.log";

  {
    std::ofstream f(reqPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      r.error = "could not write relay request temp file";
      return r;
    }
    f << req.str();
  }

  std::string cmd =
      std::string("python3 relay/relay_client.py send ") + reqPath +
      " 2>" + errPath + " >/tmp/tomo_relay_send.out";
  int rc = std::system(cmd.c_str());

  std::string out = AiReadFile("/tmp/tomo_relay_send.out");
  // relay_client.py prints the new message id on success.
  while (!out.empty() &&
         (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
    out.pop_back();
  if (rc != 0 || out.empty()) {
    std::string stderrLog = AiReadFile(errPath);
    size_t nl = stderrLog.find('\n');
    std::string detail = stderrLog.substr(0, nl == std::string::npos ? 120 : nl);
    r.error = detail.empty()
                  ? "relay send failed (see relay/README.md)"
                  : "relay send failed: " + detail;
    if (r.error.size() > 160)
      r.error.resize(160);
    return r;
  }

  r.ok = true;
  r.id = out;
  return r;
}

// Check the relay for Tomo's replies to our outstanding requests.
// Consumes at most one reply per call; call again if count > 1.
inline RelayPollResult RelayCheck() {
  RelayPollResult r;

  const char *outPath = "/tmp/tomo_relay_poll.json";
  const char *errPath = "/tmp/tomo_relay_stderr.log";
  std::remove(outPath);

  std::string cmd = std::string("python3 relay/relay_client.py poll ") +
                    outPath + " 2>" + errPath + " >/dev/null";
  int rc = std::system(cmd.c_str());

  std::string out = AiReadFile(outPath);
  if (rc != 0 || out.empty()) {
    std::string stderrLog = AiReadFile(errPath);
    size_t nl = stderrLog.find('\n');
    std::string detail = stderrLog.substr(0, nl == std::string::npos ? 120 : nl);
    r.error = detail.empty()
                  ? "relay poll failed (see relay/README.md)"
                  : "relay poll failed: " + detail;
    if (r.error.size() > 160)
      r.error.resize(160);
    return r;
  }

  r.ok = true;
  RelayExtractInt(out, "count", r.count);

  std::string val;
  bool wasNull = false;
  if (r.count > 0 &&
      AiExtractField(out, "in_reply_to", val, wasNull) && !wasNull) {
    r.hasReply = true;
    r.inReplyTo = val;
    if (AiExtractField(out, "message", val, wasNull) && !wasNull)
      r.message = val;
    if (AiExtractField(out, "new_code", val, wasNull) && !wasNull) {
      r.newCode = val;
      r.hasEdit = true;
    }
  }
  return r;
}

#endif // RELAY_BRIDGE_H
