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
#define BUZZER_PIN 8     // pin for active buzzer
#define BUZZER_TIME 100  // milliseconds for buzzer sound
#define BELOW 0          // internal use by function
#define BEETWEEN 1       // internal use by function
#define ABOVE 2          // internal use by function

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
HCSR04 hc(TRIG_PIN, ECHO_PIN);  //initialisation class HCSR04 (trig pin , echo pin)

const int samples = 20;    // number of samples to keep in temperature history
const int presence = 10;    // maximum distance in cm to consider the person present near flame.
const int threshold = 5;   // percentage of temperature variation that will detect a flame is on.
const int maxAway = 60;    // number of loops you can be away
double tHist[samples];     // temperature history array
short trend[samples];      // qq
int idx = 0;               // current history array index
double tOff = 0;           // temperature level to consider flame off. It will contain average temp when flame off is detected.
double tOn = 0;            // temperature level to consider flame on. It will contain average temp when flame on is detected.
bool flame = false;        // flame on (true) or off (false)
bool away = false;         // present or away to keep track if you are near the flame or not.
int timeAway = 0;          // count the number of loops person is away when the flame is on
const bool silent = true;  // silent mode. do not beep but flash the flame led when in alarm
bool warmDown = false;     // ss
byte averageStatus = 0;
unsigned long loop_num = 0;
double tAverage(double t[]) {
  double avg = 0;
  for (int i = 0; i < samples; i++) {
    avg = avg + t[i];
  }
  avg = avg / samples;
  return avg;
}

short pushTemp(double t[], double val) {
  short dt=0;
  t[idx] = val;
  dt=(t[idx]-t[(idx+samples/2)%samples])*100/t[(idx+samples/2)%samples];
    if (dt>threshold) {
        return 1
    } else if (dt<-1*threshold) {
        return -1
    } else {
        return 0
    }
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
    // blink the away led with '.-' (letter 'A' in morse code)
    digitalWrite(AWAY_PIN, HIGH);
    delay(200);
    digitalWrite(AWAY_PIN, LOW);
    delay(200);
    digitalWrite(AWAY_PIN, HIGH);
    delay(600);
    digitalWrite(AWAY_PIN, LOW);
    Serial.println("Error: an object block the distance sensor. distance: " + String(distance));
    distance = hc.dist();
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
}


void loop() {
  // read and save temperature history
  double tempReading = mlx.readObjectTempC();
  trend[idx]=pushTemp(tHist, tempReading);
  double avg = tAverage(tHist);
  // read distance
  double distance = hc.dist();
  double score = 0;
  for (int k = 0; k < samples; k++) {
    score+=trend[k];
  }
  // label="___"
  // if score>5:
  //     label="ACCENDI"
  // elif score<-5:
  //     label="SPEGNI"
  bool setAlarm = false;  // alarm will be decided at end of loop

  // check presence
  if (distance < presence) {
    away = false;
    timeAway = 0;
    digitalWrite(AWAY_PIN, HIGH);  // led turn on when you are near
  }
  if (distance >= presence) {
    away = true;
    digitalWrite(AWAY_PIN, LOW);
  }

  if (checkThreshold(tempReading, BELOW, avg)) {
    averageStatus = BELOW;
  }
  if (checkThreshold(tempReading, BEETWEEN, avg)) {
    averageStatus = BEETWEEN;
  }
  if (checkThreshold(tempReading, ABOVE, avg)) {
    averageStatus = ABOVE;
  }

  // check flame
  if (!flame && averageStatus == ABOVE) {
    digitalWrite(FLAME_PIN, HIGH);
    if (checkThreshold(tempReading, BEETWEEN, tOff)) {
      tOff = avg;
    }
    if (checkThreshold(tempReading, ABOVE, tOff)) {
      flame = true;
    }
    warmDown = false;
  }

  if (flame && !warmDown && averageStatus == BELOW) {
    warmDown = true;
  } else if (flame && warmDown && averageStatus == ABOVE) {
    warmDown = false;
  }
  if (flame && !warmDown && averageStatus == BEETWEEN) {
    tOn = avg * (100 - threshold) / 100;
  }

  if (away && flame) {
    ++timeAway;
    if (timeAway > maxAway) {
      setAlarm = true;
      digitalWrite(FLAME_PIN, LOW);
      if (!checkThreshold(tempReading, BELOW, tOn) && !silent) {
          digitalWrite(BUZZER_PIN, HIGH);
      }
      delay(BUZZER_TIME);
      digitalWrite(FLAME_PIN, HIGH);
      if (!silent) {
        digitalWrite(BUZZER_PIN, LOW);
      }
    }
  }

  if (flame && checkThreshold(tempReading, BEETWEEN, tOff)) {
    digitalWrite(FLAME_PIN, LOW);
    flame = false;
    away = false;
    warmDown = false;
    timeAway = 0;
    tOn = 0;
    for (int i = 0; i < samples; ++i) {
      tHist[i] = tOff;
    }
    idx = 0;
    avg = tAverage(tHist);
  }
  Serial.print(String(loop_num) + ",");
  Serial.print(String(tempReading) + ",");
  Serial.println(String(distance));
  //Serial.print("tOff:" + String(tOff) + ",");
  //Serial.print("tOn:" + String(tOn) + ",");
  //Serial.print("warmDown:"+String(20+warmDown*10)+",");
  //Serial.print("flame:"+String(20+flame*10)+",");
  //Serial.print("avgStat:"+String(20+averageStatus*5)+",");

  //Serial.print("timeAway:" + String(timeAway) + ",");
  //Serial.print("distance:" + String(distance) + ",");
  //Serial.println("avg:" + String(avg));
  if (setAlarm) {
    delay(LOOP_DELAY - BUZZER_TIME);
  } else {
    delay(LOOP_DELAY);
  }
  idx++;
  idx%=samples;
  loop_num++;
}
