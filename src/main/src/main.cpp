
#include <Arduino.h>
#include <Wire.h>
#include "PCAL9535A.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Fonts/FreeMono9pt7b.h>

#define PIN_UART_LEPTON_RX 16
#define PIN_UART_LEPTON_TX -1

#define TFT_CS 2
#define TFT_DC 5
#define TFT_MOSI 23 // default SPI as used by Adafruit_ST7735
#define TFT_SCLK 18 // default SPI as used by Adafruit_ST7735
#define TFT_RST 4

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

PCAL9535A::PCAL9535A<TwoWire> gpio(Wire);
PCAL9535A::PCAL9535A<TwoWire> gpio2(Wire);
const uint32_t thermixelUpdateRateMs = 5000;

typedef uint8_t LeptonImageBuffer[160 * 120 * 2];
const uint32_t leptonUartTimeoutMs = 8000;

QueueHandle_t queue;

LeptonImageBuffer leptonImageBuffer;
uint16_t image[160 * 120];

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void GetImageFromPtLepton(void *parameter)
{
  const uint8_t magicword[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t magicCount = 0;
  Serial1.begin(250000, SERIAL_8N1, PIN_UART_LEPTON_RX, PIN_UART_LEPTON_TX);
  // Serial1.setTimeout(50);
  LeptonImageBuffer leptonImageBuffer;
  uint32_t start = millis();

  while (true)
  {
    while (Serial1.available())
    {
      uint8_t b = Serial1.read();
      if (b == magicword[magicCount])
      {
        magicCount++;
        if (magicCount == sizeof(magicword))
        {
          magicCount = 0;

          int numBytes = Serial1.readBytes(leptonImageBuffer, sizeof(LeptonImageBuffer));
          if (numBytes == sizeof(leptonImageBuffer))
          {
            Serial.printf("[PT_Lepton] Image received (%u bytes)\n", numBytes);

            if (xQueueSend(queue, (void *)leptonImageBuffer, (TickType_t)0) == pdFALSE)
            {
              Serial.println("[PT_Lepton] Queue full!");
            }

            magicCount = 0;
          }
          else
          {
            Serial.println("[PT_Lepton UART] Wrong number of bytes received!");
          }
        }
      }
      else
      {
        magicCount = 0;
      }
    }

    delay(5);
  }
}

void UpdateDisplay(void *parameter)
{
  while (true)
  {
    if (xQueueReceive(queue, &(leptonImageBuffer), (TickType_t)5))
    {
      uint16_t max{0};
      uint16_t min{65535};
      uint64_t accumulator{0};

      // Convert packed 16-bit values into single 16-bit values
      for (uint32_t i = 0; i < 160 * 120; i++)
      {
        uint16_t high = leptonImageBuffer[i * 2 + 0];
        uint16_t low = leptonImageBuffer[i * 2 + 1];
        image[i] = (high << 8) | low;

        if (image[i] > max)
          max = image[i];
        if (image[i] < min)
          min = image[i];

        accumulator += image[i];
      }

      uint16_t avg = accumulator / (160 * 120);
      Serial.printf("(min, max, avg) (%u, %u, %u)\n", min, max, avg);

      // Manual guess for range as data from Lepton appears to have a significant bias.
      min = avg - 1000;
      max = avg + 1000;

      // Scale values for better image, drop values below min_threshold.
      for (uint32_t i = 0; i < 160 * 120; i++)
      {
        if (image[i] < min)
        {
          image[i] = ST7735_BLACK;
        }
        if (image[i] > max)
        {
          image[i] = ST7735_WHITE;
        }
        else
        {
          // Convert Y16 greyscale to RGB565.
          uint16_t fiveBitColor = map(image[i], min, max, 0, 0x001F);
          uint16_t sixBitColor = map(image[i], min, max, 0, 0x003F);
          image[i] = fiveBitColor << 11 | sixBitColor << 5 | fiveBitColor;
        }
      }

      // Update display.
      int yOffset{0};
       int xOffset{(160 - 128) / 2};
      for (uint32_t i = 0; i < 160 * 120; i++)
      {
        if (i % 160 > 127)
          continue;

        // Add red line to indicate refresh.
        if (i % 160 == 0 && i / 160 != 119)
          tft.drawLine(0 + xOffset, i / 160 + 1 + yOffset, 127 + xOffset, i / 160 + 1 + yOffset, ST7735_RED);

        tft.drawPixel(i % 160 + xOffset, i / 160 + yOffset, image[i]);
      }
    }
  }
}

// Randomize the thermixal values.
void ThermixelsTask(void *parameter)
{
  uint32_t start = millis();

  while (true)
  {
    if (millis() - start > thermixelUpdateRateMs)
    {
      start = millis();
      for (int i = 0; i < 16; i++)
      {
        gpio.pinMode(i, OUTPUT);
        gpio.pinSetDriveStrength(i, PCAL9535A::DriveStrength::P100);
        gpio.digitalWrite(i, random(0, 2));

        gpio2.pinMode(i, OUTPUT);
        gpio2.pinSetDriveStrength(i, PCAL9535A::DriveStrength::P100);
        gpio2.digitalWrite(i, random(0, 2));
      }
    }
    delay(50);
  }
}

void setup()
{
  Serial.begin(115200);

  gpio.begin(PCAL9535A::HardwareAddress::A001);
  gpio2.begin(PCAL9535A::HardwareAddress::A110);

  tft.initR(INITR_BLACKTAB);
  delay(100);
  tft.setRotation(3);
  delay(100);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST7735_RED);
  tft.setTextSize(2);
  
  //tft.setFont(&FreeMono9pt7b);
  tft.setCursor(0, 0);
  tft.print("THERMIXELS");
  delay(2000);
   tft.fillScreen(ST77XX_BLACK);


  queue = xQueueCreate(1, sizeof(LeptonImageBuffer));

  int ret = xTaskCreatePinnedToCore(GetImageFromPtLepton, "GetImageFromPtLepton", 50000, NULL, 1, NULL, 1);
  Serial.println(ret);

  ret = xTaskCreatePinnedToCore(UpdateDisplay, "UpdateDisplay", 9600, NULL, 1, NULL, 0);
  Serial.println(ret);

  ret = xTaskCreatePinnedToCore(ThermixelsTask, "ThermixelsTask", 10000, NULL, 2, NULL, 0);
  Serial.println(ret);
}

void loop()
{
  // Do nothing in main loop since everything is handled by tasks.
  delay(1000);
}
