// Check fornello
// Written by gianluca@gurutech.it, GPLv3 or later

// REQUIRES the following Arduino libraries:
// - Adafruit MLX90614 Library: https://github.com/adafruit/Adafruit-MLX90614-Library
// - gamegine HCSR04 ultrasonic sensor: https://github.com/gamegine/HCSR04-ultrasonic-sensor-lib

#include <Adafruit_MLX90614.h>
#include <HCSR04.h>
#include <math.h>

// Hardware configuration parameters
#define FLAME_PIN 13     // pin for the flame led
#define AWAY_PIN 10      // pin for the away led
#define TRIG_PIN 12      // pins for HCSR04 sensor
#define ECHO_PIN 11      // pins for HCSR04 sensor
#define BUZZER_PIN 8     // pin for active buzzer
#define BUZZER_TIME 100  // milliseconds for buzzer sound

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
HCSR04 hc(TRIG_PIN, ECHO_PIN);  //initialisation class HCSR04 (trig pin , echo pin)

// Software configuration parameters
const byte samples = 30;         // number of samples to keep in temperature history
const uint32_t interval = 1000;  // milliseconds between loops
const byte presence = 10;        // maximum distance in cm to consider the person present near flame.
const byte threshold = 5;        // percentage of temperature variation to detect flame on/off status change
const byte approval = 3;         // number of samples out of percentange range to confirm a flame status change
const double slope = 0.03;       // linear regression slope in °C/s to detect flame on/off status change
const double alignment = 0.7;    // linear regression correlation to consider the samples aligned
const short maxAway = 60;        // number of loops you can be away when the flame is off
const bool silent = true;       // silent mode. do not beep but flash the flame led when in alarm

// Global variables and constants
double tHist[samples];   // temperature history array
short trend[samples];    // qq
unsigned short idx = 0;  // current history array index
double tAvg = 0;
const double xavg = (samples - 1.0) / 2;
const double xvar = (samples - 1.0) * (2 * (samples - 1.0) + 1.0) / 6 - (xavg * xavg);

short pushTemp(double t[], double val) {
  short dt = 0;
  t[idx] = val;
  dt = (t[idx] - t[(idx + samples / 2) % samples]) * 100 / t[(idx + samples / 2) % samples];
  if (dt > threshold) {
    return 1;
  } else if (dt < -1 * threshold) {
    return -1;
  } else {
    return 0;
  }
}

