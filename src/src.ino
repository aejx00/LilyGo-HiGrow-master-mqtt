#include <algorithm>
#include <iostream>
#include <WiFi.h>
#include <AsyncTCP.h>           //https://github.com/me-no-dev/AsyncTCP
#include <ESPAsyncWebServer.h>  //https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPDash.h>            //https://github.com/ayushsharma82/ESP-DASH
#include <Button2.h>            //https://github.com/LennartHennigs/Button2
#include <BH1750.h>             //https://github.com/claws/BH1750
#include <DHT12.h>              //https://github.com/xreef/DHT12_sensor_library
#include <PubSubClient.h>
#include "ds18b20.h"
#include "configuration.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#ifdef __HAS_BME280__
#include <Adafruit_BME280.h>
#endif

#ifdef __HAS_SHT3X__
#include "SHT3X.h"
#endif

#ifdef __HAS_MOTOR__
#include <Adafruit_NeoPixel.h>
#endif


typedef enum {
    BME280_SENSOR_ID,
    DHTxx_SENSOR_ID,
    SHT3x_SENSOR_ID,
    BHT1750_SENSOR_ID,
    SOIL_SENSOR_ID,
    SALT_SENSOR_ID,
    DS18B20_SENSOR_ID,
    VOLTAGE_SENSOR_ID,
} sensor_id_t;

typedef struct {
    uint32_t timestamp;     /**< time is in milliseconds */
    float temperature;      /**< temperature is in degrees centigrade (Celsius) */
    float light;            /**< light in SI lux units */
    float pressure;         /**< pressure in hectopascal (hPa) */
    float humidity;         /**<  humidity in percent */
    float altitude;         /**<  altitude in m */
    float voltage;           /**< voltage in volts (V) */
    uint8_t soil;           //Percentage of soil
    uint8_t salt;           //Percentage of salt
} higrow_sensors_event_t;


WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (200)
char msg[MSG_BUFFER_SIZE];
int value = 0;
float temp_fahr = 0;
int mqtt_soil = 0;
int mqtt_salt = 0;
char mqtt_temp[10];
char mqtt_hum[10];
char mqtt_pres[10];
char mqtt_alt[10];
char mqtt_lux[10];
char mqtt_batt[10];


AsyncWebServer      server(80);
ESPDash             dashboard(&server);
BH1750              lightMeter(OB_BH1750_ADDRESS);  //0x23
DHT12               dht12(DHT12_PIN, true);
Button2             button(BOOT_PIN);
Button2             useButton(USER_BUTTON);

#ifdef __HAS_DS18B20__
DS18B20             dsSensor(DS18B20_PIN);
#endif /*__HAS_DS18B20__*/

#ifdef __HAS_SHT3X__
SHT3X               sht30;
#endif /*__HAS_SHT3X__*/

#ifdef __HAS_BME280__
Adafruit_BME280     bme;                            //0x77
#endif /*__HAS_BME280__*/

#ifdef __HAS_MOTOR__
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
#endif  /*__HAS_MOTOR__*/


bool                has_bmeSensor   = true;
bool                has_lightSensor = true;
bool                has_dhtSensor   = false;
uint64_t            timestamp       = 0;


Card *dhtTemperature    = new Card(&dashboard, TEMPERATURE_CARD, DASH_DHT_TEMPERATURE_STRING, "°C");
Card *dhtHumidity       = new Card(&dashboard, HUMIDITY_CARD, DASH_DHT_HUMIDITY_STRING, "%");
Card *illumination      = new Card(&dashboard, GENERIC_CARD, DASH_BH1750_LUX_STRING, "lx");
Card *soilValue         = new Card(&dashboard, GENERIC_CARD, DASH_SOIL_VALUE_STRING, "%");
Card *saltValue         = new Card(&dashboard, GENERIC_CARD, DASH_SALT_VALUE_STRING, "%");
Card *batteryValue      = new Card(&dashboard, GENERIC_CARD, DASH_BATTERY_STRING, "mV");

