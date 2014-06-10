/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "BLEDevice.h"

BLEDevice  ble;

DigitalOut led1(LED1);

#define NEED_CONSOLE_OUTPUT 0 /* Set this if you need debug messages on the console;
                               * it will have an impact on code-size and power
                               * consumption. */

#if NEED_CONSOLE_OUTPUT
Serial  pc(USBTX, USBRX);
#define DEBUG(...) { pc.printf(__VA_ARGS__); }
#else
#define DEBUG(...) /* nothing */
#endif /* #if NEED_CONSOLE_OUTPUT */

/* Battery Level Service */
uint8_t            batt      = 72; /* Battery level */
uint8_t            read_batt = 0;  /* Variable to hold battery level reads */
GattService        battService (GattService::UUID_BATTERY_SERVICE);
GattCharacteristic battLevel   (GattCharacteristic::UUID_BATTERY_LEVEL_CHAR,
                                1, /* initialLen */
                                1, /* maxLen */
                                GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ);

/* Heart Rate Service */
/* Service:  https://developer.bluetooth.org/gatt/services/Pages/ServiceViewer.aspx?u=org.bluetooth.service.heart_rate.xml */
/* HRM Char: https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.heart_rate_measurement.xml */
/* Location: https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.body_sensor_location.xml */
GattService        hrmService    (GattService::UUID_HEART_RATE_SERVICE);
GattCharacteristic hrmRate       (GattCharacteristic::UUID_HEART_RATE_MEASUREMENT_CHAR,
                                  2, /* initialLen */
                                  3, /* maxLen */
                                  GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);
GattCharacteristic hrmLocation   (GattCharacteristic::UUID_BODY_SENSOR_LOCATION_CHAR,
                                  1, /* initialLen */
                                  1, /* maxLen */
                                  GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ);

/* Device Information service */
static const uint8_t deviceName[] = {'m', 'b', 'e', 'd'};
GattService        deviceInformationService (GattService::UUID_DEVICE_INFORMATION_SERVICE);
GattCharacteristic deviceManufacturer (
    GattCharacteristic::UUID_MANUFACTURER_NAME_STRING_CHAR,
    sizeof(deviceName), /* initialLen */
    sizeof(deviceName), /* maxLen */
    GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ);

static const uint16_t uuid16_list[] = {
    GattService::UUID_BATTERY_SERVICE,
    GattService::UUID_DEVICE_INFORMATION_SERVICE,
    GattService::UUID_HEART_RATE_SERVICE
};

void timeoutCallback(void)
{
    DEBUG("Advertising Timeout!\n\r");
    // Restart the advertising process with a much slower interval,
    // only start advertising again after a button press, etc.
}

void connectionCallback(void)
{
    DEBUG("Connected!\n\r");
}

void disconnectionCallback(void)
{
    DEBUG("Disconnected!\n\r");
    DEBUG("Restarting the advertising process\n\r");
    ble.startAdvertising();
}

void updatesEnabledCallback(uint16_t charHandle)
{
    if (charHandle == hrmRate.getHandle()) {
        DEBUG("Heart rate notify enabled\n\r");
    }
}

void updatesDisabledCallback(uint16_t charHandle)
{
    if (charHandle == hrmRate.getHandle()) {
        DEBUG("Heart rate notify disabled\n\r");
    }
}

/**
 * Runs once a second in interrupt context triggered by the 'ticker'; updates
 * battery level and hrmCounter if there is a connection.
 */
void periodicCallback(void)
{
    led1 = !led1; /* Do blinky on LED1 while we're waiting for BLE events */

    if (ble.getGapState().connected) {
        /* Update battery level */
        batt++;
        if (batt > 100) {
            batt = 72;
        }
        ble.updateCharacteristicValue(battLevel.getHandle(), (uint8_t *)&batt, sizeof(batt));

        /* Update the HRM measurement */
        /* First byte = 8-bit values, no extra info, Second byte = uint8_t HRM value */
        /* See --> https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.heart_rate_measurement.xml */
        static uint8_t hrmCounter = 100;
        hrmCounter++;
        if (hrmCounter == 175) {
            hrmCounter = 100;
        }
        uint8_t bpm[2] = {0x00, hrmCounter};
        ble.updateCharacteristicValue(hrmRate.getHandle(), bpm, sizeof(bpm));
    }
}

int main(void)
{
    led1 = 1;
    Ticker ticker;
    ticker.attach(periodicCallback, 1);

    /* Setup the local GAP/GATT event handlers */
    ble.onTimeout(timeoutCallback);
    ble.onConnection(connectionCallback);
    ble.onDisconnection(disconnectionCallback);
    ble.onUpdatesEnabled(updatesEnabledCallback);
    ble.onUpdatesDisabled(updatesDisabledCallback);

    DEBUG("Initialising the nRF51822\n\r");
    ble.init();

    ble.accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED);
    ble.accumulateAdvertisingPayload(GapAdvertisingData::COMPLETE_LIST_16BIT_SERVICE_IDS, (uint8_t *)uuid16_list, sizeof(uuid16_list));
    ble.accumulateAdvertisingPayload(GapAdvertisingData::HEART_RATE_SENSOR_HEART_RATE_BELT);

    ble.setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.setAdvertisingInterval(160); /* 100ms; in multiples of 0.625ms. */
    ble.startAdvertising();

    /* Add the Device Information service */
    deviceInformationService.addCharacteristic(deviceManufacturer);
    ble.addService(deviceInformationService);
    ble.updateCharacteristicValue(deviceManufacturer.getHandle(), deviceName, sizeof(deviceName));

    /* Add the Battery Level service */
    battService.addCharacteristic(battLevel);
    ble.addService(battService);
    ble.updateCharacteristicValue(battLevel.getHandle(), (uint8_t *)&batt, sizeof(batt));

    /* Add the Heart Rate service */
    hrmService.addCharacteristic(hrmRate);
    hrmService.addCharacteristic(hrmLocation);
    ble.addService(hrmService);
    /* Set the heart rate monitor location (one time only) */
    /* See --> https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicViewer.aspx?u=org.bluetooth.characteristic.body_sensor_location.xml */
    uint8_t location = 0x03; /* Finger */
    ble.updateCharacteristicValue(hrmLocation.getHandle(), (uint8_t *)&location, sizeof(location));

    while (true) {
        ble.waitForEvent();
    }
}
