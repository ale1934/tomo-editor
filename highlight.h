#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

// Simple, generic, language-agnostic syntax highlighting.
// No AST, no grammar: a single-pass lexer that classifies each line into
// spans (tokens). The only cross-line state is "am I inside a block comment",
// which is a single bool.

#include <cctype>
#include <raylib.h>
#include <string>
#include <unordered_set>
#include <vector>

enum TokenType {
  TOK_DEFAULT,
  TOK_KEYWORD,
  TOK_TYPE,
  TOK_STRING,
  TOK_NUMBER,
  TOK_COMMENT,
  TOK_PREPROC,
  TOK_FUNCTION,
  TOK_OPERATOR
};

struct Token {
  int start; // byte index into the line
  int length;
  TokenType type;
};

struct Syntax {
  std::vector<std::string> lineComment; // "//", "#", "--"
  std::string blockStart;               // "/*"   (empty = language has none)
  std::string blockEnd;                 // "*/"
  std::string quotes;                   // every char that opens a string
  bool preproc;                         // treat leading '#' as a directive
  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> types;
};

// ---------------------------------------------------------------- palette --

inline Color TokenColor(TokenType t) {
  switch (t) {
  case TOK_KEYWORD:
    return Color{198, 120, 221, 255}; // purple
  case TOK_TYPE:
    return Color{229, 192, 123, 255}; // yellow
  case TOK_STRING:
    return Color{152, 195, 121, 255}; // green
  case TOK_NUMBER:
    return Color{209, 154, 102, 255}; // orange
  case TOK_COMMENT:
    return Color{92, 99, 112, 255}; // dim gray
  case TOK_PREPROC:
    return Color{86, 182, 194, 255}; // cyan
  case TOK_FUNCTION:
    return Color{97, 175, 239, 255}; // blue
  case TOK_OPERATOR:
    return Color{171, 178, 191, 255}; // light gray
  default:
    return RAYWHITE;
  }
}

// ------------------------------------------------------------ definitions --

inline Syntax SyntaxForFile(const std::string &filename) {
  // Keywords are deliberately a union across common languages. A word that
  // isn't a keyword in this file just never appears, so false colouring is
  // rare and the whole thing stays one table.
  static const std::unordered_set<std::string> kKeywords = {
      "if",         "else",       "elif",     "for",       "while",
      "do",         "switch",     "case",     "default",   "break",
      "continue",   "return",     "goto",     "try",       "catch",
      "except",     "finally",    "throw",    "raise",     "new",
      "delete",     "class",      "struct",   "enum",      "union",
      "namespace",  "using",      "template", "typename",  "public",
      "private",    "protected",  "virtual",  "override",  "operator",
      "this",       "self",       "import",   "from",      "export",
      "package",    "def",        "lambda",   "fn",        "func",
      "function",   "static",     "extern",   "inline",    "const",
      "constexpr",  "mutable",    "volatile", "typedef",   "sizeof",
      "true",       "false",      "null",     "nullptr",   "None",
      "True",       "False",      "nil",      "and",       "or",
      "not",        "in",         "is",       "with",      "as",
      "pass",       "yield",      "async",    "await",     "match",
      "where",      "impl",       "trait",    "mod",       "pub",
      "use",        "end",        "then",     "local",     "elseif",
      "repeat",     "until",      "require",  "interface", "extends",
      "implements", "instanceof", "typeof",   "throws",    "final",
      "abstract",   "super"};

  static const std::unordered_set<std::string> kTypes = {
      "int",      "long",    "short",    "char",      "bool",     "float",
      "double",   "void",    "unsigned", "signed",    "size_t",   "ssize_t",
      "auto",     "var",     "let",      "string",    "wstring",  "vector",
      "map",      "set",     "list",     "dict",      "tuple",    "array",
      "str",      "byte",    "rune",     "u8",        "u16",      "u32",
      "u64",      "i8",      "i16",      "i32",       "i64",      "f32",
      "f64",      "usize",   "isize",    "uint8_t",   "uint16_t", "uint32_t",
      "uint64_t", "int8_t",  "int16_t",  "int32_t",   "int64_t",  "Color",
      "Vector2",  "Vector3", "Font",     "Rectangle", "Texture2D"};

  std::string ext;
  size_t dot = filename.find_last_of('.');
  if (dot != std::string::npos)
    ext = filename.substr(dot);

  Syntax s;
  s.keywords = kKeywords;
  s.types = kTypes;
  s.quotes = "\"'`";
  s.preproc = false;

  if (ext == ".py" || ext == ".sh" || ext == ".bash" || ext == ".rb" ||
      ext == ".yml" || ext == ".yaml" || ext == ".toml" || ext == ".conf" ||
      ext == ".cmake" || ext == ".mk" || filename == "Makefile") {
    s.lineComment = {"#"};
  } else if (ext == ".lua") {
    s.lineComment = {"--"};
    s.blockStart = "--[[";
    s.blockEnd = "]]";
  } else if (ext == ".sql") {
    s.lineComment = {"--"};
    s.blockStart = "/*";
    s.blockEnd = "*/";
  } else {
    // C / C++ / Java / JS / Go / Rust / etc. Also the fallback for plain text,
    // where almost nothing matches and the file reads as normal.
    s.lineComment = {"//"};
    s.blockStart = "/*";
    s.blockEnd = "*/";
    s.preproc = true;
  }

  return s;
}

// ----------------------------------------------------------------- lexing --

inline bool IsWordChar(char c) {
  return std::isalnum((unsigned char)c) || c == '_';
}

inline bool MatchesAt(const std::string &s, int i, const std::string &p) {
  if (p.empty())
    return false;
  if (i + (int)p.size() > (int)s.size())
    return false;
  return s.compare(i, p.size(), p) == 0;
}

