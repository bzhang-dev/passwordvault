/**
   PasswordVault - Use a WIO Terminal to type passwords
   (c) 2021 Olav Schettler <olav@schettler.net>
*/
#include <Arduino.h> // for platformio

#define CODE_VERSION "v1.10"

#include <xxtea-lib.h>

#include <SPI.h>
#include <TFT_eSPI.h>

#include "Keyboard.h"

#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite display = TFT_eSprite(&tft);

const char* PASSWD;

int strncasecmp(const char*, const char*, int);
char* strdup(const char*);

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define SELECT 5
#define MODE_FILTER 6
#define MODE_LIST 7
#define MODE_LOCK 8
#define MODE_FAV 9
#define MODE_DETAIL 10
int mode = MODE_LOCK;

#define SCREEN_SIZE 13
unsigned int list_size;
unsigned int filtered_list_size;
unsigned int fav_list_size = 0;

typedef struct {
  char* passwd;
  char* name;
} Entry;

Entry* entries;
char* buffer;

Entry** filtered_entries;
Entry** fav_entries;
Entry* current_entry;

#define FILTER_SIZE 256
char filter[FILTER_SIZE];
#define FILTER_WIDTH 10
unsigned int filter_lines;
unsigned int filter_size = 0;

char lock[] = "123456789<0>";
#define LOCK_WIDTH 3

unsigned int cursor_x = 0, cursor_y = 0;

unsigned int offset = 0;
unsigned int cursor = 0;
unsigned int prefix_pos = 0;


int
countLines(File file, int* line_length) {
  int i = 0;
  int l = 0;
  int c;
  int k;
  int first_field = 1;
  char filter_tmp[FILTER_SIZE];

  *line_length = 0;

  memset(filter_tmp, 0, FILTER_SIZE);

  while (file.available()) {
    if ((c = file.read()) == '\n') {
      i++;
      first_field = 1;

      if (l > *line_length) {
        *line_length = l;
      }
      l = 0;
    }
    else {
      if (c == '\t') {
        first_field = 0;
      }
      if (first_field) {
        filter_tmp[tolower(c)] = 1;
      }
      l++;
    }
  }
  file.seek(0);

  if (l > *line_length) {
    *line_length = l;
  }

  memset(filter, 0, FILTER_SIZE);
  for (c = 0, k = 0; c < FILTER_SIZE; c++) {
    if (filter_tmp[c]) {
      filter[k++] = c;
    }
  }
  filter_size = k;

  return i;
}


char*
readField(File file) {
  char c;
  int pos = 0;
  while (file.available() && (c = file.read()) != '\t' && c != '\n') {
    buffer[pos++] = c;
  }
  buffer[pos] = '\0';
  return buffer;
}


void
readLine(File file, Entry* entry) {
  entry->name = strdup(readField(file));
  entry->passwd = strdup(xxtea.decrypt(readField(file)).c_str());
}


void
readFile(fs::FS& fs, const char* path) {
  int i = 0;
  int line_length;

  Serial.print("Reading file: ");
  Serial.println(path);
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  list_size = countLines(file, &line_length);
  Serial.print("Lines: ");
  Serial.println(list_size);

  buffer = (char*)malloc(line_length + 1);
  entries = (Entry*)malloc(sizeof(Entry) * list_size);
  filtered_entries = (Entry**)malloc(sizeof(Entry*) * list_size);
  fav_entries = (Entry**)malloc(sizeof(Entry*) * list_size);

  while (file.available()) {
    readLine(file, &entries[i]);
    i++;
  }
  file.close();

  Serial.println("Done.");
}


void
writeFav(fs::FS& fs, const char* path) {
  Serial.print("Writing fav: ");
  Serial.println(path);
  File file = fs.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  for (unsigned int i = 0; i < fav_list_size; i ++) {
    file.write(fav_entries[i]->name);
    file.write("\n");
  }
  file.close();
  Serial.println("Done.");
}


void
readFav(fs::FS& fs, const char* path) {
  char* name;
  bool found;

  Serial.print("Reading fav: ");
  Serial.println(path);
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    name = readField(file);
    found = false;
    for (unsigned int i = 0; i < list_size; i++) {
      if (0 == strcmp(entries[i].name, name)) {
        fav_entries[fav_list_size++] = &entries[i];
        Serial.print("Fav ");
        Serial.print(name);
        Serial.println(" added");
        found = true;
        break;
      }
    }
    if (!found) {
      Serial.print("Fav ");
      Serial.print(name);
      Serial.println(" not found");
    }
  }
  file.close();

  Serial.print(fav_list_size);
  Serial.println(" favs read.");
}