void setup() {
  pinMode(FLAME_PIN, OUTPUT);
  pinMode(AWAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(9600);
  while (!Serial);
  Serial.println("BEGIN SETUP");
  if (!mlx.begin()) {
    Serial.println("Error connecting to MLX sensor. Check wiring.");
    while (1);
  };
  // load initial temperature history
  for (int i = 0; i < samples; ++i) {
    digitalWrite(FLAME_PIN, HIGH);
    delay(100);
    tHist[i] = mlx.readObjectTempC();
    trend[i] = 0;
    tAvg += tHist[i];
    digitalWrite(FLAME_PIN, LOW);
    delay(100);
  }
  tAvg /= samples;

  // check that no object are placed in front of the distance sensor at startup
  while (hc.dist() < presence) {
    // blink the away led with '.-' (letter 'A' in morse code)
    digitalWrite(AWAY_PIN, HIGH);
    delay(200);
    digitalWrite(AWAY_PIN, LOW);
    delay(200);
    digitalWrite(AWAY_PIN, HIGH);
    delay(600);
    digitalWrite(AWAY_PIN, LOW);
    Serial.println("Error: an object block the distance sensor.");
    delay(400);
  }
  digitalWrite(AWAY_PIN, LOW);

  // setup is ok: emit an ok sound or flash '---' (letter 'O') with flame led
  if (silent) {
    delay(400);
    for (int m = 0; m < 3; m++) {
      digitalWrite(FLAME_PIN, HIGH);
      delay(600);
      digitalWrite(FLAME_PIN, LOW);
      delay(200);
    }
    delay(400);
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(BUZZER_TIME);
    digitalWrite(BUZZER_PIN, LOW);
  }
  // turn off flame led - flame MUST be off at startup
  digitalWrite(FLAME_PIN, LOW);
  Serial.println("END SETUP");
  Serial.println("READY");
}


void loop() {
  static uint32_t nextTime = millis();
  static uint32_t envTime = millis();
  static uint32_t timestamp;
  static double covar;
  static double yvar;
  static double correl;
  static double distance;
  static double tempReading;
  static double linest;
  static short score;
  static bool flame = false;           // flame on (true) or off (false)
  static bool away = false;            // present or away to keep track if you are near the flame or not
  static unsigned short timeAway = 0;  // count the number of loops person is away since the flame is on
  static double tEnv = tAvg;           // environment temperature to consider the flame off

  char msg[16] = "NONE";
  timestamp = millis();
  if (timestamp - nextTime >= interval) {
    nextTime += interval;
    // read and save temperature history
    tempReading = mlx.readObjectTempC();
    tAvg = (tAvg * samples - tHist[idx] + tempReading) / samples;
    trend[idx] = pushTemp(tHist, tempReading);
    // read distance
    distance = hc.dist();

    // calculate scoring, linear regression estimation and correlation for current temperature history
    covar = 0;
    yvar = 0;
    score = 0;
    for (int i = 0; i < samples; i++) {
      covar += i * tHist[(idx + i + 1) % samples];
      yvar += tHist[i] * tHist[i];
      score += trend[i];
    }
    covar = covar / samples - xavg * tAvg;
    linest = covar / xvar;
    yvar = (yvar / samples) - tAvg * tAvg;
    correl = covar / (sqrt(xvar) * sqrt(yvar));

    // emit a verdict on flame on/off
    if ((abs((tempReading-tEnv)*100/tEnv)<threshold)) {
      flame = false;
      timeAway = 0;
      strcpy(msg, "FLAME_OFF");
      digitalWrite(FLAME_PIN, LOW);
    } else {
        if (score <= -1* approval || (correl < -1*alignment && linest <= -1*slope)) {
        flame = false;
        timeAway = 0;
        strcpy(msg, "FLAME_OFF");
        digitalWrite(FLAME_PIN, LOW);
        } else if (score >= approval || (correl > alignment && linest >= slope)) {
          flame = true;
          strcpy(msg, "FLAME_ON");
          digitalWrite(FLAME_PIN, HIGH);
        }
    }


    // emit a verdict on away true/false
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
      if (timeAway > maxAway) {
        digitalWrite(FLAME_PIN, LOW);
        if (!silent) {
          digitalWrite(BUZZER_PIN, HIGH);
        }
        delay(BUZZER_TIME);
        digitalWrite(FLAME_PIN, HIGH);
        if (!silent) {
          digitalWrite(BUZZER_PIN, LOW);
        }
      } else {
          digitalWrite(AWAY_PIN, HIGH);
          delay(BUZZER_TIME);
          digitalWrite(AWAY_PIN, LOW);
      }
    }
    Serial.print("time:"+String(timestamp)+",");
    Serial.print("temp:" + String(tHist[idx]) + ",");
    Serial.print("flame:" + String(int(flame)) + ",");
    Serial.print("away:" + String(int(away)) + ",");
    //Serial.print("trend:" + String(trend[idx]) + ",");
    //Serial.print("score:" + String(score) + ",");
    Serial.print("tAvg:" + String(tAvg) + ",");
    Serial.print("tEnv:" + String(tEnv) + ",");
    //Serial.print("xavg:" + String(xavg) + ",");
    //Serial.print("xvar:" + String(xvar) + ",");
    //Serial.print("yvar:" + String(yvar) + ",");
    //Serial.print("linest:" + String(linest) + ",");
    //Serial.print("correl:" + String(correl) + ",");
    //Serial.print("msg:" + String(msg) + ",");
    Serial.println("");
    idx++;
    idx %= samples;
  }
  if (timestamp - envTime >= interval*maxAway) {
    envTime += interval*maxAway;
    if ( abs((tAvg-tEnv)*100/tEnv)<threshold && score == 0 && abs(linest)< slope) {
        tEnv = tAvg;
    }
  }
}
