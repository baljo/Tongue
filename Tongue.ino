/* Edge Impulse ingestion SDK
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Includes ---------------------------------------------------------------- */
#include <Tongue_inferencing.h>
#include <SparkFunBQ27441.h>   // to read battery info
#include "seeed_line_chart.h"  //include the line chart library
#include "TFT_eSPI.h"

TFT_eSPI tft;


#define TDS_Pin A2
#define turbidity_Pin A4
#define MAX_SIZE 100  //maximum size of data
doubles stack[3];
TFT_eSprite spr = TFT_eSprite(&tft);  // Sprite
#define FF17 &FreeSans9pt7b
#define FF20 &FreeSans20pt7b
#define FF24 &FreeSans24pt7b
#define LCD_BACKLIGHT (72Ul)  // Control Pin of LCD

#define FREQUENCY_HZ 5
#define INTERVAL_MS (1000 / (FREQUENCY_HZ + 1))

float features[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
int i = 0;
int sensorValue = 0;
float tdsValue = 0;
float Voltage = 0;
char label[50] = "";

static unsigned long last_interval_ms = 0;

const unsigned int BATTERY_CAPACITY = 650;  // Set Wio Terminal Battery's Capacity


void setupBQ27441(void) {
  // Use lipo.begin() to initialize the BQ27441-G1A and confirm that it's
  // connected and communicating.
  if (!lipo.begin())  // begin() will return true if communication is successful
  {
    // If communication fails, print an error message and loop forever.
    Serial.println("Error: Unable to communicate with BQ27441.");
    Serial.println("  Check wiring and try again.");
    Serial.println("  (Battery must be plugged into Battery Babysitter!)");
    tft.setTextColor(TFT_RED);
    tft.setCursor((320 - tft.textWidth("Battery Not Initialised!")) / 2, 120);
    tft.print("Battery Not Initialised! ?");
    //while (1) ;
  }
  Serial.println("Connected to BQ27441!");

  // Uset lipo.setCapacity(BATTERY_CAPACITY) to set the design capacity
  // of your battery.
  lipo.setCapacity(BATTERY_CAPACITY);
}




/**
 * @brief      Copy raw feature data in out_ptr
 *             Function called by inference library
 *
 * @param[in]  offset   The offset
 * @param[in]  length   The length
 * @param      out_ptr  The out pointer
 *
 * @return     0
 */
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

void print_inference_result(ei_impulse_result_t result);

/**
 * @brief      Arduino setup function
 */
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // comment out the below line to cancel the wait for USB connection (needed for native USB)
  //while (!Serial)
  ;
  Serial.println("Edge Impulse Inferencing Demo");

  tft.begin();
  tft.setRotation(3);
  spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
  spr.setRotation(3);

  setupBQ27441();
}



/**
 * @brief      Arduino main function
 */
