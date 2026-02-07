#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <JPEGDEC.h>
#include "Audio.h"

#define PIN_BTN_UP 13
#define PIN_BTN_DOWN 14
#define PIN_BTN_SEL 8

#define SD_MISO 1
#define SD_MOSI 2
#define SD_SCK 42
#define SD_CS 41

#define I2S_BCLK 5
#define I2S_LRC 6
#define I2S_DOUT 4

TFT_eSPI tft = TFT_eSPI();
JPEGDEC jpeg;
Audio audio;
SPIClass sdSPI = SPIClass(HSPI);

#define MJPEG_BUFFER_SIZE (1024 * 60)
uint8_t *mjpeg_buf;

File videoFile;
String fileList[50];
int fileCount = 0;
int selectedIndex = 0;
bool isPlaying = false;
unsigned long lastFrameTime = 0;
const int fps = 25;
const int frameDelay = 1000 / fps;

bool checkButton(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(30);
    if (digitalRead(pin) == LOW) {
      while (digitalRead(pin) == LOW)
        ;
      return true;
    }
  }
  return false;
}

int JPEGDraw(JPEGDRAW *pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

void scanFiles() {
  fileCount = 0;
  File root = SD.open("/");
  if (!root) return;

  File file = root.openNextFile();
  while (file) {
    String fileName = String(file.name());
    if (fileName.endsWith(".mjpeg") && !fileName.startsWith(".")) {
      fileList[fileCount] = fileName;
      fileCount++;
      if (fileCount >= 50) break;
    }
    file = root.openNextFile();
  }
}

void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 10);
  tft.println(" SELECT VIDEO:");
  tft.drawFastHLine(0, 35, 280, TFT_BLUE);

  for (int i = 0; i < fileCount; i++) {
    tft.setCursor(10, 50 + (i * 25));
    if (i == selectedIndex) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.print("> ");
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.print("  ");
    }
    tft.println(fileList[i]);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SEL, INPUT_PULLUP);

  if (psramInit()) {
    mjpeg_buf = (uint8_t *)ps_malloc(MJPEG_BUFFER_SIZE);
  } else {
    mjpeg_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);
  }

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 40000000)) {
    tft.init();
    tft.fillScreen(TFT_RED);
    tft.println("SD Fail");
    while (1)
      ;
  }

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(14);

  scanFiles();
  drawMenu();
}

void playVideo(String videoName) {
  String audioName = videoName;
  audioName.replace(".mjpeg", ".wav");

  videoFile = SD.open("/" + videoName);
  if (!videoFile) return;

  if (SD.exists("/" + audioName)) {
    audio.connecttoFS(SD, ("/" + audioName).c_str());
  }

  isPlaying = true;
  tft.fillScreen(TFT_BLACK);

  int bytesInBuf = 0;
  bytesInBuf = videoFile.read(mjpeg_buf, MJPEG_BUFFER_SIZE);

  while (isPlaying) {
    audio.loop();

    int frameStartIdx = -1;
    int frameEndIdx = -1;

    for (int i = 0; i < bytesInBuf - 1; i++) {
      if (mjpeg_buf[i] == 0xFF) {
        if (mjpeg_buf[i + 1] == 0xD8) frameStartIdx = i;
        else if (mjpeg_buf[i + 1] == 0xD9) {
          frameEndIdx = i + 1;
          if (frameStartIdx != -1) break;
        }
      }
    }

    if (frameStartIdx != -1 && frameEndIdx != -1) {
      int frameSize = frameEndIdx - frameStartIdx + 1;

      while (millis() - lastFrameTime < frameDelay) {
        audio.loop();
        if (checkButton(PIN_BTN_SEL)) {
          isPlaying = false;
          break;
        }
      }
      lastFrameTime = millis();

      if (jpeg.openRAM(mjpeg_buf + frameStartIdx, frameSize, JPEGDraw)) {
        int x = (tft.width() - jpeg.getWidth()) / 2;
        int y = (tft.height() - jpeg.getHeight()) / 2;

        jpeg.decode(x, y, 0); 
        jpeg.close();
      }

      int remaining = bytesInBuf - (frameEndIdx + 1);
      if (remaining > 0) memmove(mjpeg_buf, mjpeg_buf + frameEndIdx + 1, remaining);

      int toRead = MJPEG_BUFFER_SIZE - remaining;
      if (toRead > 0) {
        int actuallyRead = videoFile.read(mjpeg_buf + remaining, toRead);
        bytesInBuf = remaining + actuallyRead;
        if (actuallyRead == 0 && remaining == 0) isPlaying = false;
      } else {
        bytesInBuf = remaining;
      }

    } else {
      if (bytesInBuf < MJPEG_BUFFER_SIZE) {
        int actuallyRead = videoFile.read(mjpeg_buf + bytesInBuf, MJPEG_BUFFER_SIZE - bytesInBuf);
        if (actuallyRead == 0) isPlaying = false;
        bytesInBuf += actuallyRead;
      } else {
        memmove(mjpeg_buf, mjpeg_buf + 1, MJPEG_BUFFER_SIZE - 1);
        bytesInBuf--;
      }
    }

    if (checkButton(PIN_BTN_SEL)) {
      isPlaying = false;
      audio.stopSong();
    }
  }

  videoFile.close();
  drawMenu();
}

void loop() {
  if (!isPlaying) {
    if (checkButton(PIN_BTN_DOWN)) {
      selectedIndex++;
      if (selectedIndex >= fileCount) selectedIndex = 0;
      drawMenu();
    }
    if (checkButton(PIN_BTN_UP)) {
      selectedIndex--;
      if (selectedIndex < 0) selectedIndex = fileCount - 1;
      drawMenu();
    }
    if (checkButton(PIN_BTN_SEL)) {
      if (fileCount > 0) playVideo(fileList[selectedIndex]);
    }
  }
}