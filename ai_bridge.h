#ifndef AI_BRIDGE_H
#define AI_BRIDGE_H

// Tiny bridge between the Tomo editor and ai/ai.py.
//
// The editor calls AiAsk() with the user's instruction plus the current
// buffer. That shells out to `python3 ai/ai.py`, which talks to an
// OpenAI-compatible chat API and writes back a JSON response. This header
// intentionally depends on nothing but the C++ standard library (no raylib,
// no third-party JSON lib) so the protocol stays easy to audit.
//
// Request  (written to /tmp/tomo_ai_request.json):
//   {"instruction": "...", "file": "...", "language": "...",
//    "cursor": {"line": 0, "col": 0}, "code": "<full file text>"}
// Response (read from /tmp/tomo_ai_response.json):
//   {"ok": true, "message": "...", "new_code": "<full file>" | null}
//   {"ok": false, "error": "..."}

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct AiResult {
  bool ok = false;         // false => check `error`
  std::string message;     // short summary from the model
  std::string newCode;     // complete replacement file content
  bool hasEdit = false;    // true when newCode should replace the buffer
  std::string error;       // human-readable failure reason
};

// ------------------------------------------------------------------ JSON --

// Escape a string for embedding in JSON.
inline std::string AiJsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    default:
      if (c < 0x20) {
        char buf[7];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += (char)c;
      }
    }
  }
  return out;
}

// Append the UTF-8 encoding of `cp` to `out`.
inline void AiAppendUtf8(std::string &out, unsigned cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Extract a top-level field from a flat JSON object.
// Returns true when the key exists; `wasNull` is set for `null` values,
// otherwise `value` holds the unescaped string.
inline bool AiExtractField(const std::string &json, const std::string &key,
                           std::string &value, bool &wasNull) {
  value.clear();
  wasNull = false;
  std::string quoted = "\"" + key + "\"";
  size_t pos = json.find(quoted);
  if (pos == std::string::npos)
    return false;
  pos = json.find(':', pos + quoted.size());
  if (pos == std::string::npos)
    return false;
  pos++;
  while (pos < json.size() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
          json[pos] == '\r'))
    pos++;
  if (pos >= json.size())
    return false;
  if (json.compare(pos, 4, "null") == 0) {
    wasNull = true;
    return true;
  }
  if (json.compare(pos, 4, "true") == 0) {
    value = "true";
    return true;
  }
  if (json.compare(pos, 5, "false") == 0) {
    value = "false";
    return true;
  }
  if (json[pos] != '"')
    return false;
  pos++;
  while (pos < json.size()) {
    char c = json[pos];
    if (c == '"')
      return true;
    if (c == '\\') {
      if (pos + 1 >= json.size())
        return false;
      char e = json[pos + 1];
      switch (e) {
      case '"': value += '"'; break;
      case '\\': value += '\\'; break;
      case '/': value += '/'; break;
      case 'n': value += '\n'; break;
      case 'r': value += '\r'; break;
      case 't': value += '\t'; break;
      case 'b': value += '\b'; break;
      case 'f': value += '\f'; break;
      case 'u': {
        if (pos + 5 >= json.size())
          return false;
        unsigned cp = 0;
        for (int i = 0; i < 4; i++) {
          char h = json[pos + 2 + i];
          cp <<= 4;
          if (h >= '0' && h <= '9')
            cp |= (unsigned)(h - '0');
          else if (h >= 'a' && h <= 'f')
            cp |= (unsigned)(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F')
            cp |= (unsigned)(h - 'A' + 10);
          else
            return false;
        }
        // Handle UTF-16 surrogate pairs.
        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 11 < json.size() &&
            json[pos + 6] == '\\' && json[pos + 7] == 'u') {
          unsigned lo = 0;
          bool ok = true;
          for (int i = 0; i < 4; i++) {
            char h = json[pos + 8 + i];
            lo <<= 4;
            if (h >= '0' && h <= '9')
              lo |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f')
              lo |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
              lo |= (unsigned)(h - 'A' + 10);
            else
              ok = false;
          }
          if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            pos += 6;
          }
        }
        AiAppendUtf8(value, cp);
        pos += 6;
        continue;
      }
      default:
        return false;
      }
      pos += 2;
    } else {
      value += c;
      pos++;
    }
  }
  return false;
}