#ifdef __HAS_BME280__
Card *bmeTemperature    = new Card(&dashboard, TEMPERATURE_CARD, DASH_BME280_TEMPERATURE_STRING, "°C");
Card *bmeHumidity       = new Card(&dashboard, HUMIDITY_CARD,   DASH_BME280_HUMIDITY_STRING, "%");
Card *bmeAltitude       = new Card(&dashboard, GENERIC_CARD,    DASH_BME280_ALTITUDE_STRING, "m");
Card *bmePressure       = new Card(&dashboard, GENERIC_CARD,    DASH_BME280_PRESSURE_STRING, "hPa");
#endif  /*__HAS_BME280__*/

#ifdef __HAS_DS18B20__
Card *dsTemperature     = new Card(&dashboard, TEMPERATURE_CARD, DASH_DS18B20_STRING, "°C");
#endif  /*__HAS_DS18B20__*/

#ifdef __HAS_SHT3X__
Card *sht3xTemperature    = new Card(&dashboard, TEMPERATURE_CARD, DASH_SHT3X_TEMPERATURE_STRING, "°C");
Card *sht3xHumidity       = new Card(&dashboard, HUMIDITY_CARD, DASH_SHT3X_HUMIDITY_STRING, "%");
#endif  /*__HAS_DS18B20__*/

#ifdef __HAS_MOTOR__
Card motorButton(&dashboard, BUTTON_CARD, DASH_MOTOR_CTRL_STRING);
#endif  /*__HAS_MOTOR__*/


void deviceProbe(TwoWire &t);

