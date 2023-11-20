// Check fornello
// Written by gianluca@gurutech.it, GPLv3 or later

// REQUIRES the following Arduino libraries:
// - Adafruit MLX90614 Library: https://github.com/adafruit/Adafruit-MLX90614-Library
// - gamegine HCSR04 ultrasonic sensor: https://github.com/gamegine/HCSR04-ultrasonic-sensor-lib

#include <Adafruit_MLX90614.h>
#include <HCSR04.h>
#include <math.h>

#define FLAME_PIN 13     // pin for the flame led
#define AWAY_PIN 10      // pin for the away led
#define TRIG_PIN 12      // pins for HCSR04 sensor
#define ECHO_PIN 11      // pins for HCSR04 sensor
#define BUZZER_PIN 8     // pin for active buzzer
#define BUZZER_TIME 100  // milliseconds for buzzer sound

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
HCSR04 hc(TRIG_PIN, ECHO_PIN);  //initialisation class HCSR04 (trig pin , echo pin)

const byte samples = 30;    // number of samples to keep in temperature history
uint32_t interval = 1000;  // milliseconds between loops
const int presence = 10;    // maximum distance in cm to consider the person present near flame.
const int threshold = 5;   // percentage of temperature variation that will detect a flame is on.
const int maxAway = 60;    // number of loops you can be away
double tHist[samples];     // temperature history array
short trend[samples];      // qq
unsigned short idx = 0;    // current history array index
bool flame = false;        // flame on (true) or off (false)
bool away = false;         // present or away to keep track if you are near the flame or not.
int timeAway = 0;          // count the number of loops person is away when the flame is on
const bool silent = true;  // silent mode. do not beep but flash the flame led when in alarm
unsigned long loop_num = 0;
double tAvg = 0;
const double xavg = (samples-1.0)/2;
const double xvar = (samples-1.0)*(2*(samples-1.0)+1.0)/6-(xavg*xavg);
short pushTemp(double t[], double val) {
  short dt=0;
  t[idx] = val;
  dt=(t[idx]-t[(idx+samples/2)%samples])*100/t[(idx+samples/2)%samples];
    if (dt>threshold) {
        return 1;
    } else if (dt<-1*threshold) {
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
    trend[i] = 0;
    tAvg += tHist[i];
    digitalWrite(FLAME_PIN, LOW);
    delay(100);
  }
  tAvg/=samples;

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
  static uint32_t nextTime = millis();
  static double covar;
  static double yvar;
  static double correl;
  static double distance;
  static double tempReading;

  char msg[16] = "NONE";
  if (millis() - nextTime >= interval) {
    nextTime += interval;
    // read and save temperature history
    tempReading = mlx.readObjectTempC();
    tAvg=(tAvg*samples-tHist[idx]+tempReading)/samples;
    trend[idx]=pushTemp(tHist, tempReading);
    // read distance
    distance = hc.dist();
    covar = 0;
    for (int i = 1; i <= samples; i++) {
      covar+=(i-1)*tHist[(idx+i)%samples];
    }
    covar=covar/samples-xavg*tAvg;
    double linest = covar/xvar;
    yvar = 0;
    for (int i = 0; i< samples; i++) {
      yvar+=tHist[i]*tHist[i];
    }
    yvar = (yvar/samples)-tAvg*tAvg;

    double correl = covar / (sqrt(xvar)*sqrt(yvar));
    double score = 0;
    for (int k = 0; k < samples; k++) {
      score+=trend[k];
    }
    if (score>2) {
      flame = true;
      digitalWrite(FLAME_PIN, HIGH);
      strcpy(msg,"SCORE_HIGH");
    } else if (score<-2) {
      flame = false;
      strcpy(msg,"SCORE_LOW");
      digitalWrite(FLAME_PIN, LOW);
    }

    if (abs(correl)>0.7) {
      if (linest >= 0.05) {
        flame = true;
        digitalWrite(FLAME_PIN, HIGH);
        strcpy(msg,"SLOPE_HIGH");
      } else if (linest <= -0.05) {
        flame = false;
        digitalWrite(FLAME_PIN, LOW);
        strcpy(msg,"SLOPE_LOW");
      }
    }
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

    if (away && flame) {
      ++timeAway;
      if (timeAway > maxAway) {
        setAlarm = true;
        digitalWrite(FLAME_PIN, LOW);
        if (!silent) {
            digitalWrite(BUZZER_PIN, HIGH);
        }
        delay(BUZZER_TIME);
        digitalWrite(FLAME_PIN, HIGH);
        if (!silent) {
          digitalWrite(BUZZER_PIN, LOW);
        }
      }
    }

    Serial.print("temp:"+String(tHist[idx]) + ",");
    Serial.print("trend:"+String(trend[idx])+",");
    Serial.print("score:"+String(score)+",");
    Serial.print("tAvg:"+String(tAvg)+",");
    Serial.print("xavg:"+String(xavg)+",");
    Serial.print("xvar:"+String(xvar)+",");
    Serial.print("yvar:"+String(yvar)+",");
    Serial.print("linest:"+String(linest)+",");
    Serial.print("correl:"+String(correl)+",");
    Serial.print("msg:"+String(msg)+",");
    Serial.println("");
    // if (setAlarm) {
    //   delay(LOOP_DELAY - BUZZER_TIME);
    // } else {
    //   delay(LOOP_DELAY);
    // }
    idx++;
    idx%=samples;
    loop_num++;
  }
}