void
setupJoystick() {
  pinMode(WIO_5S_UP, INPUT_PULLUP);
  pinMode(WIO_5S_DOWN, INPUT_PULLUP);
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);

  pinMode(WIO_KEY_C, INPUT_PULLUP);
}


void
setup() {
  Serial.begin(115200);
  //while (!Serial);

# include "./env.h"
  ; 

  xxtea.setKey(PASSWD);

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  display.setColorDepth(8);
  display.createSprite(240, 320);
  display.fillSprite(TFT_BLACK);

  setupJoystick();

  if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI, 4000000UL)) {
    Serial.println("Card mount failed");
    return;
  }

  uint8_t card_type = SD.cardType();
  if (card_type == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Keyboard.begin();

  Serial.print("PasswordVault ");
  Serial.println(CODE_VERSION);

  readFile(SD, "/crypted.txt");
  readFav(SD, "/fav.txt");
  buffer[0] = '\0';
}


bool 
prefix(const char *pre, const char *str) {
  return strncasecmp(pre, str, strlen(pre)) == 0;
}


int
filterEntries() {
  int count = 0;
  for (unsigned int i = 0; i < list_size; i++) {
    if (prefix(buffer, entries[i].name)) {
      filtered_entries[count++] = &entries[i];
    }
  }
  return count;
}


void
about() {
  const int OVERLAY_Y = 220;
  display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.setTextSize(2);
  display.drawCentreString("PasswordVault", 120, OVERLAY_Y, 2);
  display.setTextSize(1);
  display.drawCentreString(CODE_VERSION, 120, OVERLAY_Y + 40, 1);
  display.drawCentreString("(c) 2021 Olav Schettler", 120, OVERLAY_Y + 56, 1);
  display.drawCentreString("info@passwordvault.de", 120, OVERLAY_Y + 72, 1);  
}


