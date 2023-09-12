
#include <M5GFX.h>
#include <M5Unified.h>

#include "utilities/smooth_menu/src/smooth_menu.h"
#include "icon_temp.h"
#include "icon_hum.h"
#include "icon_pa.h"
#include "icon_air.h"
#include "string.h"

// #define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000)
#define STATE_SAVE_PERIOD UINT32_C(5 * 60 * 1000)

#define BSEC_MAX_STATE_BLOB_SIZE (221)

#if defined(ARDUINO_ARCH_ESP32) || (ARDUINO_ARCH_ESP8266)
#include <EEPROM.h>
#define USE_EEPROM
static uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
#endif

#ifndef NATIVE_PLATFORM

#include <bsec2.h>
#include "config/FieldAir_HandSanitizer/FieldAir_HandSanitizer.h"

#else
#define millis SDL_GetTicks
#define delay  SDL_Delay
#endif

M5GFX display;
M5Canvas canvas(&display);

float iaq          = 0.0;
float iaq_accuracy = 0.0;
float temperature  = 0.0;
float humidity     = 0.0;
float pressure     = 0.0;

struct custom_data_t {
    std::string tag;
    uint16_t bg_color;
    void* icon;
};

struct My_SimpleMenu_CB : public SMOOTH_MENU::SimpleMenuCallback_t {
    void renderCallback(const std::vector<SMOOTH_MENU::Item_t*>& menuItemList,
                        const SMOOTH_MENU::RenderAttribute_t& selector,
                        const SMOOTH_MENU::RenderAttribute_t& camera) {
        canvas.clear();
        /* Draw menu */
        canvas.setFont(&fonts::Orbitron_Light_24);
        canvas.setTextColor(TFT_WHITE);
        canvas.setTextDatum(middle_left);

        canvas.setTextSize(1);
        custom_data_t* selectItem =
            (custom_data_t*)menuItemList[selector.targetItem]->userData;

        for (int i = 0; i < menuItemList.size(); i++) {
            if (i != selector.targetItem) {
                custom_data_t* item = (custom_data_t*)menuItemList[i]->userData;
                canvas.fillSmoothRoundRect(menuItemList[i]->x,
                                           menuItemList[i]->y, 28, 45, 3,
                                           item->bg_color);
                canvas.setTextDatum(middle_left);
                canvas.drawString(menuItemList[i]->tag.substr(0, 1).c_str(),
                                  menuItemList[i]->x + 5,
                                  menuItemList[i]->y + 22);

            } else {
                canvas.fillSmoothRoundRect(selector.x, selector.y,
                                           selector.width, 45, 3,
                                           selectItem->bg_color);

                canvas.drawString(selectItem->tag.c_str(), selector.x + 5,
                                  selector.y + 22);
            }
        }

        canvas.setTextSize(2);
        canvas.setFont(&fonts::Orbitron_Light_24);
        canvas.setTextDatum(middle_center);

        switch (selector.targetItem) {
            case 0:
                canvas.pushImage(130, 30, 100, 100,
                                 (uint16_t*)selectItem->icon);
                canvas.drawString(String(temperature) + "C", 175, 195);
                break;
            case 1:
                canvas.pushImage(130, 30, 100, 100,
                                 (uint16_t*)selectItem->icon);
                canvas.drawString(String(humidity) + "%", 175, 195);

                break;
            case 2:
                canvas.pushImage(130, 30, 100, 100,
                                 (uint16_t*)selectItem->icon);

                canvas.drawString(String((int)pressure / 1000) + +"kPa", 175,
                                  195);
                break;
            case 3:
                canvas.pushImage(130, 30, 100, 100,
                                 (uint16_t*)selectItem->icon);

                canvas.drawString(String(iaq), 175, 195);
                break;
        }

        canvas.pushSprite(0, 0);
    }
};
SMOOTH_MENU::Simple_Menu simple_menu;
Bsec2 envSensor;

