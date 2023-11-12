// Check fornello
// Written by gianluca@gurutech.it, GPLv3 or later

// REQUIRES the following Arduino libraries:
// - Adafruit MLX90614 Library: https://github.com/adafruit/Adafruit-MLX90614-Library
// - gamegine HCSR04 ultrasonic sensor: https://github.com/gamegine/HCSR04-ultrasonic-sensor-lib

#include <Adafruit_MLX90614.h>
#include <HCSR04.h>

#define LOOP_DELAY 500   // milliseconds between loops
#define FLAME_PIN 13     // pin for the flame led
#define AWAY_PIN 10      // pin for the away led
#define TRIG_PIN 12      // pins for HCSR04 sensor
#define ECHO_PIN 11      // pins for HCSR04 sensor
#define BUZZER_PIN 8         // pin for active buzzer
#define BUZZER_TIME 100  // milliseconds for buzzer sound
#define ABOVE 0          // internal use by function
#define BELOW 1          // internal use by function
#define BEETWEEN 2       // internal use by function

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
HCSR04 hc(TRIG_PIN, ECHO_PIN);  //initialisation class HCSR04 (trig pin , echo pin)

const int samples = 20;  // number of samples to keep in temperature history
const int presence = 10;  // maximum distance in cm to consider the person present near flame.
const int threshold = 5;  // percentage of temperature variation that will detect a flame is on.
const int maxAway = 10;   // number of loops you can be away
double tHist[samples];    // temperature history array
int idx = 0;              // current history array index
double tOff = 0;          // temperature level to consider flame off. It will contain average temp when flame off is detected.
double tOn = 100;         // temperature level to consider flame on. It will contain average temp when flame on is detected.
bool flame = false;       // flame on (true) or off (false)
bool away = false;        // present or away to keep track if you are near the flame or not.
int timeAway = 0;         // count the number of loops person is away

double tAverage(double t[]) {
  double avg = 0;
  for (int i = 0; i < samples; i++) {
    avg = avg + t[i];
  }
  avg = avg / samples;
  return avg;
}

void pushTemp(double t[], double val) {
  t[idx] = val;
  idx++;
  idx %= samples;
}

bool checkThreshold(double t, int comparison, double val) {
  if (comparison == ABOVE) {
    return (100 * (t - val) / val) > threshold;
  }
  if (comparison == BELOW) {
    return (100 * (t - val) / val) < -1 * threshold;
  }
  if (comparison == BEETWEEN) {
    return abs((100 * (t - val) / val)) < threshold;
  }
}

void setup() {
  pinMode(FLAME_PIN, OUTPUT);
  pinMode(AWAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(9600);
  while (!Serial)
    ;
  if (!mlx.begin()) {
    Serial.println("Error connecting to MLX sensor. Check wiring.");
    while (1)
      ;
  };

  // init temperature history
  for (int i = 0; i < samples; ++i) {
    digitalWrite(FLAME_PIN, HIGH);
    delay(100);
    tHist[i] = mlx.readObjectTempC();
    digitalWrite(FLAME_PIN, LOW);
    delay(100);
  }
  tOff = tAverage(tHist);

  // check that no object are placed in front of the distance sensor at startup
  double distance = hc.dist();
  while (distance < presence) {
    // .- .-- AW in morse code
    digitalWrite(AWAY_PIN, HIGH);
    delay(100);
    digitalWrite(AWAY_PIN, LOW);
    delay(100);
    digitalWrite(AWAY_PIN, HIGH);
    delay(300);
    digitalWrite(AWAY_PIN, LOW);
    Serial.println("Error: an object block the distance sensor. distance: " + String(distance));
    distance = hc.dist();
    delay(200);
  }

  // turn off flame led - flame MUST be off at startup
  digitalWrite(FLAME_PIN, LOW);
  digitalWrite(AWAY_PIN, LOW);
  // everything is setup emit a startup beep for buzzer test
  digitalWrite(BUZZER_PIN, HIGH);
  delay(BUZZER_TIME);
  digitalWrite(BUZZER_PIN, LOW);
}


void loop() {
  // put your main code here, to run repeatedly:
  double tempReading = mlx.readObjectTempC();
  double distance = hc.dist();
  bool setAlarm = false;
  if (distance < presence) {
    away = false;
    timeAway = 0;
    digitalWrite(AWAY_PIN, HIGH);  // led turn on when you are near
  }
  if (distance >= presence) {
    away = true;
    digitalWrite(AWAY_PIN, LOW);
  }

  if (away && flame) {
    ++timeAway;
  }
  if (flame && away && timeAway > maxAway) {
    setAlarm = true;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(BUZZER_TIME);
    digitalWrite(BUZZER_PIN, LOW);
  }

  pushTemp(tHist, tempReading);
  //printTemp(tHist);
  double avg = tAverage(tHist);
  if (!flame && checkThreshold(tempReading, ABOVE, avg)) {
    digitalWrite(FLAME_PIN, HIGH);
    tOff = avg;
    flame = true;
  }

  if (flame && checkThreshold(tempReading, BEETWEEN, avg)) {
    tOn = avg;
  }
  if (flame && checkThreshold(tempReading, BEETWEEN, tOff)) {
    digitalWrite(FLAME_PIN, LOW);
    flame = false;
    away = false;
    timeAway = 0;
    tOn=100;
    for (int i = 0; i < samples; ++i) {
      tHist[i] = tOff;
    }
    idx = 0;
    avg = tAverage(tHist);
  }
  Serial.print("temp:" + String(tempReading) + ",");
  Serial.print("tOff:" + String(tOff) + ",");
  Serial.print("tOn:" + String(tOn) + ",");
//  Serial.print("away:" + String(away) + ",");
//  Serial.print("timeAway:" + String(timeAway) + ",");
//  Serial.print("setAlarm:" + String(setAlarm) + ",");
//  Serial.print("distance:" + String(distance) + ",");
  Serial.println("avg:" + String(avg));
  if (setAlarm) {
    delay(LOOP_DELAY - BUZZER_TIME);
  } else {
    delay(LOOP_DELAY);
  }
}