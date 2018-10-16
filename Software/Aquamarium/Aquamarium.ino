
/** Needed and tested libraries :
 * ArduinoJson 5.13.3
 * PubSubClient 2.6.0
 * Adafruit_NeoPixel 1.1.6
 * WifiManager 0.14.0
 */

/** TODO
 * - led du haut qui fade au lieu de round
 * - taille strip en fichier de config ?
 * - couleur marée montante / descendante
 * - version autonome sans MQTT
 * - moyen de reset config ?
 * - brightness auto avec capteur lumière
 * - webserver une fois démarré (config/reset/current values)
 */

#include <FS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <Ticker.h>
/* you HAVE to modify PubSubClient.h to update MQTT_MAX_PACKET_SIZE to 512 :
 * #define MQTT_MAX_PACKET_SIZE 512
 * This cannot be done here because of Arduino IDE
 */
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

/** exemple mqtt/json
 *  mosquitto_sub -v -h mqtt.example.fr -t tides/trebeurden/json
tides/trebeurden/json
 * {"location":"trebeurden",
 * "high":{"timestamp":"2018-10-08 18:38 +0200","level":9.6,"coefficient":100},
 * "current":{"timestamp":"08/10/18 20:45 +0200","level":7.76,"clock":59},
 * "low":{"timestamp":"2018-10-09 01:03 +0200","level":0.97,"coefficient":100}
 * }
 */

char mqtt_server[40]; ///< domain name of the MQTT server. Stored in config
char mqtt_topic[40]; ///< topic, will be created from mqtt_city
char mqtt_city[29]; ///< city of the tide, Stored in config
// const char* mqtt_topic = "tides/+/json";
uint16_t globalBrightness = 64; ///< Global brightness of the strip
char globalBrightness_str[4] = "64";

#define TRIGGER_PIN D0 /**< reset parameters button */

#define STRIP_LENGHT 16  /**< length of the pixels strips */
#define MARNAGE 11.0 /**< max hight tide. Used to compute increment for each pixel */
#define H_LED (MARNAGE/STRIP_LENGHT) /**< height of a led */
#define H_TO_LED(h) (ceil(h/H_LED))
#define STRIP_PIN D6 /**< pin to connect the pixels strips */
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(STRIP_LENGHT, STRIP_PIN, NEO_GRB + NEO_KHZ800);

// define some colors for easy use
#define RED     ((uint32_t)0xFF0000)
#define GREEN   ((uint32_t)0x00FF00)
#define BLUE    ((uint32_t)0x0000FF)
#define YELLOW  ((uint32_t)0xFFFF00)
#define FUCHSIA ((uint32_t)0xFF00FF)
#define AQUA    ((uint32_t)0x00FFFF)

WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wifiManager;
bool shouldSaveConfig = false; /**< flag to save custom configuration */
Ticker startTick; /** Ticker for light blink during start/config */

/** setup hartdware & software
 */
void setup (void) {
	Serial.begin(115200);
	Serial.printf("\n\nReset reason: %s\n", ESP.getResetReason().c_str());

	pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
	digitalWrite(BUILTIN_LED, HIGH);

	pinMode(TRIGGER_PIN, INPUT_PULLUP);

	pixels.begin(); // This initializes the NeoPixel library.
	startTick.attach_ms (300, moveLed, BLUE);

	//clean FS, for testing
	//SPIFFS.format();
	// get config from FS
	Serial.println("mounting FS...");
	if (SPIFFS.begin()) {
		Serial.println("mounted file system");
		if (SPIFFS.exists("/config.json")) {
			//file exists, reading and loading
			Serial.println("reading config file");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				Serial.println("opened config file");
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
				json.printTo(Serial);
				if (json.success()) {
					Serial.println("\nparsed json");

					strncpy(mqtt_server, json["mqtt_server"], 40);
					strncpy(mqtt_city, json["mqtt_city"], 29);
					globalBrightness = json["brightness"];
					snprintf (globalBrightness_str, 4, "%d", globalBrightness);
				} else {
					Serial.println ("failed to parse json");
				}
			} else {
				Serial.println("failed to load json config");
			}
			configFile.close();
		}
	} else {
		Serial.println("failed to mount FS");
	}
	pixels.setBrightness (globalBrightness);

	// WIFI setup
	startTick.attach_ms (200, moveLed, BLUE);

	//reset settings - for testing
	// wifiManager.resetSettings();

	// wifiManager.setDebugOutput(false); // disable debug
	// add parameters
	// id/name, placeholder/prompt, default, length
	WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
	WiFiManagerParameter custom_mqtt_city("city", "mqtt city", mqtt_city, 29);
	WiFiManagerParameter custom_brightness("brightness", "brightness", globalBrightness_str, 3, "type='range' max='255' min='0' step='5'");
	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_mqtt_city);
	wifiManager.addParameter(&custom_brightness);
	// set saving config callaback
	wifiManager.setSaveConfigCallback(saveConfigCallback);
	wifiManager.setAPCallback(configModeCallback);
	WiFi.hostname("Aquamarium");
	if ( ! wifiManager.autoConnect("Aquamarium") ) {
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//read updated parameters
	startTick.attach_ms (300, moveLed, BLUE);
	strncpy(mqtt_server, custom_mqtt_server.getValue(), 40);
	strncpy(mqtt_city,  custom_mqtt_city.getValue(),  29);
	globalBrightness = atoi(custom_brightness.getValue());
	//save the custom parameters to FS
	if (shouldSaveConfig) {
		Serial.println("saving config");
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();
		json["mqtt_server"] = mqtt_server;
		json["mqtt_city"] = mqtt_city;
		json["brightness"] = globalBrightness;

		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile) {
			Serial.println("failed to open config file for writing");
		}

		json.printTo(Serial);
		json.printTo(configFile);
		configFile.close();
		//end save
	}

	pixels.setBrightness (globalBrightness);

	// Start MQTT client
	snprintf (mqtt_topic,  40, "tides/%s/json", mqtt_city);
	client.setServer(mqtt_server, 1883);
	client.setCallback(mqttSubCallback);

	startTick.detach();
}

