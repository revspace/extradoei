#include <TM1637TinyDisplay.h>
#include <OneWire.h>
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <MQTT.h>
#include <WiFi.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

TM1637TinyDisplay display(25, 32);
MQTTClient      mqtt;

const int pin_red = 18;
const int pin_green = 26;
const int pin_1wire = 23;
const int max_failures = 5;

OneWire ds(pin_1wire);
byte id[8];
String topic_template;
String result_topic;
String n_topic;
int n = -1;

unsigned long ignore_start = -1;
unsigned long ignore_time = 0;

void handle_mqtt_message (String& topic, String& message) {

    Serial.println("MQTT: ");
    Serial.println(topic);
    Serial.println(message);
    if (topic == result_topic) {
        ignore_start = millis();
        ignore_time = 5000;

        digitalWrite(pin_red, LOW);
        digitalWrite(pin_green, LOW);
        if (message == "bad") {
            digitalWrite(pin_red, HIGH);
            display.showString("bad ibutton");
        } else if (message == "ok") {
            digitalWrite(pin_green, HIGH);
            display.showString("dOEI");
        } else {
            Serial.println("bad value");
            return;
        }
    } else if (topic == n_topic) {
        n = message.toInt();
    } else {
        Serial.println("bad topic");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Hello world.");
    SPIFFS.begin(true);

    pinMode(pin_red, OUTPUT);
    pinMode(pin_green, OUTPUT);

    int do_portal = -1;
    if (digitalRead(pin_1wire) == LOW) do_portal++;
    display.setBrightness(BRIGHT_7);
    display.setScrolldelay(350);
    display.showString("reset");
    if (digitalRead(pin_1wire) == LOW) do_portal++;

    WiFiSettings.hostname = "extradoei-";
    String server  = WiFiSettings.string("mqtt_server", 64, "test.mosquitto.org");
    int port       = WiFiSettings.integer("mqtt_port", 0, 65535, 1883);
    n_topic        = WiFiSettings.string("mqtt_n_topic", 64, "revspace/doorduino/checked-in");
    topic_template = WiFiSettings.string("mqtt_topic", 64, "revspace-local/doorduino/extradoei/{me}/{hook}");

    if (do_portal > 0) {
        display.showString("_AP_");
        WiFiSettings.portal();
    }

    topic_template.replace("{me}", Sprintf("%06" PRIx64, ESP.getEfuseMac() >> 24));

    result_topic = topic_template;
    result_topic.replace("{hook}", "result");

    display.showString("Conn");
    if (!WiFiSettings.connect(false, 60)) {
        display.showString("cannot connect to wifi - restarting");
        ESP.restart();
    }

    static WiFiClient wificlient;
    mqtt.begin(server.c_str(), port, wificlient);
    mqtt.onMessage(handle_mqtt_message);
}

void connect_mqtt() {
    if (mqtt.connected()) return;  // already/still connected

    display.showString("conn");
    static int failures = 0;
    if (mqtt.connect(WiFiSettings.hostname.c_str())) {
        failures = 0;
    } else {
        failures++;
        display.showString("FAIL");
        delay(1000);
        if (failures >= max_failures) ESP.restart();
    }

    mqtt.subscribe(result_topic);
    mqtt.subscribe(n_topic);
}

bool handle_ui() {
    static bool ibutton_connected = false;

    // recent connection (debounce) or during green/red result display
    if (ignore_start > 0 && millis() - ignore_start < ignore_time) return false;

    // same ibutton still connectod
    if (ibutton_connected && ds.reset()) return false;

    String str = n < 0 ? "----" : n >= 100 ? Sprintf("%d", n) : Sprintf("n=%2d", n);
    display.showString(str.c_str());

    unsigned int m = millis() % 3000;
    bool yellow = !((m > 2600 && m <= 2700) || ( m >2900));
    digitalWrite(pin_green, yellow);
    digitalWrite(pin_red,   yellow);

    ibutton_connected = false;
    ds.reset_search();
    if (!ds.search(id)) return false;

    if (OneWire::crc8(id, 7) != id[7]) return false;
    ibutton_connected = true;
    String hex;
    for (int i = 0; i < 8; i++) hex += Sprintf("%02x", id[i]);
    Serial.println(hex);

    String topic = topic_template;
    topic.replace("{hook}", "unlocked");
    mqtt.publish(topic, hex);

    digitalWrite(pin_green, LOW);
    digitalWrite(pin_red,   LOW);

    ignore_start = millis();
    ignore_time = 1000;

    return true;
}

void loop() {
    connect_mqtt();
    handle_ui();

    mqtt.loop();
    delay(0);
}