void checkBsecStatus(Bsec2 bsec);

bool loadState(Bsec2 bsec) {
#ifdef USE_EEPROM

    if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE) {
        /* Existing state in EEPROM */
        Serial.println("Reading state from EEPROM");
        Serial.print("State file: ");
        for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
            bsecState[i] = EEPROM.read(i + 1);
            Serial.print(String(bsecState[i], HEX) + ", ");
        }
        Serial.println();

        if (!bsec.setState(bsecState)) return false;
    } else {
        /* Erase the EEPROM with zeroes */
        Serial.println("Erasing EEPROM");

        for (uint8_t i = 0; i <= BSEC_MAX_STATE_BLOB_SIZE; i++)
            EEPROM.write(i, 0);

        EEPROM.commit();
    }
#endif
    return true;
}

bool saveState(Bsec2 bsec) {
#ifdef USE_EEPROM
    if (!bsec.getState(bsecState)) return false;

    Serial.println("Writing state to EEPROM");
    Serial.print("State file: ");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
        EEPROM.write(i + 1, bsecState[i]);
        Serial.print(String(bsecState[i], HEX) + ", ");
    }
    Serial.println();

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
#endif
    return true;
}

void updateBsecState(Bsec2 bsec) {
    static uint16_t stateUpdateCounter = 0;
    bool update                        = false;

    if (!stateUpdateCounter ||
        (stateUpdateCounter * STATE_SAVE_PERIOD) < millis()) {
        /* Update every STATE_SAVE_PERIOD minutes */
        update = true;
        stateUpdateCounter++;
    }

    if (update && !saveState(bsec)) checkBsecStatus(bsec);
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs,
                     Bsec2 bsec) {
    if (!outputs.nOutputs) {
        return;
    }

    Serial.println(
        "BSEC outputs:\n\ttimestamp = " +
        String((int)(outputs.output[0].time_stamp / INT64_C(1000000))));
    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData output = outputs.output[i];
        switch (output.sensor_id) {
            case BSEC_OUTPUT_IAQ:
                Serial.println("\tiaq = " + String(output.signal));
                Serial.println("\tiaq accuracy = " +
                               String((int)output.accuracy));

                iaq          = output.signal;
                iaq_accuracy = output.accuracy;
                break;
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                Serial.println("\ttemperature = " + String(output.signal));
                temperature = output.signal;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                Serial.println("\tpressure = " + String(output.signal));
                pressure = output.signal;

                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                Serial.println("\thumidity = " + String(output.signal));
                humidity = output.signal;

                break;
            case BSEC_OUTPUT_RAW_GAS:
                Serial.println("\tgas resistance = " + String(output.signal));
                break;
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                Serial.println("\tstabilization status = " +
                               String(output.signal));
                break;
            case BSEC_OUTPUT_RUN_IN_STATUS:
                Serial.println("\trun in status = " + String(output.signal));
                break;
            default:
                break;
        }
    }
    updateBsecState(envSensor);
}

void setup() {
    M5.begin();
    display.begin();
    if (display.width() < display.height()) {
        display.setRotation(display.getRotation() ^ 1);
    }
    canvas.setFont(&fonts::lgfxJapanMincho_8);

    canvas.setTextWrap(false);
    canvas.createSprite(display.width(), display.height());
    My_SimpleMenu_CB my_cb;

    simple_menu.init(320, 240);
    simple_menu.setRenderCallback(&my_cb);
    auto cfg            = simple_menu.getCamera()->config();
    cfg.animPath_x      = LVGL::overshoot;
    cfg.animPath_y      = LVGL::overshoot;
    cfg.animPath_width  = LVGL::overshoot;
    cfg.animPath_height = LVGL::overshoot;
    cfg.animTime_x      = 400;
    cfg.animTime_width  = 400;
    cfg.animTime_y      = 400;
    cfg.animTime_height = 400;

    simple_menu.getCamera()->config(cfg);
    simple_menu.setMenuLoopMode(true);

    canvas.setTextSize(2);
    /* 8x8 */
    int text_width  = 24;
    int text_height = 24;
    int text_size   = 1;

#ifdef USE_EEPROM
    EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1);
