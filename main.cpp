#define PADDING 32
#define FONT_SIZE 24
#define FONT_SPACING 2
#define STATUS_BAR_HEIGHT 50
#define TAB_SPACING 4

#include "highlight.h"
#include "ai_bridge.h"
#include <filesystem>
#include <fstream>
#include <raylib.h>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

struct Vector2i {
  int x;
  int y;
};

enum Mode { EDIT, SEARCH };

string statusMessage = "";
Color statusColor = WHITE;
double statusMessageTime = -1.0;

const double STATUS_MESSAGE_DURATION = 1.0;

string FileInput(string inputText, Font mainFont) {
  string filename = "";
  while (GetKeyPressed() != KEY_ENTER) {
    char c = GetCharPressed();
    if (c >= 32 && c <= 127)
      filename += c;

    if (IsKeyPressed(KEY_BACKSPACE) && filename.length() > 0) {
      filename.pop_back();
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
      return "";
    }

    BeginDrawing();

    // Draw bottom status bar
    DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                  STATUS_BAR_HEIGHT, BLACK);
    DrawRectangleLines(0, GetScreenHeight() - STATUS_BAR_HEIGHT,
                       GetScreenWidth(), STATUS_BAR_HEIGHT, WHITE);

    string infoText = inputText + ": " + filename;
    DrawTextEx(
        mainFont, infoText.c_str(),
        {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
        FONT_SIZE, FONT_SPACING, BLUE);

    EndDrawing();
  }

  return filename;
}

void DisplayError(string err) {
  statusMessage = err;
  statusColor = RED;
  statusMessageTime = GetTime();
}

void DisplayInfo(string msg) {
  statusMessage = msg;
  statusColor = BLUE;
  statusMessageTime = GetTime();
}

// Gets occurances of searched string in document
// TODO
vector<Vector2i> searchForString(vector<string> &document, string txt,
                                 Font mainFont) {
  vector<Vector2i> locations;

  for (int y = 0; y < document.size(); y++) {
    int pos = document[y].find(txt, 0);
    while (pos != string::npos) {
      locations.push_back({pos, y});
      pos = document[y].find(txt, pos + 1);
    }
  }

  return locations;
}

void LoadFile(vector<string> &document, const string &filename, Font mainFont) {
  document.clear();

  ifstream file(filename);
  if (!file.is_open()) {
    document.push_back("");
    return;
  }

  string line;
  while (getline(file, line)) {
    document.push_back(line);
  }

  if (document.empty())
    document.push_back("");

  file.close();
}

void SaveFile(const vector<string> &document, const string &filename) {
  if (filename == "")
    return;

  ofstream file(filename);

  if (!file.is_open())
    return;

  for (const string &line : document) {
    file << line << '\n';
  }

  file.close();
}