void smartConfigStart(Button2 &b)
{
    Serial.println("smartConfigStart...");
    WiFi.disconnect();
    WiFi.beginSmartConfig();
    while (!WiFi.smartConfigDone()) {
        Serial.print(".");
        delay(200);
    }
    WiFi.stopSmartConfig();
    Serial.println();
    Serial.print("smartConfigStop Connected:");
    Serial.print(WiFi.SSID());
    Serial.print("PSW: ");
    Serial.println(WiFi.psk());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  //if ((char)payload[0] == '1') {
    // digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  //} else {
    // digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  //}

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    //if (client.connect(clientId.c_str())) {
    if (client.connect(device_id, mqtt_user, mqtt_pass)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void sleepHandler(Button2 &b)
{
    Serial.println("Enter Deepsleep ...");
    esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
    delay(1000);
    esp_deep_sleep_start();
}

void setupWiFi()
{
#ifdef SOFTAP_MODE
    Serial.println("Configuring access point...");
    uint8_t mac[6];
    char buff[128];
    WiFi.macAddress(mac);
    sprintf(buff, "T-Higrow-%02X:%02X", mac[4], mac[5]);
    WiFi.softAP(buff);
    Serial.printf("The hotspot has been established, please connect to the %s and output 192.168.4.1 in the browser to access the data page \n", buff);
#else
    WiFi.mode(WIFI_STA);

    Serial.print("Connect SSID:");
    Serial.print(WIFI_SSID);
    Serial.print(" Password:");
    Serial.println(WIFI_PASSWD);
    WiFi.setHostname(device_id);
    WiFi.begin(WIFI_SSID, WIFI_PASSWD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("WiFi connect fail!");
        delay(3000);
        esp_restart();
    }
    Serial.print("WiFi connect success ! , ");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
#endif
    // Adafruit_MQTT_Client mqtt(&WiFiClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY); // INIT MQTT
    server.begin();
}

bool get_higrow_sensors_event(sensor_id_t id, higrow_sensors_event_t &val)
{
    memset(&val, 0, sizeof(higrow_sensors_event_t));
    switch (id) {
#ifdef __HAS_BME280__
    case BME280_SENSOR_ID: {
        if (has_bmeSensor) {
            val.temperature = bme.readTemperature();
            if (val.temperature > 100)
            {
              val.temperature=0;
            }
            if (val.temperature < -73)
            {
              val.temperature=0;
            }
            val.humidity = bme.readHumidity();
            val.pressure = (bme.readPressure() / 100.0F);
            val.altitude = bme.readAltitude(1013.25);
        }
    }
    break;
#endif

#ifdef __HAS_SHT3X__
    case SHT3x_SENSOR_ID: {
        if (has_dhtSensor) {
            if (sht30.get()) {
                val.temperature = sht30.cTemp;
                val.humidity = sht30.humidity;
            }
        }

    }
    break;
#endif

    case DHTxx_SENSOR_ID: {
        val.temperature = dht12.readTemperature();
        val.humidity = dht12.readHumidity();
        if (isnan(val.temperature)) {
            val.temperature = 0.0;
        }
        if (isnan(val.humidity)) {
            val.humidity = 0.0;
        }
    }
    break;

    case BHT1750_SENSOR_ID: {
        if (has_lightSensor) {
            val.light = lightMeter.readLightLevel();
        } else {
            val.light = 0;
        }
    }
    break;
    case SOIL_SENSOR_ID: {
        uint16_t soil = analogRead(SOIL_PIN);
        // Serial.println(soil);
        val.soil = map(soil, dry, wet, 0, 100);
        // enforce range limits
        if (val.soil > 100)
        {
          val.soil=100;
        }
        if (val.soil < 0)
        {
          val.soil=0;
        }
        
        
    }
    break;
    case SALT_SENSOR_ID: {
        uint8_t samples = 120;
        uint32_t humi = 0;
        uint16_t array[120];
        for (int i = 0; i < samples; i++) {
            array[i] = analogRead(SALT_PIN);
            delay(2);
        }
        std::sort(array, array + samples);
        for (int i = 1; i < samples - 1; i++) {
            humi += array[i];
        }
        humi /= samples - 2;
        val.salt = humi;
    }
    break;
#ifdef __HAS_DS18B20__
    case DS18B20_SENSOR_ID: {
        val.temperature = dsSensor.temp();
        if (isnan(val.temperature) || val.temperature > 125.0) {
            val.temperature = 0;
        }
    }
#endif
    break;
    case VOLTAGE_SENSOR_ID: {
        int vref = 1100;
        uint16_t volt = analogRead(BAT_ADC);
        val.voltage = ((float)volt / 4095.0) * 6.6 * (vref);
    }
    break;
    default:
        break;
    }
    return true;
}


void setup()
{
    Serial.begin(115200);

    button.setLongClickHandler(smartConfigStart);
    useButton.setLongClickHandler(sleepHandler);

    //! Sensor power control pin , use deteced must set high
    pinMode(POWER_CTRL, OUTPUT);
    digitalWrite(POWER_CTRL, 1);
    delay(1000);

    Wire.begin(I2C_SDA, I2C_SCL);

    deviceProbe(Wire);

    dht12.begin();

    if (!lightMeter.begin()) {
        Serial.println(F("Could not find a valid BH1750 sensor, check wiring!"));
        has_lightSensor = false;
    }

#ifdef __HAS_BME280__
    if (!bme.begin()) {
        Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
        has_bmeSensor = false;
    }
#endif /*__HAS_BME280__*/

#ifdef __HAS_SHT3X__
    Wire1.begin(21, 22);
    deviceProbe(Wire1);
#endif /*__HAS_SHT3X__*/

    setupWiFi();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);

#ifdef __HAS_MOTOR__

    pixels.begin();
    pixels.setPixelColor(0, 0xFF0000);
    pixels.show();

    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, LOW);
    motorButton.attachCallback([&](bool value) {
        Serial.println("motorButton Triggered: " + String((value) ? "true" : "false"));
        digitalWrite(MOTOR_PIN, value);
        pixels.setPixelColor(0, value ? 0x00FF00 : 0);
        pixels.show();
        motorButton.update(value);
        dashboard.sendUpdates();
    });
#endif  /*__HAS_MOTOR__*/

}