#endif

    // std::string tag_list[] = {"Temperature", "Humidity", "Pressure", "Gas"};

    custom_data_t item1 = {
        .tag = "Tem", .bg_color = CYAN, .icon = (void*)image_data_icon_temp};

    custom_data_t item2 = {
        .tag = "Hum", .bg_color = 0x1c9f, .icon = (void*)image_data_icon_hum};

    custom_data_t item3 = {
        .tag = "Pre", .bg_color = GREEN, .icon = (void*)image_data_icon_pa};

    custom_data_t item4 = {
        .tag = "IAQ", .bg_color = SKYBLUE, .icon = (void*)image_data_icon_air};

    simple_menu.getMenu()->addItem(item1.tag,
                                   // 0,      // Align center
                                   0,  // Align left
                                   12,
                                   text_width * text_size * item1.tag.length(),
                                   text_height * text_size, (void*)&item1);

    simple_menu.getMenu()->addItem(item2.tag,
                                   // 0,      // Align center
                                   0,  // Align left
                                   69,
                                   text_width * text_size * item2.tag.length(),
                                   text_height * text_size, (void*)&item2);

    simple_menu.getMenu()->addItem(item3.tag,
                                   // 0,      // Align center
                                   0,  // Align left
                                   126,
                                   text_width * text_size * item3.tag.length(),
                                   text_height * text_size, (void*)&item3);

    simple_menu.getMenu()->addItem(item4.tag,
                                   // 0,      // Align center
                                   0,  // Align left
                                   183,
                                   text_width * text_size * item4.tag.length(),
                                   text_height * text_size, (void*)&item4);
    long start = millis();

    /* Desired subscription list of BSEC2 outputs */
    bsecSensor sensorList[] = {
        BSEC_OUTPUT_IAQ,          BSEC_OUTPUT_RAW_TEMPERATURE,
        BSEC_OUTPUT_RAW_PRESSURE, BSEC_OUTPUT_RAW_HUMIDITY,
        BSEC_OUTPUT_RAW_GAS,      BSEC_OUTPUT_STABILIZATION_STATUS,
        BSEC_OUTPUT_RUN_IN_STATUS};

    Wire.begin();

    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire)) {
        checkBsecStatus(envSensor);
    }

    /* Load the configuration string that stores information on how to classify
     * the detected gas */
    if (!envSensor.setConfig(FieldAir_HandSanitizer_config)) {
        checkBsecStatus(envSensor);
    }

    /* Copy state from the EEPROM to the algorithm */
    if (!loadState(envSensor)) {
        checkBsecStatus(envSensor);
    }

    /* Subsribe to the desired BSEC2 outputs */
    if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList),
                                      BSEC_SAMPLE_RATE_LP)) {
        checkBsecStatus(envSensor);
    }

    /* Whenever new data is available call the newDataCallback function */
    envSensor.attachCallback(newDataCallback);

    Serial.println("BSEC library version " + String(envSensor.version.major) +
                   "." + String(envSensor.version.minor) + "." +
                   String(envSensor.version.major_bugfix) + "." +
                   String(envSensor.version.minor_bugfix));

    while (1) {
        simple_menu.update(millis());
        if (millis() - start > 1000) {
            start = millis();
            simple_menu.goNext();
        }
        if (!envSensor.run()) {
            checkBsecStatus(envSensor);
        }
    }
}

void loop(void) {
}

void checkBsecStatus(Bsec2 bsec) {
    if (bsec.status < BSEC_OK) {
        Serial.println("BSEC error code : " + String(bsec.status));
    } else if (bsec.status > BSEC_OK) {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK) {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
    } else if (bsec.sensor.status > BME68X_OK) {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}
