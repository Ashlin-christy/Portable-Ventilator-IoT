#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL36qPgo4_4"
#define BLYNK_TEMPLATE_NAME "project"
#define BLYNK_AUTH_TOKEN "zXQZupjaDuU1GY7-kcpQzJ1dBa9Y_xmr"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_BMP280.h>
#include <MAX30105.h>
#include "spo2_algorithm.h"
#include <ESP32Servo.h>

char ssid[] = "Use";
char pass[] = "12345678";

// -------- Arduino Mega Communication --------
#define RXD2 16
#define TXD2 17

// ---------------- LCD ----------------
LiquidCrystal_I2C lcdTemp(0x27,16,2);
LiquidCrystal_I2C lcdHR(0x26,16,2);

// ---------------- Sensors ----------------
Adafruit_BMP280 bmp;
MAX30105 particleSensor;

// ---------------- Servo ----------------
Servo myServo;
int servoPin = 18;

int servoMin = 0;
int servoMax = 40;

unsigned long lastStrokeTime = 0;
bool strokeForward = true;

// ---------------- Buttons ----------------
#define BTN_UP 33
#define BTN_RIGHT 32

bool menuMode=false;
bool valueMode=false;
int menuIndex=0;

bool lastUpState=HIGH;
bool lastRightState=HIGH;

// ---------------- Menu ----------------
String menuItems[6]={
"Temperature","Pressure","Heart Rate",
"SpO2","Breath Rate","Ambu Mode"
};

// ================= MAX30102 =================
#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

byte sampleCount=0;
long irValue=0;

int hrHistory[5]={0};
byte hrIndex=0;
int filteredHR=75;

#define FINGER_THRESHOLD 50000

// ---------------- Breathing ----------------
int breathsPerMin=12;
int pumpInterval=5000;
int ambuMode=0;

// ---------------- LCD Cache ----------------
String lastTop="";
String lastBottom="";

// ---------------- Finger Detect ----------------
bool fingerDetected=false;

// =====================================================
void handleButtons()
{
  bool upState = digitalRead(BTN_UP);
  bool rightState = digitalRead(BTN_RIGHT);

  if(lastUpState==HIGH && upState==LOW)
  {
    if(menuMode && valueMode && menuIndex==5)
    {
      ambuMode++;
      if(ambuMode>2) ambuMode=0;
    }
    else if(menuMode && !valueMode)
    {
      menuIndex++;
      if(menuIndex>5) menuIndex=0;
    }
  }

  if(lastRightState==HIGH && rightState==LOW)
  {
    if(menuMode && valueMode && menuIndex==5)
      valueMode=false;
    else if(!menuMode) menuMode=true;
    else if(menuMode && !valueMode) valueMode=true;
    else
    {
      valueMode=false;
      menuMode=false;
    }
  }

  lastUpState = upState;
  lastRightState = rightState;
}

// =====================================================
void updatePump()
{
  bool ambuScreenActive = (menuMode && valueMode && menuIndex==5);

  if(menuMode && !ambuScreenActive)
  {
    myServo.write(servoMin);
    return;
  }

  bool pumpEnabled = false;

  if(!menuMode && fingerDetected)
    pumpEnabled = true;

  if(ambuScreenActive)
    pumpEnabled = true;

  if(!pumpEnabled)
  {
    myServo.write(servoMin);
    return;
  }

  if(ambuMode==0)
  {
    breathsPerMin = 12;
    servoMax = 35;
  }
  else if(ambuMode==1)
  {
    breathsPerMin = 20;
    servoMax = 55;
  }
  else
  {
    breathsPerMin = 30;
    servoMax = 70;
  }

  pumpInterval = 60000 / breathsPerMin;

  if(millis() - lastStrokeTime >= pumpInterval/2)
  {
    lastStrokeTime = millis();

    if(strokeForward)
      myServo.write(servoMax);
    else
      myServo.write(servoMin);

    strokeForward = !strokeForward;
  }
}

// =====================================================
void printLCD(String top, String bottom)
{
  if(top != lastTop)
  {
    lcdHR.setCursor(0,0);
    lcdHR.print("                ");
    lcdHR.setCursor(0,0);
    lcdHR.print(top);
    lastTop = top;
  }

  if(bottom != lastBottom)
  {
    lcdHR.setCursor(0,1);
    lcdHR.print("                ");
    lcdHR.setCursor(0,1);
    lcdHR.print(bottom);
    lastBottom = bottom;
  }
}