void loop()
{
    button.loop();
    useButton.loop();
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    if (millis() - timestamp > 1000) {
        timestamp = millis();

        higrow_sensors_event_t val = {0};

        get_higrow_sensors_event(BHT1750_SENSOR_ID, val);
        illumination->update(val.light);
        strcpy(mqtt_lux, "");
        dtostrf(val.light, 2, 2, &mqtt_lux[strlen(mqtt_lux)]);

        get_higrow_sensors_event(SOIL_SENSOR_ID, val);
        soilValue->update(val.soil);
        mqtt_soil = val.soil;

        get_higrow_sensors_event(SALT_SENSOR_ID, val);
        saltValue->update(val.salt);
        mqtt_salt = val.salt;

        get_higrow_sensors_event(VOLTAGE_SENSOR_ID, val);
        batteryValue->update(val.voltage);
        strcpy(mqtt_batt, "");
        dtostrf(val.voltage, 4, 2, &mqtt_batt[strlen(mqtt_batt)]);

        get_higrow_sensors_event(DHTxx_SENSOR_ID, val);
        dhtTemperature->update(val.temperature);
        dhtHumidity->update(val.humidity);

#ifdef __HAS_BME280__
        get_higrow_sensors_event(BME280_SENSOR_ID, val);
        bmeTemperature->update(val.temperature);
        bmeHumidity->update(val.humidity);
        bmeAltitude->update(val.altitude);
        bmePressure->update(val.pressure);
#endif /*__HAS_BME280__*/

#ifdef __HAS_SHT3X__
        get_higrow_sensors_event(SHT3x_SENSOR_ID, val);
        sht3xTemperature->update(val.temperature);
        sht3xHumidity->update(val.humidity);
#endif  /*__HAS_SHT3X__*/


#ifdef __HAS_DS18B20__
        get_higrow_sensors_event(DS18B20_SENSOR_ID, val);
        dsTemperature->update(val.temperature);
#endif  /*__HAS_DS18B20__*/
        // str conversions for mqtt payload (note some conversions above)
        strcpy(mqtt_temp, "");
        temp_fahr = (val.temperature*1.8)+32;
        dtostrf(temp_fahr, 2, 2, &mqtt_temp[strlen(mqtt_temp)]);
        strcpy(mqtt_pres, "");
        dtostrf(val.pressure, 2, 2, &mqtt_pres[strlen(mqtt_pres)]);
        strcpy(mqtt_hum, "");
        dtostrf(val.humidity, 2, 2, &mqtt_hum[strlen(mqtt_hum)]);
        strcpy(mqtt_alt, "");
        dtostrf(val.altitude, 2, 2, &mqtt_alt[strlen(mqtt_alt)]);
        snprintf (msg, MSG_BUFFER_SIZE, "{\"sat\":\"%1d\",\n\"temp\":\"%ls\",\n\"pres\":\"%ls\",\n\"lux\":\"%s\",\n\"hum\":\"%ls\",\n\"alt\":\"%ls\",\n\"sal\":\"%ld\",\n\"batt\":\"%s\"}", mqtt_soil, mqtt_temp, mqtt_pres, mqtt_lux, mqtt_hum, mqtt_alt, mqtt_salt, mqtt_batt);
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish(device_id, msg);
        dashboard.sendUpdates();
        delay(poll_interval); // add subtle delay to loop
    }
}



void deviceProbe(TwoWire &t)
{
    uint8_t err, addr;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        t.beginTransmission(addr);
        err = t.endTransmission();
        if (err == 0) {
            Serial.print("I2C device found at address 0x");
            if (addr < 16)
                Serial.print("0");
            Serial.print(addr, HEX);
            Serial.println(" !");
            switch (addr) {
            case OB_BH1750_ADDRESS:
                has_lightSensor = true;
                break;
            case OB_BME280_ADDRESS:
                has_bmeSensor = true;
                break;
            case OB_SHT3X_ADDRESS:
                has_dhtSensor = true;
                break;
            default:
                break;
            }
            nDevices++;
        } else if (err == 4) {
            Serial.print("Unknow error at address 0x");
            if (addr < 16)
                Serial.print("0");
            Serial.println(addr, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    else
        Serial.println("done\n");
}