void loop() {

  sensorValue = analogRead(TDS_Pin);
  Voltage = sensorValue * 3.3 / 1024.0;                                                                     //Convert analog reading to Voltage
  tdsValue = (133.42 * Voltage * Voltage * Voltage - 255.86 * Voltage * Voltage + 857.39 * Voltage) * 0.5;  //Convert voltage value to TDS value
  tdsValue = round(tdsValue);

  int turbidityValue = analogRead(turbidity_Pin);        // read the input on analog pin 0:
  float turb_voltage = turbidityValue * (3.3 / 1024.0);  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 5V):
  unsigned int soc = lipo.soc();                         // Read state-of-charge (%)
  int current = lipo.current(AVG);                       // Read average current (mA)


  if (millis() > last_interval_ms + INTERVAL_MS) {
    last_interval_ms = millis();

    // by keeping the following rows you can use this program to both acquire data as well as for inferencing!
    Serial.print(tdsValue);
    Serial.print(",");
    Serial.println(turb_voltage);  // print out the value you read:

    if (i >= 19)
      i = 0;

    // filling the features array 
    features[i] = tdsValue;
    features[i + 1] = turb_voltage;

    i = i + 2;
  }


  if (sizeof(features) / sizeof(float) != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
    ei_printf("The size of your 'features' array is not correct. Expected %lu items, but had %lu\n",
              EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, sizeof(features) / sizeof(float));
    delay(1000);
    return;
  }

  ei_impulse_result_t result = { 0 };

  // the features are stored into flash, and we don't want to load everything into RAM
  signal_t features_signal;
  features_signal.total_length = sizeof(features) / sizeof(features[0]);
  features_signal.get_data = &raw_feature_get_data;

  // invoke the impulse
  EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false /* debug */);
  if (res != EI_IMPULSE_OK) {
    ei_printf("ERR: Failed to run classifier (%d)\n", res);
    return;
  }

  // below loop finds the maximum prediction score and uses that as the final prediction
  // you might also want to add a check for uncertainty if the highest score is e.g. < 60 %  
  float max = -1.0;
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > max) {
      max = result.classification[i].value;
      strncpy(label, ei_classifier_inferencing_categories[i], sizeof(label));
      label[0] = toupper(label[0]);
    }
    // uncomment these if you want to print the prediction to the terminal
    /*    ei_printf(" %s: ", label);
    ei_printf("%.5f\r\n", max);*/
  }


  if (stack[0].size() == MAX_SIZE) {
    for (uint8_t i = 0; i < 3; i++) {
      stack[i].pop();  //this is used to remove the first read variable
    }
  }
  stack[0].push(tdsValue);  //read variables and store in data
  stack[1].push(tdsValue);
  stack[2].push(tdsValue);

  draw_chart(turb_voltage, soc, current, max);

}


void draw_chart(float turb_voltage, unsigned int soc, int current, float max) {
  spr.fillSprite(TFT_WHITE);


  spr.setFreeFont(FF17);
  spr.setTextColor(TFT_BLACK);
  spr.drawString(String(turb_voltage) + "/" + String(round(tdsValue)) + " PPM", 170, 10);
  spr.drawString("B:" + String(soc) + "% " + " Curr.:" + String(current), 10, 220);

  int equal = strcmp(label, "Air");
  float prediction_perc = round(max * 100);

  if (equal == 0) {
    spr.drawString(label, 170, 220);
    spr.drawString(String(prediction_perc) + "%", 195, 220);
  } else {
    spr.setFreeFont(&FreeSansBold18pt7b);
    spr.setTextColor(TFT_RED);
    spr.drawString(label, 50, 120);
    spr.setFreeFont(&FreeSansBold12pt7b);
    spr.setTextColor(TFT_BLUE);
    spr.drawString(String(prediction_perc) + " %", 50, 160);
  }

  int y_low = 0;
  //Settings for the line graph title
  auto header = text(0, 0)
                  .value(" Turb./TDS")
                  .align(left)
                  .valign(vcenter)
                  .width(spr.width())
                  .thickness(2);

  header.height(header.font_height(&spr) * 2);
  header.draw(&spr);  // Header height is the twice the height of the font

  //  y_low = trunc(tdsValue / 4 * 3);
  y_low = 0.0;

  //Settings for the line graph
  auto content = line_chart(20, header.height());  //(x,y) where the line graph begins
  content
    .height(spr.height() - header.height() * 1.5)  //actual height of the line chart
    .width(spr.width() - content.x() * 2)          //actual width of the line chart
    .based_on(y_low)                               //Starting point of y-axis, must be a float
    .show_circle(false)                            //drawing a cirle at each point, default is on.
    .value({ stack[0], stack[1], stack[2] })       //passing through the data to line graph
    .max_size(MAX_SIZE)
    .color(TFT_BLUE, TFT_RED, TFT_BLUE)
    .backgroud(TFT_WHITE)
    .draw(&spr);

  spr.pushSprite(0, 0);
}


void print_inference_result(ei_impulse_result_t result) {

  /*  // Print how long it took to perform inference
  ei_printf("Timing: DSP %d ms, inference %d ms, anomaly %d ms\r\n",
            result.timing.dsp,
            result.timing.classification,
            result.timing.anomaly);
*/
  if (Serial) {
    ei_printf("Predictions:\r\n");
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
      ei_printf("  %s: ", ei_classifier_inferencing_categories[i]);
      ei_printf("%.5f\r\n", result.classification[i].value);
    }
  }
}