// --------------------------------------------------------------- request --

// Best-effort language name from a file extension, for the AI's context.
inline std::string AiLanguageForFile(const std::string &filename) {
  size_t dot = filename.rfind('.');
  std::string ext = (dot == std::string::npos) ? "" : filename.substr(dot);
  for (char &c : ext)
    c = (char)tolower((unsigned char)c);
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++")
    return "cpp";
  if (ext == ".h" || ext == ".hpp" || ext == ".hh")
    return "cpp-header";
  if (ext == ".c")
    return "c";
  if (ext == ".py")
    return "python";
  if (ext == ".js" || ext == ".mjs")
    return "javascript";
  if (ext == ".ts")
    return "typescript";
  if (ext == ".rs")
    return "rust";
  if (ext == ".go")
    return "go";
  if (ext == ".java")
    return "java";
  if (ext == ".rb")
    return "ruby";
  if (ext == ".sh")
    return "shell";
  if (ext == ".md")
    return "markdown";
  if (ext == ".json")
    return "json";
  if (ext == ".html" || ext == ".htm")
    return "html";
  if (ext == ".css")
    return "css";
  return "text";
}

inline std::string AiReadFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Send `instruction` + the current buffer to the AI and return its answer.
// Blocks while the Python helper runs (the editor shows "Thinking...").
inline AiResult AiAsk(const std::string &instruction,
                      const std::string &filename, int curLine, int curCol,
                      const std::vector<std::string> &document) {
  AiResult r;

  std::string code;
  for (size_t i = 0; i < document.size(); i++) {
    if (i > 0)
      code += '\n';
    code += document[i];
  }

  std::ostringstream req;
  req << "{\"instruction\":\"" << AiJsonEscape(instruction) << "\","
      << "\"file\":\"" << AiJsonEscape(filename) << "\","
      << "\"language\":\"" << AiLanguageForFile(filename) << "\","
      << "\"cursor\":{\"line\":" << curLine << ",\"col\":" << curCol << "},"
      << "\"code\":\"" << AiJsonEscape(code) << "\"}";

  const char *reqPath = "/tmp/tomo_ai_request.json";
  const char *respPath = "/tmp/tomo_ai_response.json";
  const char *errPath = "/tmp/tomo_ai_stderr.log";

  {
    std::ofstream f(reqPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
      r.error = "could not write AI request temp file";
      return r;
    }
    f << req.str();
  }
  std::remove(respPath);

  std::string cmd = std::string("python3 ai/ai.py ") + reqPath + " " +
                    respPath + " 2>" + errPath;
  int rc = std::system(cmd.c_str());

  std::string resp = AiReadFile(respPath);
  if (resp.empty()) {
    std::string stderrLog = AiReadFile(errPath);
    if (rc != 0 && !stderrLog.empty()) {
      // Show the helper's own error line, e.g. missing API key.
      size_t nl = stderrLog.find('\n');
      r.error = "AI helper failed: " +
                stderrLog.substr(0, nl == std::string::npos ? 120 : nl);
      if (r.error.size() > 160)
        r.error.resize(160);
    } else if (rc != 0) {
      r.error = "AI helper failed (is python3 installed? see ai/README.md)";
    } else {
      r.error = "AI helper produced no response";
    }
    return r;
  }

  std::string val;
  bool wasNull = false;
  if (!AiExtractField(resp, "ok", val, wasNull) || val != "true") {
    if (AiExtractField(resp, "error", val, wasNull) && !wasNull && !val.empty())
      r.error = val;
    else
      r.error = "AI helper returned an unreadable response";
    return r;
  }

  r.ok = true;
  if (AiExtractField(resp, "message", val, wasNull) && !wasNull)
    r.message = val;
  if (AiExtractField(resp, "new_code", val, wasNull)) {
    if (!wasNull) {
      r.newCode = val;
      r.hasEdit = true;
    }
  }
  return r;
}

#endif // AI_BRIDGE_H