int main(int argc, char *argv[]) {
  SetConfigFlags(FLAG_FULLSCREEN_MODE);
  InitWindow(GetScreenWidth(), GetScreenHeight(), "Tap");
  SetExitKey(KEY_NULL);

  // Load custom font
  Font mainFont =
      LoadFontEx("resources/CascadiaMono/CaskaydiaMonoNerdFontMono-Regular.ttf",
                 FONT_SIZE, 0, 250);

  vector<string> document;
  vector<Vector2i> occurances;
  document.push_back("");

  float textHue = 0.0f;

  int curLine = 0;
  int curLetter = 0;
  char curChar = '\0';
  int scrollOffset = 0;
  double keyHoldStart = 0.0;
  const double SCROLL_DELAY = 0.2;
  const double SCROLL_INTERVAL = 0.05;
  int lastKey = -1;
  int searchIndex = 0;
  int lastCurLine = curLine;

  Mode currentMode = EDIT;

  int maxVisibleLines =
      (GetScreenHeight() - (PADDING / 2) - STATUS_BAR_HEIGHT) / FONT_SIZE;

  // keeps the view inside the document no matter who moved it
  auto ClampScroll = [&]() {
    int maxScroll = (int)document.size() - maxVisibleLines;
    if (maxScroll < 0)
      maxScroll = 0;
    if (scrollOffset > maxScroll)
      scrollOffset = maxScroll;
    if (scrollOffset < 0)
      scrollOffset = 0;
  };

  double savedTime = -1.0;
  const double SAVED_FLASH_DURATION = 1.0;

  string currentFile = "untitled.txt";
  string currentSearch = "";

  if (argc >= 2) {
    currentFile = argv[1];
  }

  LoadFile(document, currentFile, mainFont);
  Syntax syntax = SyntaxForFile(currentFile);

  while (!WindowShouldClose()) {

    // Draw Cursor
    bool cursorVisible = ((int)(GetTime() * 2) % 2) == 0;

    // Get cursor position by measuring text in current line
    int cursorX =
        PADDING + MeasureTextEx(mainFont,
                                document[curLine].substr(0, curLetter).c_str(),
                                FONT_SIZE, FONT_SPACING)
                      .x;
    int cursorY = PADDING / 2 + FONT_SIZE * (curLine - scrollOffset);

    curChar = GetCharPressed();

    if (curChar >= 32 && curChar <= 127 && currentMode == EDIT) {
      document[curLine].insert(curLetter, 1, curChar);
      curLetter++;
    }

    if (IsKeyPressed(KEY_ENTER)) {
      document.insert(document.begin() + curLine + 1, "");
      curLine++;
      curLetter = document[curLine].length();
    }

    int keyPressed = -1;
    if (IsKeyDown(KEY_UP))
      keyPressed = KEY_UP;
    if (IsKeyDown(KEY_DOWN))
      keyPressed = KEY_DOWN;
    if (IsKeyDown(KEY_LEFT))
      keyPressed = KEY_LEFT;
    if (IsKeyDown(KEY_RIGHT))
      keyPressed = KEY_RIGHT;
    if (IsKeyDown(KEY_BACKSPACE))
      keyPressed = KEY_BACKSPACE;

    double currentTime = GetTime();

    const int SCROLL_LINES = 3;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
      scrollOffset -= (int)(wheel * SCROLL_LINES);
      ClampScroll();
    }

    if (IsKeyPressed(KEY_PAGE_DOWN)) {
      scrollOffset += maxVisibleLines;
      curLine += maxVisibleLines;
      if (curLine > (int)document.size() - 1)
        curLine = document.size() - 1;
      curLetter = 0;
      ClampScroll();
    }

    if (IsKeyPressed(KEY_PAGE_UP)) {
      scrollOffset -= maxVisibleLines;
      curLine -= maxVisibleLines;
      if (curLine < 0)
        curLine = 0;
      curLetter = 0;
      ClampScroll();
    }

    if (keyPressed != -1) {
      if (keyPressed != lastKey) {
        // First press, move immediately
        if (keyPressed == KEY_UP && curLine > 0) {
          curLine--;
          curLetter = document[curLine].length();
          if (curLine < scrollOffset)
            scrollOffset = curLine;
        } else if (keyPressed == KEY_DOWN && curLine < document.size() - 1) {
          curLine++;
          curLetter = document[curLine].length();
          if (curLine >= scrollOffset + maxVisibleLines)
            scrollOffset = curLine - maxVisibleLines + 1;
        } else if (keyPressed == KEY_LEFT && curLetter > 0) {
          curLetter--;
        } else if (keyPressed == KEY_RIGHT &&
                   curLetter < document[curLine].length()) {
          curLetter++;
        } else if (keyPressed == KEY_BACKSPACE) {
          if (document[curLine].length() > 0) {
            document[curLine].erase(curLetter - 1, 1);
            curLetter--;
          } else {
            if (curLine > 0) {
              document.erase(document.begin() + curLine);
              curLine--;
              curLetter = document[curLine].length();
            }
          }
        }

        keyHoldStart = currentTime;
        lastKey = keyPressed;
      } else {
        // Key is held down
        if (currentTime - keyHoldStart >= SCROLL_DELAY) {
          int repeats = (int)((currentTime - keyHoldStart - SCROLL_DELAY) /
                              SCROLL_INTERVAL);
          if (repeats > 0) {
            for (int i = 0; i < repeats; i++) {
              if (keyPressed == KEY_UP && curLine > 0) {
                curLine--;
                curLetter = document[curLine].length();
                if (curLine < scrollOffset)
                  scrollOffset = curLine;
              } else if (keyPressed == KEY_DOWN &&
                         curLine < document.size() - 1) {
                curLine++;
                curLetter = document[curLine].length();
                if (curLine >= scrollOffset + maxVisibleLines)
                  scrollOffset = curLine - maxVisibleLines + 1;
              } else if (keyPressed == KEY_LEFT && curLetter > 0) {
                curLetter--;
              } else if (keyPressed == KEY_RIGHT &&
                         curLetter < document[curLine].length()) {
                curLetter++;
              } else if (keyPressed == KEY_BACKSPACE) {
                if (document[curLine].length() > 0) {
                  document[curLine].erase(curLetter - 1, 1);
                  curLetter--;
                } else {
                  if (curLine > 0) {
                    document.erase(document.begin() + curLine);
                    curLine--;
                    curLetter = document[curLine].length();
                  }
                }
              }
            }
            keyHoldStart += repeats * SCROLL_INTERVAL;
          }
        }
      }
    } else {
      lastKey = -1; // reset when no key held
    }

    if (IsKeyPressed(KEY_TAB)) {
      document[curLine].insert(curLetter, TAB_SPACING, ' ');
      curLetter += TAB_SPACING;
    }

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_S)) {

      SaveFile(document, currentFile);
      savedTime = GetTime();
    }

    // Search for string
    if ((IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_F))) {
      currentSearch = FileInput("Enter String", mainFont);
      occurances = searchForString(document, currentSearch, mainFont);
      if (!occurances.empty()) {
        currentMode = SEARCH;
        searchIndex = 0;
        curLine = occurances[searchIndex].y;
        curLetter = occurances[searchIndex].x;
        if (curLine >= scrollOffset + maxVisibleLines)
          scrollOffset = curLine - maxVisibleLines + 1;
      } else {
        DisplayInfo("No Matches Found!");
      }
    }

    // Go to next occurrence if in search mode
    if (currentMode == SEARCH && IsKeyPressed(KEY_N)) {
      if (searchIndex + 1 < occurances.size()) {
        searchIndex++;
      } else {
        // Wrap back to first occurrence
        searchIndex = 0;
      }

      curLine = occurances[searchIndex].y;
      curLetter = occurances[searchIndex].x;

      // Make sure the occurrence is visible
      if (curLine < scrollOffset) {
        scrollOffset = curLine;
      } else if (curLine >= scrollOffset + maxVisibleLines) {
        scrollOffset = curLine - maxVisibleLines + 1;
      }
    }

    if (IsKeyPressed(KEY_F2)) {
      filesystem::path old_file = currentFile.c_str();
      currentFile = FileInput("Rename File To", mainFont);

      if (!filesystem::remove(old_file)) {
        string errorMsg = "Cannot Remove: " + old_file.generic_string() + "!";
        DisplayError(errorMsg);
        currentFile = old_file;
      }

      syntax = SyntaxForFile(currentFile);
    }

    if (IsKeyPressed(KEY_F3)) {
      string filename = FileInput("Open File", mainFont);
      LoadFile(document, filename, mainFont);
      syntax = SyntaxForFile(currentFile);
      curLetter = 0;
      curLine = 0;
      scrollOffset = 0;
      currentFile = filename;
    }

    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_J)) {
      string input = FileInput("Line Number", mainFont);
      if (!input.empty()) {
        int lineNum = TextToInteger(input.c_str()) - 1;

        if (lineNum < 0)
          lineNum = 0;
        if (lineNum > (int)document.size() - 1)
          lineNum = document.size() - 1;

        curLine = lineNum;
        curLetter = 0;

        // centre the target line, then clamp to the document
        scrollOffset = curLine - maxVisibleLines / 2;
        if (scrollOffset > (int)document.size() - maxVisibleLines)
          scrollOffset = document.size() - maxVisibleLines;
        if (scrollOffset < 0)
          scrollOffset = 0;
      }
    }

    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_U)) {
      document[curLine].clear();
      curLetter = 0;
    }

    // Ask AI (Ctrl+K): prompt for an instruction, send the current buffer
    // to the AI helper, and apply the returned edit if there is one.
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
        IsKeyPressed(KEY_K)) {
      string instruction = FileInput("Ask AI", mainFont);
      if (!instruction.empty()) {
        // Show feedback before blocking on the API call.
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTextEx(mainFont, "Thinking...",
                   {PADDING / 2.0f, (float)GetScreenHeight() / 2.0f}, FONT_SIZE,
                   FONT_SPACING, BLUE);
        EndDrawing();

        AiResult ai =
            AiAsk(instruction, currentFile, curLine, curLetter, document);
        if (!ai.ok) {
          DisplayError(ai.error);
        } else if (ai.hasEdit) {
          document.clear();
          istringstream codeStream(ai.newCode);
          string line;
          while (getline(codeStream, line)) {
            if (!line.empty() && line.back() == '\r')
              line.pop_back();
            document.push_back(line);
          }
          if (document.empty())
            document.push_back("");
          curLine = 0;
          curLetter = 0;
          scrollOffset = 0;
          ClampScroll();
          syntax = SyntaxForFile(currentFile);
          DisplayInfo("AI edit applied (Ctrl+S to save)");
        } else {
          DisplayInfo(ai.message);
        }
      }
    }

    if (IsKeyPressed(KEY_ESCAPE) && currentMode == SEARCH) {
      currentMode = EDIT;
      currentSearch = "";
    }

    // cout << "Current Letter: " << curLetter << endl;
    // cout << "Current Line: " << curLine << endl;

    if (curLine != lastCurLine) {
      if (curLine < scrollOffset)
        scrollOffset = curLine;
      if (curLine >= scrollOffset + maxVisibleLines)
        scrollOffset = curLine - maxVisibleLines + 1;
      ClampScroll();
    }
    lastCurLine = curLine;

    BeginDrawing();
    ClearBackground(BLACK);

    if (cursorVisible && curLine >= scrollOffset &&
        curLine < scrollOffset + maxVisibleLines) {
      DrawRectangle(cursorX + 1 + PADDING, cursorY, 3, FONT_SIZE, PINK);
    }

    // Draw Line Numbers and Text
    int lineNumberWidth = to_string(document.size()).length();

    bool inBlock = BlockStateAtLine(document, syntax, scrollOffset);
    vector<Token> tokens;

    for (int i = 0; i < maxVisibleLines; i++) {
      int lineIndex = scrollOffset + i;
      if (lineIndex >= document.size())
        break;

      Vector2 textPos = {PADDING * 2, (float)PADDING / 2 + FONT_SIZE * i};
      inBlock = TokenizeLine(document[lineIndex], syntax, inBlock, tokens);
      DrawHighlightedLine(mainFont, document[lineIndex], textPos, FONT_SIZE,
                          FONT_SPACING, tokens);

      string lineNumber = to_string(lineIndex + 1);

      lineNumber =
          string(lineNumberWidth - lineNumber.length(), ' ') + lineNumber;

      DrawTextEx(mainFont, lineNumber.c_str(),
                 {PADDING / 8, (float)FONT_SIZE * i + PADDING / 2}, FONT_SIZE,
                 FONT_SPACING, LIGHTGRAY);
    }

    // Draw Highlighted Searches
    if (currentMode == SEARCH) {
      for (int i = 0; i < occurances.size(); i++) {
        if (occurances[i].y < scrollOffset ||
            occurances[i].y >= scrollOffset + maxVisibleLines)
          continue;
        float x =
            PADDING * 2 +
            MeasureTextEx(
                mainFont,
                document[occurances[i].y].substr(0, occurances[i].x).c_str(),
                FONT_SIZE, FONT_SPACING)
                .x;

        float y = PADDING / 2 + FONT_SIZE * (occurances[i].y - scrollOffset);
        Vector2 dimensions =
            MeasureTextEx(mainFont,
                          document[occurances[i].y]
                              .substr(occurances[i].x, currentSearch.size())
                              .c_str(),
                          FONT_SIZE, FONT_SPACING);

        if (i == searchIndex) {
          DrawRectangleRounded({x, y, dimensions.x + 5, dimensions.y}, 0.5f, 10,
                               GetColor(0xffffff80));
        } else {
          DrawRectangleRounded({x, y, dimensions.x + 5, dimensions.y}, 0.5f, 10,
                               GetColor(0xffffff20));
        }
      }
    }

    // Draw bottom status bar
    DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                  STATUS_BAR_HEIGHT, BLACK);
    DrawRectangleLines(0, GetScreenHeight() - STATUS_BAR_HEIGHT,
                       GetScreenWidth(), STATUS_BAR_HEIGHT, WHITE);

    string infoText = "";
    Color infoColor = Color{198, 120, 221, 255};

    string statusText = "";
    Color statusColor = BLACK;

    switch (currentMode) {
    case EDIT:
      statusText = "Edit Mode";
      statusColor = BLUE;
      break;
    case SEARCH:
      statusText = "Search Mode";
      statusColor = PINK;
      break;
    }

    if (statusMessageTime > 0 &&
        GetTime() - statusMessageTime < STATUS_MESSAGE_DURATION) {

      infoText = statusMessage;
      infoColor = statusColor;

    } else if (savedTime > 0 && GetTime() - savedTime < SAVED_FLASH_DURATION) {

      infoText = currentFile + " Saved!";

    } else {

      infoText = "Current File: " + currentFile;
    }

    DrawTextEx(mainFont, infoText.c_str(),
               {PADDING / 2.0f,
                (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2.0f - 15},
               FONT_SIZE, FONT_SPACING, infoColor);

    DrawTextEx(mainFont, statusText.c_str(),
               {GetScreenWidth() - PADDING / 2.0f -
                    MeasureTextEx(mainFont, statusText.c_str(), FONT_SIZE,
                                  FONT_SPACING)
                        .x,
                (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2.0f - 15},
               FONT_SIZE, FONT_SPACING, statusColor);

    EndDrawing();
  }

  UnloadFont(mainFont);
  CloseWindow();
}