// =====================================================
void setup()
{
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(BTN_UP,INPUT_PULLUP);
  pinMode(BTN_RIGHT,INPUT_PULLUP);

  Wire.begin(21,22);
  Wire.setClock(400000);

  lcdTemp.init(); lcdTemp.backlight();
  lcdHR.init();   lcdHR.backlight();

  lcdTemp.setCursor(0,0);
  lcdTemp.print("Temp:");

  bmp.begin(0x76);

  if(particleSensor.begin(Wire, I2C_SPEED_FAST))
  {
    particleSensor.setup(60,4,2,400,411,4096);
  }

  myServo.attach(servoPin);
  myServo.write(servoMin);

  WiFi.begin(ssid, pass);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

// =====================================================
void loop()
{
  Blynk.run();
  handleButtons();

  float temperature=bmp.readTemperature();
  float pressure=bmp.readPressure()/100.0;

  lcdTemp.setCursor(0,1);
  lcdTemp.print("                ");
  lcdTemp.setCursor(0,1);
  lcdTemp.print(temperature);
  lcdTemp.print("C ");

  Blynk.virtualWrite(V2,temperature);

  particleSensor.check();

  while(particleSensor.available())
  {
    redBuffer[sampleCount]=particleSensor.getRed();
    irBuffer[sampleCount]=particleSensor.getIR();
    irValue = irBuffer[sampleCount];

    particleSensor.nextSample();
    sampleCount++;

    if(sampleCount>=BUFFER_SIZE)
    {
      maxim_heart_rate_and_oxygen_saturation(
        irBuffer, BUFFER_SIZE, redBuffer,
        &spo2, &validSPO2,
        &heartRate, &validHeartRate
      );

      fingerDetected = (irValue > FINGER_THRESHOLD);

      if(validHeartRate && heartRate>40 && heartRate<180)
      {
        hrHistory[hrIndex++] = heartRate;
        if(hrIndex>=5) hrIndex=0;

        int sum=0;
        for(int i=0;i<5;i++) sum+=hrHistory[i];
        filteredHR = sum/5;
      }

      sampleCount=0;
    }
  }

  if(menuMode && !valueMode)
  {
    int nextItem=menuIndex+1;
    if(nextItem>5) nextItem=0;

    printLCD(">" + menuItems[menuIndex],
             " " + menuItems[nextItem]);
    updatePump();
    return;
  }

  if(menuMode && valueMode)
  {
    String top = menuItems[menuIndex];
    String bottom="";

    switch(menuIndex)
    {
      case 0: bottom = String(temperature)+" C"; break;
      case 1: bottom = String(pressure)+" hPa"; break;
      case 2: bottom = String(filteredHR)+" BPM"; break;
      case 3: bottom = String(spo2)+" %"; break;
      case 4: bottom = String(breathsPerMin)+" BPM"; break;
      case 5:
        if(ambuMode==0) bottom="NORM 12/min";
        else if(ambuMode==1) bottom="MED 20/min";
        else bottom="EMRG 30/min";
      break;
    }

    printLCD(top, bottom);
    updatePump();
    return;
  }

  if(fingerDetected && validHeartRate && validSPO2)
  {
    String line =
      "H:"+String(filteredHR)+
      " S:"+String(spo2)+
      " B:"+String(breathsPerMin);

    printLCD("HR SpO2 BPM", line);

    Blynk.virtualWrite(V0,spo2);
    Blynk.virtualWrite(V1,filteredHR);
  }
  else
  {
    printLCD("HR SpO2 BPM", "Place Finger");
  }

  updatePump();
}

// =====================================================
// -------- BLYNK BED SELECTION (Line Follower Control)
// =====================================================

BLYNK_WRITE(V3)   // Bed 1
{
  if(param.asInt()==1)
  {
    Serial2.println(1);
    Serial.println("Bed 1 Selected");
  }
}

BLYNK_WRITE(V4)   // Bed 2
{
  if(param.asInt()==1)
  {
    Serial2.println(2);
    Serial.println("Bed 2 Selected");
  }
}

BLYNK_WRITE(V5)   // Bed 3
{
  if(param.asInt()==1)
  {
    Serial2.println(3);
    Serial.println("Bed 3 Selected");
  }
}

// -------- Emergency STOP --------
BLYNK_WRITE(V6)
{
  if(param.asInt()==1)
  {
    Serial2.println(0);   // Stop command
    Serial.println("EMERGENCY STOP");
  }
}