/** main loop
 */
void loop (void) {
	if (!client.connected()) {
		reconnect();
	}
	yield(); // pass to other tasks
	client.loop();
	handleButton();
}

/** Callback WifiManager
 * device enters configuration mode on failed WiFi connection attempt
 */
void configModeCallback (WiFiManager *myWiFiManager) {
	startTick.attach_ms(200, moveLed, RED);

	Serial.println("Entered config mode");
	Serial.println(WiFi.softAPIP());
	Serial.println(myWiFiManager->getConfigPortalSSID());
}

/** Callback WifiManager
 * notifying us of the need to save config
 */
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

/** callback for MQTT subscriber
 */
void mqttSubCallback(char* topic, byte* payload, unsigned int length) {
	// Serial.print("Message arrived [");
	// Serial.print(topic);
	// Serial.print("] ");
	// for (int i = 0; i < length; i++) {
	// 	Serial.print((char)payload[i]);
	// }
	// Serial.println();
	showTide ((const char*)payload);
}

/** move/blink strip
 */
void moveLed (uint32_t color) {
	static uint16_t led = 0;
	static int8_t inc = 1;

	pixels.clear();
	pixels.setPixelColor (led, color);
	pixels.show();
	led += inc;
	if (led >= STRIP_LENGHT-1) {
		inc = -1;
	} else if (led == 0) {
		inc = 1;
	}
}

/** handle reset settings button
 * note: use quite crappy debounce
 */
void handleButton (void) {
	int debounce = 50;
	if ( digitalRead(TRIGGER_PIN) == LOW ) {
		delay(debounce);
		if(digitalRead(TRIGGER_PIN) == LOW ) {
			Serial.println ("reset parameters ...");
			wifiManager.resetSettings();
			delay(1000);
			ESP.reset();
		}
	}
}

#define BRIGHT(c,b)  (b*c/255)
/** update color with brightness
 * @param color the color 0xRRGGBB
 * @param bright the new brightness 0-255
 * @return the color updated for brightness 0xrrggbb
 */
uint32_t getPixelColorBrightness (uint32_t color, uint16_t bright) {
	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >>  8) & 0xFF;
	uint8_t b = (color      ) & 0xFF;

	return BRIGHT(r, bright)<<16 
	     | BRIGHT(g, bright)<<8 
	     | BRIGHT(b, bright);
}

/** (re)connect to MQTT broker
 */
void reconnect (void) {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.printf("Attempting MQTT connection to %s ...\n", mqtt_server);
    // Attempt to connect
    if (client.connect("AquamariumV2")) {
      Serial.println("connected");
      // ... and resubscribe
      if (client.subscribe(mqtt_topic) != true) {
			Serial.println ("Subscribe failed");
		}
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/** get current tide level from json
 * @param json the received json object
 * @return the tide level
 */
float getCurrentLevel (const char* json) {
	StaticJsonBuffer<512> jsonBuffer;

	JsonObject& root = jsonBuffer.parseObject(json);

	if (root == JsonObject::invalid()) {
		Serial.println ("Parse json failed!");
		return 0.0;
	}

	const char* location = root["location"]; // "trebeurden"

	JsonObject& current = root["current"];
	const char* current_timestamp = current["timestamp"]; // "08/10/18 20:45 +0200"
	return (float)current["level"]; // 7.76
}

/** get leds to light from tide level
 * @param[in] current_level the tide level to convert
 * @param[out] fullLeds number of full led to light up
 * @param[out] lastLed brightness of thze not full led (0-255)
 */
void getLeds (float current_level, uint16_t* fullLeds, uint16_t* lastLed) {
	float l = 0.0; ///< temp value, height converted to leds
	float levelLed = 0.0; ///< decimal part of height in leds
	uint16_t levelLed_b = 0; ///< brightness of top led

	l = current_level/H_LED;
	*fullLeds = floor(l); // nb of full leds
	levelLed = (l - *fullLeds) * H_LED;
	*lastLed = map ((long)(levelLed*100), 0, (long)(H_LED*100), 0, 255);
	// Serial.printf ("%f:%f->%d/%d (%f)\n", current_level, l, *fullLeds, *lastLed, levelLed );
}
/** display current tide level
 * @param json mqtt topic message in json format
 */
void showTide (const char* json) {
	float level = 0.0;
	uint16_t nbLeds = 0;
	uint16_t levelLed = 0;

	level = getCurrentLevel (json);
	getLeds (level, &nbLeds, &levelLed);

	// light up pixels
	pixels.clear();
	for (uint16_t i=0; i<nbLeds; i++) {
		pixels.setPixelColor (i, BLUE);
	}
	pixels.setPixelColor (nbLeds, getPixelColorBrightness (BLUE, levelLed));
	pixels.show();
}
