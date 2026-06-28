// -------- IR Sensors --------
#define IR1 22   // Left
#define IR2 24   // Middle
#define IR3 26   // Right

// -------- Motor Driver --------
#define IN1 8
#define IN2 9
#define IN3 10
#define IN4 11
#define ENA 5
#define ENB 6

int targetBed = 0;
int bedCounter = 0;

bool startMove = false;
bool markerDetected = false;

// Speed correction (adjust if robot drifts)
int leftSpeed  = 245;
int rightSpeed = 255;

// =====================================================
void setup()
{
  Serial.begin(9600);    // USB Monitor
  Serial1.begin(9600);   // ESP32 communication

  pinMode(IR1, INPUT_PULLUP);
  pinMode(IR2, INPUT_PULLUP);
  pinMode(IR3, INPUT_PULLUP);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  stopMotor();

  Serial.println("Arduino Mega Ready");
}

// =====================================================
void forward()
{
  analogWrite(ENA, leftSpeed);
  analogWrite(ENB, rightSpeed);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

// =====================================================
void stopMotor()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// =====================================================
void loop()
{
  // -------- Receive Bed Number from ESP32 --------
  if (Serial1.available())
  {
    String input = Serial1.readStringUntil('\n');
    input.trim();

    targetBed = input.toInt();

    if (targetBed >= 1 && targetBed <= 3)
    {
      bedCounter = 0;
      startMove = true;

      Serial.print("Moving to Bed ");
      Serial.println(targetBed);
    }
    else
    {
      Serial.println("Invalid Bed Number");
      stopMotor();
      startMove = false;
    }
  }

  if (!startMove) return;

  // -------- Read IR Sensors --------
  int L = digitalRead(IR1);
  int C = digitalRead(IR2);
  int R = digitalRead(IR3);

  String pattern = String(L) + String(C) + String(R);

  Serial.print("IR Pattern: ");
  Serial.println(pattern);

  // -------- Bed Marker Detection (000) --------
  if (pattern == "000")
  {
    if (!markerDetected)
    {
      bedCounter++;
      markerDetected = true;

      Serial.print("Bed Marker Count: ");
      Serial.println(bedCounter);

      // Stop at selected bed
      if (bedCounter == targetBed)
      {
        stopMotor();
        Serial.println("Target Bed Reached");
        startMove = false;
        return;
      }
    }
  }
  else
  {
    markerDetected = false;
    forward();   // Move forward for all non-marker patterns
  }

  delay(40);
}