// Tokenizes one line. `inBlock` says whether the previous line ended inside a
// block comment; the return value says whether this one does.
inline bool TokenizeLine(const std::string &line, const Syntax &syn,
                         bool inBlock, std::vector<Token> &out) {
  out.clear();
  const int n = (int)line.size();
  int i = 0;

  while (i < n) {
    // --- continuation of a block comment ---
    if (inBlock) {
      int start = i;
      while (i < n && !MatchesAt(line, i, syn.blockEnd))
        i++;
      if (i < n) {
        i += (int)syn.blockEnd.size();
        inBlock = false;
      }
      out.push_back({start, i - start, TOK_COMMENT});
      continue;
    }

    char c = line[i];

    if (std::isspace((unsigned char)c)) {
      i++;
      continue;
    }

    // --- line comment: rest of the line ---
    bool matchedLineComment = false;
    for (const std::string &lc : syn.lineComment) {
      if (MatchesAt(line, i, lc)) {
        out.push_back({i, n - i, TOK_COMMENT});
        i = n;
        matchedLineComment = true;
        break;
      }
    }
    if (matchedLineComment)
      continue;

    // --- block comment opener ---
    if (MatchesAt(line, i, syn.blockStart)) {
      int start = i;
      i += (int)syn.blockStart.size();
      while (i < n && !MatchesAt(line, i, syn.blockEnd))
        i++;
      if (i < n)
        i += (int)syn.blockEnd.size();
      else
        inBlock = true;
      out.push_back({start, i - start, TOK_COMMENT});
      continue;
    }

    // --- preprocessor directive (#include, #define, ...) ---
    if (syn.preproc && c == '#' && out.empty()) {
      int start = i;
      i++;
      while (i < n && IsWordChar(line[i]))
        i++;
      out.push_back({start, i - start, TOK_PREPROC});

      // colour <stdio.h> as a string so includes look right
      int j = i;
      while (j < n && std::isspace((unsigned char)line[j]))
        j++;
      if (j < n && line[j] == '<') {
        int qs = j;
        while (j < n && line[j] != '>')
          j++;
        if (j < n)
          j++;
        out.push_back({qs, j - qs, TOK_STRING});
        i = j;
      }
      continue;
    }

    // --- string / char literal ---
    if (syn.quotes.find(c) != std::string::npos) {
      int start = i;
      i++;
      while (i < n) {
        if (line[i] == '\\' && i + 1 < n) {
          i += 2;
          continue;
        }
        if (line[i] == c) {
          i++;
          break;
        }
        i++;
      }
      out.push_back({start, i - start, TOK_STRING});
      continue;
    }

    // --- number literal (ints, floats, hex, exponents, suffixes) ---
    if (std::isdigit((unsigned char)c) ||
        (c == '.' && i + 1 < n && std::isdigit((unsigned char)line[i + 1]))) {
      int start = i;
      while (i < n && (std::isalnum((unsigned char)line[i]) || line[i] == '.' ||
                       line[i] == '_' ||
                       ((line[i] == '+' || line[i] == '-') &&
                        (line[i - 1] == 'e' || line[i - 1] == 'E')))) {
        i++;
      }
      out.push_back({start, i - start, TOK_NUMBER});
      continue;
    }

    // --- identifier: keyword, type, call, or plain ---
    if (std::isalpha((unsigned char)c) || c == '_') {
      int start = i;
      while (i < n && IsWordChar(line[i]))
        i++;
      std::string word = line.substr(start, i - start);

      TokenType type = TOK_DEFAULT;
      if (syn.keywords.count(word)) {
        type = TOK_KEYWORD;
      } else if (syn.types.count(word)) {
        type = TOK_TYPE;
      } else {
        int j = i;
        while (j < n && std::isspace((unsigned char)line[j]))
          j++;
        if (j < n && line[j] == '(')
          type = TOK_FUNCTION;
      }

      out.push_back({start, i - start, type});
      continue;
    }

    // --- operators and punctuation ---
    if (std::string("+-*/%=<>!&|^~?:;,.()[]{}").find(c) != std::string::npos) {
      out.push_back({i, 1, TOK_OPERATOR});
      i++;
      continue;
    }

    out.push_back({i, 1, TOK_DEFAULT});
    i++;
  }

  return inBlock;
}

// Block-comment state at the top of the viewport. Only scans, never draws.
inline bool BlockStateAtLine(const std::vector<std::string> &document,
                             const Syntax &syn, int line) {
  bool inBlock = false;
  std::vector<Token> scratch;
  for (int i = 0; i < line && i < (int)document.size(); i++) {
    inBlock = TokenizeLine(document[i], syn, inBlock, scratch);
  }
  return inBlock;
}

// ---------------------------------------------------------------- drawing --

// X offset of character `index` within `text`. MeasureTextEx only counts the
// gaps *between* glyphs, so a prefix of length k is one `spacing` short of
// where character k actually starts. Use this everywhere (cursor, search
// highlights, tokens) so nothing drifts apart.
inline float TextOffsetX(Font font, const std::string &text, int index,
                         float fontSize, float spacing) {
  if (index <= 0)
    return 0.0f;
  return MeasureTextEx(font, text.substr(0, index).c_str(), fontSize, spacing)
             .x +
         spacing;
}

inline void DrawHighlightedLine(Font font, const std::string &line, Vector2 pos,
                                float fontSize, float spacing,
                                const std::vector<Token> &tokens) {
  for (const Token &t : tokens) {
    Vector2 p = {pos.x + TextOffsetX(font, line, t.start, fontSize, spacing),
                 pos.y};
    DrawTextEx(font, line.substr(t.start, t.length).c_str(), p, fontSize,
               spacing, TokenColor(t.type));
  }
}

#endif // HIGHLIGHT_H