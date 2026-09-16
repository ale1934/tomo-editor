#define PADDING 32
#define FONT_SIZE 32
#define FONT_SPACING 2
#define STATUS_BAR_HEIGHT 50
#define TAB_SPACING 4

#include <filesystem>
#include <fstream>
#include <raylib.h>
#include <string>
#include <vector>

using namespace std;

struct Vector2i {
  int x;
  int y;
};

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
                  STATUS_BAR_HEIGHT, GetColor(0x222222ff));

    string infoText = inputText + ": " + filename;
    DrawTextEx(
        mainFont, infoText.c_str(),
        {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
        FONT_SIZE, FONT_SPACING, GREEN);

    EndDrawing();
  }

  return filename;
}

void DisplayError(string err, Font mainFont) {
  while (GetKeyPressed() != KEY_ENTER) {
    BeginDrawing();

    // Draw bottom status bar
    DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                  STATUS_BAR_HEIGHT, GetColor(0x222222ff));

    DrawTextEx(
        mainFont, err.c_str(),
        {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
        FONT_SIZE, FONT_SPACING, RED);

    EndDrawing();
  }
}

void DisplayInfo(string msg, Font mainFont) {
  while (GetKeyPressed() != KEY_ENTER) {
    BeginDrawing();

    // Draw bottom status bar
    DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                  STATUS_BAR_HEIGHT, GetColor(0x222222ff));

    DrawTextEx(
        mainFont, msg.c_str(),
        {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
        FONT_SIZE, FONT_SPACING, BLUE);

    EndDrawing();
  }
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

  // Load custom font
  Font mainFont = LoadFontEx("resources/Roboto/static/Roboto-Regular.ttf",
                             FONT_SIZE, 0, 250);

  vector<string> document;
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
  vector<Vector2i> occurances;
  bool searchMode = false;
  int searchIndex = 0;

  int maxVisibleLines =
      (GetScreenHeight() - PADDING - STATUS_BAR_HEIGHT) / FONT_SIZE;

  double savedTime = -1.0;
  const double SAVED_FLASH_DURATION = 1.0;

  string currentFile = "untitled.txt";

  if (argc >= 2) {
    currentFile = argv[1];
  }

  LoadFile(document, currentFile, mainFont);

  while (!WindowShouldClose()) {

    // Draw Cursor
    bool cursorVisible = ((int)(GetTime() * 2) % 2) == 0;

    // Get cursor position by measuring text in current line
    int cursorX =
        PADDING + MeasureTextEx(mainFont,
                                document[curLine].substr(0, curLetter).c_str(),
                                FONT_SIZE, FONT_SPACING)
                      .x;
    int cursorY = PADDING + FONT_SIZE * (curLine - scrollOffset);

    curChar = GetCharPressed();

    if (curChar >= 32 && curChar <= 127 && !searchMode) {
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
      occurances = searchForString(
          document, FileInput("Enter String", mainFont), mainFont);
      if (!occurances.empty()) {
        searchMode = true;
        searchIndex = 0;
        curLine = occurances[searchIndex].y;
        curLetter = occurances[searchIndex].x;
        if (curLine >= scrollOffset + maxVisibleLines)
          scrollOffset = curLine - maxVisibleLines + 1;
      } else {
        DisplayInfo("No Matches Found!", mainFont);
      }
    }

    // Go to next occurance if in search mode
    if (searchMode && (IsKeyPressed(KEY_N))) {
      if (searchIndex + 1 < occurances.size()) {
        searchIndex++;
        curLine = occurances[searchIndex].y;
        curLetter = occurances[searchIndex].x;
        if (curLine >= scrollOffset + maxVisibleLines)
          scrollOffset = curLine - maxVisibleLines + 1;
      } else {
        searchIndex = 0;
      }
    }

    if (IsKeyPressed(KEY_F2)) {
      filesystem::path old_file = currentFile.c_str();
      currentFile = FileInput("Rename File To", mainFont);

      if (!filesystem::remove(old_file)) {
        string errorMsg = "Cannot Remove: " + old_file.generic_string() + "!";
        DisplayError(errorMsg, mainFont);
      }
    }

    if (IsKeyPressed(KEY_F3)) {
      string filename = FileInput("Open File", mainFont);
      LoadFile(document, filename, mainFont);
      curLetter = 0;
      curLine = 0;
      scrollOffset = 0;
      currentFile = filename;
    }

    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_J)) {
      int lineNum =
          TextToInteger(FileInput("Line Number", mainFont).c_str()) - 1;
      scrollOffset += (lineNum - curLine);
      curLine = lineNum;
      curLetter = 0;
    }

    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_U)) {
      document[curLine].clear();
      curLetter = 0;
    }

    if (IsKeyPressed(KEY_ESCAPE) && searchMode) {
      searchMode = false;
    }

    // cout << "Current Letter: " << curLetter << endl;
    // cout << "Current Line: " << curLine << endl;

    BeginDrawing();
    ClearBackground(BLACK);

    if (cursorVisible) {
      DrawRectangle(cursorX + 1 + PADDING, cursorY, 3, FONT_SIZE, PINK);
    }

    for (int i = 0; i < maxVisibleLines; i++) {
      int lineIndex = scrollOffset + i;
      if (lineIndex >= document.size())
        break;

      Vector2 textPos = {PADDING * 2, (float)PADDING + FONT_SIZE * i};
      DrawTextEx(mainFont, document[lineIndex].c_str(), textPos, FONT_SIZE,
                 FONT_SPACING, RAYWHITE);
      DrawTextEx(mainFont, to_string(lineIndex + 1).c_str(),
                 {PADDING / 8, (float)FONT_SIZE * i + PADDING}, FONT_SIZE,
                 FONT_SPACING, LIGHTGRAY);
    }

    string infoText = "";
    if (savedTime > 0 && GetTime() - savedTime < SAVED_FLASH_DURATION) {
      DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                    STATUS_BAR_HEIGHT, GetColor(0x222222ff));
      infoText = currentFile + " Saved!";
      DrawTextEx(
          mainFont, infoText.c_str(),
          {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
          FONT_SIZE, FONT_SPACING, GREEN);
    } else {
      // Draw bottom status bar
      DrawRectangle(0, GetScreenHeight() - STATUS_BAR_HEIGHT, GetScreenWidth(),
                    STATUS_BAR_HEIGHT, GetColor(0x222222ff));

      infoText = "Current File: " + currentFile;
      DrawTextEx(
          mainFont, infoText.c_str(),
          {PADDING / 2, (float)GetScreenHeight() - STATUS_BAR_HEIGHT / 2 - 15},
          FONT_SIZE, FONT_SPACING, GREEN);
    }
    EndDrawing();
  }

  UnloadFont(mainFont);
  CloseWindow();
}
