
#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==================== PINS ====================

#define DHT_PIN       4
#define DHT_TYPE      DHT11
#define ACS_PIN       34
#define SERVO_PIN     18
#define LED_PIN       2

// ==================== FAULT LIMITS ====================

#define TEMP_LIMIT       40.0
#define CURRENT_LIMIT    2.0
#define ACS_SENSITIVITY  0.066

// ==================== WIFI / BACKEND ====================

const char* WIFI_SSID = "IITIAN rupees";
const char* WIFI_PASSWORD = "vitianhun";

// Update laptop IP if it changes
const char* SERVER_URL =
    "http://10.245.27.197:8000/api/data";

const char* CONTROL_URL =
    "http://10.245.27.197:8000/api/control";

// ==================== OBJECTS ====================

DHT dht(DHT_PIN, DHT_TYPE);
Servo faultServo;

// ==================== SERVO SETTINGS ====================

// 0 = OPEN, 1 = CLOSE, 2 = AUTO
volatile int controlMode = 0;
volatile int currentServoPosition = 90;

// Servo moves one degree every 25 ms.
// 90 degrees takes about 2.25 seconds.
const unsigned long SERVO_STEP_MS = 25;
const unsigned long END_PAUSE_MS = 1000;

bool autoOpening = false;
unsigned long lastServoStep = 0;
unsigned long pauseUntil = 0;

// ==================== SENSOR SETTINGS ====================

float zeroVoltage = 0.0;

unsigned long lastControlCheck = 0;
unsigned long lastSensorUpdate = 0;

const unsigned long CONTROL_INTERVAL = 500;
const unsigned long SENSOR_INTERVAL = 2000;

// ==================== ACS712 ====================

float readACSVoltage()
{
    long total = 0;
    const int samples = 100;

    for (int i = 0; i < samples; i++)
    {
        total += analogRead(ACS_PIN);
        delayMicroseconds(200);
    }

    float averageADC = total / (float)samples;
    return (averageADC / 4095.0) * 3.3;
}

void calibrateACS()
{
    Serial.println("ACS712 calibration: keep motor OFF");
    delay(3000);

    float totalVoltage = 0;

    for (int i = 0; i < 20; i++)
    {
        totalVoltage += readACSVoltage();
        delay(50);
    }

    zeroVoltage = totalVoltage / 20.0;

    Serial.print("Zero voltage: ");
    Serial.print(zeroVoltage, 3);
    Serial.println(" V");
}

float readCurrent(float &acsVoltage)
{
    acsVoltage = readACSVoltage();

    float current =
        (acsVoltage - zeroVoltage) / ACS_SENSITIVITY;

    current = abs(current);

    if (current < 0.10)
    {
        current = 0.0;
    }

    return current;
}

// ==================== SERVO TASK ====================