void
showLock() {
  unsigned int x = 0, y = 0;

  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);

  display.drawString(">", 20, 10);
  display.drawString(buffer, 40, 10);

  display.drawFastHLine(0, 30, 240, TFT_WHITE);

  for (unsigned int i = 0; i < sizeof(lock); i++) {
    char c[2] = { lock[i], '\0' };
    if (x == cursor_x && y == cursor_y) {
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    else {
      display.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    display.drawString(c, 90 + x * 20, 50 + y * 20);
    x++;
    if (x >= LOCK_WIDTH) {
      x = 0;
      y++;
    }
  }

  display.drawFastHLine(0, 150, 240, TFT_WHITE);
  display.drawCentreString("Please unlock", 120, 160, 1);

  about();

  display.pushSprite(0, 0);
}


void
showFilter() {
  unsigned int x = 0, y = 0;

  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);

  display.drawString(">", 20, 10);
  display.drawString(buffer, 40, 10);

  display.drawFastHLine(0, 30, 240, TFT_WHITE);

  for (unsigned int i = 0; i < filter_size; i++) {
    char c[2] = { filter[i], '\0' };
    if (x == cursor_x && y == cursor_y) {
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    else {
      display.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    display.drawString(c, 20 + x * 20, 50 + y * 20);
    x++;
    if (x >= FILTER_WIDTH) {
      x = 0;
      y++;
    }
  }
  filter_lines = y;

  filtered_list_size = filterEntries();

  display.drawFastHLine(0, 80 + filter_lines * 20, 240, TFT_WHITE);

  display.setCursor(20, 100 + filter_lines * 20);
  display.print(filtered_list_size);
  display.print(" passwords");

  about();

  display.pushSprite(0, 0);
}


void
showList() {
  Serial.print("All offset / cursor: ");
  Serial.print(offset);
  Serial.print(" ");
  Serial.println(cursor);

  display.fillScreen(TFT_BLACK);
  display.setTextSize(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);

  display.drawString("#", 20, 10);
  display.drawString(buffer, 40, 10);

  display.drawFastHLine(0, 30, 240, TFT_WHITE);

  for (unsigned int i = 0; offset + i < filtered_list_size && i < SCREEN_SIZE; i++) {
    if (i == cursor) {
      display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    else {
      display.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    display.drawString(filtered_entries[offset + i]->name, 20, 50 + 20 * i);
  }
  display.pushSprite(0, 0);
}


int
checkButtons() {
  if (digitalRead(WIO_5S_LEFT) == LOW) {
    return UP;
  }
  else
  if (digitalRead(WIO_5S_RIGHT) == LOW) {
    return DOWN;
  }
  else
  if (digitalRead(WIO_5S_UP) == LOW) {
    return RIGHT;
  }
  else
  if (digitalRead(WIO_5S_DOWN) == LOW) {
    return LEFT;
  }
  else
  if (digitalRead(WIO_5S_PRESS) == LOW) {
    return SELECT;
  }
  else
  if (digitalRead(WIO_KEY_C) == LOW) {
    offset = 0;
    cursor = 0;
  
    switch (mode) {
      case MODE_LIST:
        buffer[0] = '\0';
        return MODE_FILTER;
      
      case MODE_FILTER:
        if (strlen(buffer) > 0) {
          return MODE_LIST;
        }
        else {
          return MODE_FAV;
        }
      
      case MODE_FAV:
        return MODE_LIST;
      
      case MODE_DETAIL:
        return MODE_LIST;
    }
    Serial.print("mode: ");
    Serial.println(mode);
  }

  return 0;
}


int
getButtons() {
  int cmd = 0;
  do {
    delay(150);
  }
  while (!(cmd = checkButtons()));

  return cmd;
}


void
lockCursor() {
  int i;
  int buffer_len;
  int cmd = getButtons();

  switch (cmd) {
    case LEFT:
      if (cursor_x > 0) {
        cursor_x--;
      }
      break;

    case RIGHT:
      if (cursor_x < LOCK_WIDTH - 1) {
        cursor_x++;
      }
      break;

    case UP:
      if (cursor_y > 0) {
        cursor_y--;
      }
      break;

    case DOWN:
      if (cursor_y < sizeof(lock) / LOCK_WIDTH - 1) {
        cursor_y++;
      }
      break;

    case SELECT:
      i = cursor_y * LOCK_WIDTH + cursor_x;
      buffer_len = strlen(buffer);
      switch (lock[i]) {
        case '<':
          if (buffer_len > 0) {
            buffer[buffer_len - 1] = '\0';
          }
          break;

        case '>':
          if (0 == strcmp(PASSWD, buffer)) {
            Serial.println("Unlocked :)");
            mode = MODE_FILTER;
            buffer[0] = '\0';
            cursor_x = 0;
            cursor_y = 0;
          }
          else {
            buffer[0] = '\0';
          }
          break;

        default:
          if (buffer_len > 7) {
            break;
          }
          buffer[buffer_len] = lock[i];
          buffer[buffer_len + 1] = '\0';
      }
      break;
  }
}

void
filterCursor() {
  unsigned int i, new_i;
  unsigned int buffer_len;
  int cmd = getButtons();

  switch (cmd) {
    case LEFT:
      if (cursor_x > 0) {
        cursor_x--;
      }
      break;

    case RIGHT:
      new_i = cursor_y * FILTER_WIDTH + cursor_x + 1;
      if (cursor_x < FILTER_WIDTH - 1 && new_i < filter_size - 1) {
        cursor_x++;
      }
      break;

    case UP:
      if (cursor_y > 0) {
        cursor_y--;
      }
      break;

    case DOWN:
      new_i = (cursor_y + 1) * FILTER_WIDTH + cursor_x;
      if (cursor_y < filter_lines && new_i < filter_size) {
        cursor_y++;
      }
      break;

    case SELECT:
      i = cursor_y * FILTER_WIDTH + cursor_x;
      buffer_len = strlen(buffer);
      buffer[buffer_len] = filter[i];
      buffer[buffer_len + 1] = '\0';
      break;

    case MODE_FAV:
    case MODE_LIST:
    case MODE_FILTER:
      mode = cmd;
      break;
  }
  i = cursor_y * FILTER_WIDTH + cursor_x;
  Serial.print("Filter [");
  Serial.print(i);
  Serial.print("] = ");
  Serial.print(filter[i]);
  Serial.print(", filter size: ");
  Serial.println(filter_size);
}


void
typeAndFavEntry(Entry* entry) {
  unsigned int i;
  bool grow = true;

  Keyboard.print(entry->passwd);
  Serial.println(entry->name);

  // Insert the entry as the first favorite

  // First, search the entry from the start of the favorites
  for (i = 0; i < fav_list_size; i++) {
    if (fav_entries[i] == entry) {
      grow = false;
      //D Serial.print("already at ");
      //D Serial.println(i);
      break;
    }
  }
  //D Serial.print("move up to ");
  //D Serial.println(i);
  // Second, move all favorites one up, making space for the new entry at the head of the list
  // This overrides an old occurance of there was one, moving it to the head of the list
  for (int j = i; j > 0; j--) {
    fav_entries[j] = fav_entries[j-1];
  }
  fav_entries[0] = entry;
  if (grow) {
    fav_list_size++;
      writeFav(SD, "/fav.txt");
  }
}

void
listCursor() {
  int cmd = getButtons();

  switch (cmd) {
    case UP:
      if (cursor > 0) {
        cursor--;
      }
      else
      if (offset > 0) {
        offset--;
      }
      break;

    case DOWN:
      if (cursor < min(SCREEN_SIZE - 1, filtered_list_size - 1)) {
        cursor++;
      }
      else
      if (cursor == SCREEN_SIZE - 1 && offset < filtered_list_size - SCREEN_SIZE) {
        offset++;
      }
      break;

    case SELECT:
      current_entry = filtered_entries[offset + cursor];
      typeAndFavEntry(current_entry);
      mode = MODE_DETAIL;
      break;

    case MODE_FAV:
    case MODE_LIST:
    case MODE_FILTER:
      mode = cmd;
      break;
  }
}

class FavController {
  private:
    Entry** entries;
    unsigned int list_size;

  public:
    FavController(Entry** entries, int list_size) {
      this->entries = entries;
      this->list_size = list_size;
    }

    void
    show() {
      Serial.print("Fav offset=");
      Serial.print(offset);
      Serial.print(" cursor=");
      Serial.print(cursor);
      Serial.print(" size=");
      Serial.println(this->list_size);

      display.fillScreen(TFT_BLACK);
      display.setTextSize(2);
      display.setTextColor(TFT_WHITE, TFT_BLACK);

      display.drawString("*", 20, 10);
      display.drawString(buffer, 40, 10);

      display.drawFastHLine(0, 30, 240, TFT_WHITE);

      if (this->list_size > 0) {
        for (unsigned int i = 0; offset + i < this->list_size && i < SCREEN_SIZE; i++) {
          if (i == cursor) {
            display.setTextColor(TFT_BLACK, TFT_WHITE);
          }
          else {
            display.setTextColor(TFT_WHITE, TFT_BLACK);
          }
          //D Serial.println(offset + i);
          //D Serial.println(fav_entries[offset + i]->name);
          display.drawString(fav_entries[offset + i]->name, 20, 50 + 20 * i);
        }
      }
      else {
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setTextSize(2);
        display.drawCentreString("No favorites yet", 120, 160, 1);
      }
      display.pushSprite(0, 0);
    }

    void
    update() {
      int cmd = getButtons();

      switch (cmd) {
        case UP:
          if (cursor > 0) {
            cursor--;
          }
          else
          if (offset > 0) {
            offset--;
          }
          break;

        case DOWN:
          if (cursor < min(SCREEN_SIZE - 1, this->list_size - 1)) {
            cursor++;
          }
          else
          if (cursor == SCREEN_SIZE - 1 && offset < this->list_size - SCREEN_SIZE) {
            offset++;
          }
          break;

        case SELECT:
          current_entry = this->entries[offset + cursor];
          typeAndFavEntry(current_entry);
          mode = MODE_DETAIL;
          offset = 0;
          cursor = 0;
          break;

        case MODE_FAV:
        case MODE_LIST:
        case MODE_FILTER:
          mode = cmd;
          break;
      }
    }
};


class DetailController {
  public:
    void
    show() {
      display.fillScreen(TFT_BLACK);
      display.setTextSize(2);
      display.setTextColor(TFT_WHITE, TFT_BLACK);

      display.drawString(":", 20, 10);
      display.drawString(current_entry->name, 40, 10);

      display.drawFastHLine(0, 30, 240, TFT_WHITE);

      display.drawString(current_entry->passwd, 20, 50);

      display.pushSprite(0, 0);
    }

    void
    update() {
      int cmd = getButtons();

      switch (cmd) {
        case MODE_FAV:
        case MODE_LIST:
        case MODE_FILTER:
          mode = cmd;
          break;
      }
    }
};


void
loop() {
  switch (mode) {
    case MODE_FILTER:    
      showFilter();
      filterCursor();
      break;

    case MODE_LIST:
      showList();
      listCursor();
      break;

    case MODE_FAV: {
      FavController ctl = FavController(fav_entries, fav_list_size);
      ctl.show();
      ctl.update();
      break;
    }

    case MODE_LOCK:
      showLock();
      lockCursor();
      break;
    
    case MODE_DETAIL: {
      DetailController ctl = DetailController();
      ctl.show();
      ctl.update();
      break;
    }
  }
}