void servoTask(void *parameter)
{
    for (;;)
    {
        unsigned long now = millis();
        int mode = controlMode;
        int pos = currentServoPosition;

        if (mode == 0)  // OPEN
        {
            pauseUntil = 0;

            if (pos < 90 &&
                now - lastServoStep >= SERVO_STEP_MS)
            {
                lastServoStep = now;
                currentServoPosition = pos + 1;
                faultServo.write(pos + 1);
            }
        }
        else if (mode == 1)  // CLOSE
        {
            pauseUntil = 0;

            if (pos > 0 &&
                now - lastServoStep >= SERVO_STEP_MS)
            {
                lastServoStep = now;
                currentServoPosition = pos - 1;
                faultServo.write(pos - 1);
            }
        }
        else if (mode == 2)  // AUTO
        {
            // Wait at each endpoint
            if (pauseUntil != 0)
            {
                if ((long)(now - pauseUntil) < 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }

                pauseUntil = 0;
            }

            if (now - lastServoStep >= SERVO_STEP_MS)
            {
                lastServoStep = now;

                if (autoOpening)
                {
                    pos++;

                    if (pos >= 90)
                    {
                        pos = 90;
                        autoOpening = false;
                        pauseUntil = millis() + END_PAUSE_MS;
                    }
                }
                else
                {
                    pos--;

                    if (pos <= 0)
                    {
                        pos = 0;
                        autoOpening = true;
                        pauseUntil = millis() + END_PAUSE_MS;
                    }
                }

                currentServoPosition = pos;
                faultServo.write(pos);
            }
        }

        // Dedicated task keeps servo movement independent
        // of the HTTP calls made by the main loop.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ==================== READ DASHBOARD COMMAND ====================

void getServoCommand()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    HTTPClient http;
    http.setConnectTimeout(300);
    http.setTimeout(300);
    http.begin(CONTROL_URL);

    int responseCode = http.GET();

    if (responseCode == 200)
    {
        String response = http.getString();
        int newMode = controlMode;

        if (response.indexOf("\"mode\":\"OPEN\"") >= 0)
        {
            newMode = 0;
        }
        else if (response.indexOf("\"mode\":\"CLOSE\"") >= 0)
        {
            newMode = 1;
        }
        else if (response.indexOf("\"mode\":\"AUTO\"") >= 0)
        {
            newMode = 2;
        }

        if (newMode != controlMode)
        {
            if (newMode == 2)
            {
                autoOpening = (currentServoPosition < 90);
                pauseUntil = 0;
            }

            controlMode = newMode;

            Serial.print("New servo mode: ");
            Serial.println(
                newMode == 0 ? "OPEN" :
                newMode == 1 ? "CLOSE" : "AUTO"
            );
        }
    }

    http.end();
}

// ==================== SETUP ====================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("AI ASSIST FAULT DETECTION SYSTEM");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    analogReadResolution(12);
    dht.begin();

    // Attach servo and set initial OPEN position
    faultServo.setPeriodHertz(50);
    faultServo.attach(SERVO_PIN, 500, 2400);
    faultServo.write(90);

    currentServoPosition = 90;
    controlMode = 0;

    // Keep the existing Wi-Fi connection approach
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi connected");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    // Calibrate with motor OFF
    calibrateACS();

    // Separate task keeps servo motion running while
    // the main loop performs network requests.
    xTaskCreatePinnedToCore(
        servoTask,
        "ServoTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );

    Serial.println("System ready");
    Serial.println("OPEN / CLOSE / AUTO");
}

// ==================== MAIN LOOP ====================

void loop()
{
    unsigned long now = millis();

    // Check dashboard command every 500 ms
    if (now - lastControlCheck >= CONTROL_INTERVAL)
    {
        lastControlCheck = now;
        getServoCommand();
    }

    // Read sensors and update backend every 2 seconds
    if (now - lastSensorUpdate < SENSOR_INTERVAL)
    {
        delay(2);
        return;
    }

    lastSensorUpdate = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    float acsVoltage = 0.0;
    float current = readCurrent(acsVoltage);

    bool fault =
        (!isnan(temperature) && temperature >= TEMP_LIMIT) ||
        (current >= CURRENT_LIMIT);

    digitalWrite(LED_PIN, fault ? HIGH : LOW);

    Serial.println("------------------------------");

    Serial.print("Temperature: ");
    if (isnan(temperature))
        Serial.println("ERROR");
    else
        Serial.printf("%.2f C\n", temperature);

    Serial.print("Humidity: ");
    if (isnan(humidity))
        Serial.println("ERROR");
    else
        Serial.printf("%.2f %%\n", humidity);

    Serial.printf("Current: %.2f A\n", current);
    Serial.printf("ACS voltage: %.3f V\n", acsVoltage);
    Serial.printf("Status: %s\n", fault ? "FAULT" : "NORMAL");
    Serial.printf("Servo: %d degrees\n", (int)currentServoPosition);

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.setConnectTimeout(500);
        http.setTimeout(500);
        http.begin(SERVER_URL);
        http.addHeader("Content-Type", "application/json");

        float safeTemperature = isnan(temperature) ? 0.0 : temperature;
        float safeHumidity = isnan(humidity) ? 0.0 : humidity;

        String jsonData = "{";
        jsonData += "\"temperature\":" + String(safeTemperature, 2);
        jsonData += ",\"humidity\":" + String(safeHumidity, 2);
        jsonData += ",\"current\":" + String(current, 2);
        jsonData += ",\"status\":\"";
        jsonData += fault ? "FAULT" : "NORMAL";
        jsonData += "\",\"servo\":";
        jsonData += String((int)currentServoPosition);
        jsonData += "}";

        int responseCode = http.POST(jsonData);

        Serial.printf("Backend response: %d\n", responseCode);
        http.end();
    }
}
