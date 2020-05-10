/*
  WIFI Communicating sender
  Adjust IP according to your ESP32 value 10.42.0.253 in this example
  On your browser send :
  http://10.42.0.253/0   *********** to stop the sender
  http://10.42.0.253/1   *********** to start the sender
  http://10.42.0.253/sigCode/2 ******* to change the sigcode in use possible value are 0,1,2,3,4 ,see sigcode list
  http://10.42.0.253/?   *********** to see the state of the sender
  http://10.42.0.253/sigDuration/104    *********** to change the speed sender to 104 microsecondes
  http://10.42.0.253/sigDuration/50    *********** to change the speed sender to 50 microsecondes

  If USE_STATION : the sender start and stop automaticly if the mower is in the station or not


*/
#include <Wire.h>
#include "SDL_Arduino_INA3221.h"
#include "ACROBOTIC_SSD1306.h"
#include <WiFi.h>

//********************* user setting **********************************
const char* ssid     = "My_ssid_wifi";   // put here your acces point ssid
const char* password = "My_pass_wifi";  // put here the password
#define USE_STATION     1 // a station is connected and is used to charge the mower
#define USE_PERI_CURRENT      1     // use pinFeedback for perimeter current measurements? (set to '0' if not connected!)
#define USE_BUTTON      1     // use button to start mowing or send mower to station
#define USE_RAINFLOW    0     // check the amount of rain 
#define WORKING_TIMEOUT_MINS 300  // timeout for perimeter switch-off if robot not in station (minutes)
#define PERI_CURRENT_MIN    100    // minimum milliAmpere for cutting wire detection


//********************* setting for current sensor **********************************
float DcDcOutVoltage = 9.0;  //Use to have a correct value on perricurrent (Need to change the value each time you adjust the DC DC )
#define I2C_SDA 4
#define I2C_SCL 15
#define PERI_CURRENT_CHANNEL 1
#define MOWER_STATION_CHANNEL 2


byte sigCodeInUse = 1;  //1 is the original ardumower sigcode
int sigDuration = 104;  // send the bits each 104 microsecond (Also possible 50)
int8_t sigcode_norm[128];
int sigcode_size;


hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;



#define pinIN1       12  // M1_IN1         ( connect this pin to L298N-IN1)
#define pinIN2       13  // M1_IN2         ( connect this pin to L298N-IN2)
#define pinEnable    23  // EN             (connect this pin to L298N-EN)    
#define pinPushButton      34  //           (connect to Button) //R1 2.2K R2 3.3K
#define pinRainFlow       35  //           (connect to Rain box) //R3 2.2K R4 3.3K



// code version
#define VER "ESP32 2.0"

volatile int step = 0;
boolean enableSender = false; //OFF on start to autorise the reset
boolean WiffiRequestOn = true;


int timeSeconds = 0;
unsigned long nextTimeControl = 0;
unsigned long nextTimeInfo = 0;
unsigned long nextTimeSec = 0;
unsigned long nextTimeCheckButton = 0;
int workTimeMins = 0;
boolean StartButtonProcess = false;

int Button_pressed = 0;


float PeriCurrent = 0;
float ChargeCurrent = 0;
float shuntvoltage1 = 0;
float busvoltage1 = 0;
float loadvoltage1 = 0;
float shuntvoltage2 = 0;
float busvoltage2 = 0;
float loadvoltage2 = 0;

//*********************  Sigcode list *********************************************
// must be multiple of 2 !
// http://grauonline.de/alexwww/ardumower/filter/filter.html
// "pseudonoise4_pw" signal (sender)
int8_t sigcode0[] = { 1, -1 };  //simple square test code
int8_t sigcode1[] = { 1, 1, -1, -1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1 };  //ardumower signal code
int8_t sigcode2[] = { 1, 1, -1, -1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1 };
int8_t sigcode3[] = { 1, 1, -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, -1, -1, 1, -1,
                      -1, 1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, 1
                    }; // 128 Zahlen from Roland
int8_t sigcode4[] = { 1, 1, 1, -1, -1, -1};  //extend square test code

WiFiServer server(80);
SDL_Arduino_INA3221 ina3221;

void IRAM_ATTR onTimer()
{ // management of the signal
  portENTER_CRITICAL_ISR(&timerMux);
  if (enableSender) {

    if (sigcode_norm[step] == 1) {
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, HIGH);

    } else if (sigcode_norm[step] == -1) {
      digitalWrite(pinIN1, HIGH);
      digitalWrite(pinIN2, LOW);

    } else {
      Serial.println("errreur");
      //digitalWrite(pinEnable, LOW);
    }
    step ++;
    if (step == sigcode_size) {
      step = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&timerMux);

}

String IPAddress2String(IPAddress address)
{
  return String(address[0]) + "." +
         String(address[1]) + "." +
         String(address[2]) + "." +
         String(address[3]);
}


void changeArea(byte areaInMowing) {  // not finish to dev
  step = 0;
  enableSender = false;
  Serial.print("Change to Area : ");
  Serial.println(areaInMowing);
  for (int uu = 0 ; uu <= 128; uu++) { //clear the area
    sigcode_norm[uu] = 0;
  }
  sigcode_size = 0;
  switch (areaInMowing) {
    case 0:
      sigcode_size = sizeof sigcode0;
      for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
        sigcode_norm[uu] = sigcode0[uu];
      }
      break;
    case 1:
      sigcode_size = sizeof sigcode1;
      for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
        sigcode_norm[uu] = sigcode1[uu];
      }
      break;
    case 2:
      sigcode_size = sizeof sigcode2;
      for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
        sigcode_norm[uu] = sigcode2[uu];
      }
      break;
    case 3:
      sigcode_size = sizeof sigcode3;
      for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
        sigcode_norm[uu] = sigcode3[uu];
      }
      break;
    case 4:
      sigcode_size = sizeof sigcode4;
      for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
        sigcode_norm[uu] = sigcode4[uu];
      }
      break;


  }

  Serial.print("New sigcode in use  : ");
  Serial.println(sigCodeInUse);

  for (int uu = 0 ; uu <= (sigcode_size - 1); uu++) {
    Serial.print(sigcode_norm[uu]);
    Serial.print(",");
  }
  Serial.println();
  Serial.print("New sigcode size  : ");
  Serial.println(sigcode_size);
  enableSender = true;
}

void setup()
{
  Wire.begin(I2C_SDA, I2C_SCL);
  //------------------------  Signal parts  ----------------------------------------
  Serial.begin(115200);
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 104, true);
  timerAlarmEnable(timer);
  pinMode(pinIN1, OUTPUT);
  pinMode(pinIN2, OUTPUT);
  pinMode(pinEnable, OUTPUT);
  pinMode(pinPushButton, INPUT);
  pinMode(pinRainFlow, INPUT);

  Serial.println("START");
  Serial.print("Ardumower Sender ");
  Serial.println(VER);
  Serial.print("USE_PERI_CURRENT=");
  Serial.println(USE_PERI_CURRENT);

  changeArea(sigCodeInUse);
  if (enableSender) {
    digitalWrite(pinEnable, HIGH);
  }
  //------------------------  WIFI parts  ----------------------------------------
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  //------------------------  SCREEN parts  ----------------------------------------
  oled.init();    // Initialze SSD1306 OLED display
  delay(500);
  oled.clearDisplay();              // Clear screen
  delay(500);
  oled.setTextXY(0, 0);             // Set cursor position, start of line 0
  oled.putString("ARDUMOWER");
  oled.setTextXY(1, 0);             // Set cursor position, start of line 1
  oled.putString("BB SENDER");
  oled.setTextXY(2, 0);             // Set cursor position, start of line 2
  oled.putString("V3.1");
  oled.setTextXY(3, 0);           // Set cursor position, line 2 10th character
  oled.putString("USE INA3221");
  //------------------------  current sensor parts  ----------------------------------------
  Serial.println("Measuring voltage and current using ina3221 ...");
  ina3221.begin();
  Serial.print("Manufactures ID=0x");
  int MID;
  MID = ina3221.getManufID();
  Serial.println(MID, HEX);
  delay(5000);
}

void connection()
{
  oled.clearDisplay();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  //WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  for (int i = 0; i < 60; i++)
  {
    if ( WiFi.status() != WL_CONNECTED )
    {
      oled.setTextXY(0, 0);
      oled.putString("Try connecting");
      delay (250);
    }
  }

  if ( WiFi.status() == WL_CONNECTED )
  {
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    String ipAddress = IPAddress2String(WiFi.localIP());
    oled.clearDisplay();
    oled.setTextXY(0, 0);
    oled.putString("WIFI Connected");
    oled.setTextXY(1, 0);
    oled.putString(ipAddress);
    server.begin();
  }
}
static void ScanNetwork()
{
  oled.clearDisplay();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  oled.setTextXY(0, 0);
  oled.putString("Hotspot Lost");
  if (enableSender) {
    oled.setTextXY(3, 0);
    oled.putString("Sender ON ");
  }
  else
  {
    oled.setTextXY(3, 0);
    oled.putString("Sender OFF");
  }

  oled.setTextXY(4, 0);
  oled.putString("worktime= ");
  oled.setTextXY(4, 10);
  oled.putString("     ");
  oled.setTextXY(4, 10);
  oled.putFloat(workTimeMins, 0);

  if (USE_PERI_CURRENT) {
    busvoltage1 = ina3221.getBusVoltage_V(PERI_CURRENT_CHANNEL);
    PeriCurrent = ina3221.getCurrent_mA(PERI_CURRENT_CHANNEL);
    PeriCurrent = PeriCurrent - 300.0; //the DC/DC,ESP32,LN298N can drain up to 300 ma when scanning network
    if (PeriCurrent <= 5) PeriCurrent = 0; //
    PeriCurrent = PeriCurrent * busvoltage1 / DcDcOutVoltage; // it's 3.2666 = 29.4/9.0 the power is read before the DC/DC converter so the current change according : 29.4V is the Power supply 9.0V is the DC/DC output voltage (Change according your setting)
    oled.setTextXY(5, 0);
    oled.putString("Pericurr ");
    oled.setTextXY(5, 10);
    oled.putString("     ");
    oled.setTextXY(5, 10);
    oled.putFloat(PeriCurrent, 0);
  }


  if (USE_STATION) {
    ChargeCurrent = ina3221.getCurrent_mA(MOWER_STATION_CHANNEL);
    if (ChargeCurrent <= 5) ChargeCurrent = 0;
    oled.setTextXY(6, 0);
    oled.putString("Charcurr ");
    oled.setTextXY(6, 10);
    oled.putString("     ");
    oled.setTextXY(6, 10);
    oled.putFloat(ChargeCurrent, 0);
  }
  delay(5000);  // wait until all is disconnect
  int n = WiFi.scanNetworks();
  if (n == -1)
  {
    oled.setTextXY(0, 0);
    oled.putString("Scan running ???");
    oled.setTextXY(1, 0);
    oled.putString("Need Reset ?? ? ");
    oled.setTextXY(2, 0);
    oled.putString("If sender is OFF");
    delay(5000);
    if (!enableSender) ESP.restart(); // do not reset if sender is ON
  }
  if (n == -2)
    //bug in esp32 if wifi is lost many time the esp32 fail to autoreconnect,maybe solve in other firmware ???????
  {
    oled.setTextXY(0, 0);
    oled.putString("Scan Fail.");
    oled.setTextXY(1, 0);
    oled.putString("Need Reset ?? ? ");
    oled.setTextXY(2, 0);
    oled.putString("If sender is Off");
    delay(5000);
    if (!enableSender) ESP.restart();
  }
  if (n == 0)
  {
    oled.setTextXY(0, 0);
    oled.putString("No networks.");
  }
  if (n > 0)
  {
    Serial.print("find ");
    Serial.println(n);
    oled.setTextXY(0, 0);
    oled.putString("Find ");
    for (int i = 0; i < n; ++i) {
      // Print SSID for each network found
      char currentSSID[64];
      WiFi.SSID(i).toCharArray(currentSSID, 64);
      Serial.print("find Wifi : ");
      Serial.println(currentSSID);
      oled.setTextXY(0, 5);
      oled.putString(currentSSID);
      delay (1500);
      if (String(currentSSID) == ssid) {
        connection();
        i = 200; //to avoid loop again when connected
      }
    }
  }
}

void loop()
{
  if (millis() >= nextTimeControl) {
    nextTimeControl = millis() + 2000;  //after debug can set this to 10 secondes
    StartButtonProcess = false;
    oled.setTextXY(4, 0);
    oled.putString("worktime = ");
    oled.setTextXY(4, 10);
    oled.putString("     ");
    oled.setTextXY(4, 10);
    oled.putFloat(workTimeMins, 0);
    if (USE_PERI_CURRENT) {
      busvoltage1 = ina3221.getBusVoltage_V(PERI_CURRENT_CHANNEL);
      shuntvoltage1 = ina3221.getShuntVoltage_mV(PERI_CURRENT_CHANNEL);
      PeriCurrent = ina3221.getCurrent_mA(PERI_CURRENT_CHANNEL);
      PeriCurrent = PeriCurrent - 60.0; //the DC/DC,ESP32,LN298N drain 60 ma when nothing is ON and a wifi access point is found (To confirm ????)
      if (PeriCurrent <= 5) PeriCurrent = 0; //
      PeriCurrent = PeriCurrent * busvoltage1 / DcDcOutVoltage; // it's 3.2666 = 29.4/9.0 the power is read before the DC/DC converter so the current change according : 29.4V is the Power supply 9.0V is the DC/DC output voltage (Change according your setting)
      loadvoltage1 = busvoltage1 + (shuntvoltage1 / 1000);

      if ((enableSender) && (PeriCurrent < PERI_CURRENT_MIN)) {
        oled.setTextXY(5, 0);
        oled.putString("  Wire is Cut  ");
      }
      else
      {
        oled.setTextXY(5, 0);
        oled.putString("Pericurr ");
        oled.setTextXY(5, 10);
        oled.putString("     ");
        oled.setTextXY(5, 10);
        oled.putFloat(PeriCurrent, 0);
      }
    }

    if ( (WiFi.status() != WL_CONNECTED)) ScanNetwork();
    if  ( workTimeMins >= WORKING_TIMEOUT_MINS ) {
      // switch off perimeter
      enableSender = false;
      workTimeMins = 0;
      digitalWrite(pinEnable, LOW);
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, LOW);
      Serial.println("********************************   Timeout , so stop Sender  **********************************");
    }

  }

  if (millis() >= nextTimeSec) {
    nextTimeSec = millis() + 1000;

    oled.setTextXY(7, 0);
    oled.putString("                ");
    oled.setTextXY(7, 0);
    oled.putString("Area : ");
    oled.setTextXY(7, 7);
    oled.putFloat(sigCodeInUse, 0);


    if (USE_STATION) {
      busvoltage2 = ina3221.getBusVoltage_V(MOWER_STATION_CHANNEL);
      shuntvoltage2 = ina3221.getShuntVoltage_mV(MOWER_STATION_CHANNEL);
      ChargeCurrent = ina3221.getCurrent_mA(MOWER_STATION_CHANNEL);
      if (ChargeCurrent <= 5) ChargeCurrent = 0;
      loadvoltage2 = busvoltage2 + (shuntvoltage2 / 1000);
      oled.setTextXY(6, 0);
      oled.putString("Charcurr ");
      oled.setTextXY(6, 10);
      oled.putString("     ");
      oled.setTextXY(6, 10);
      oled.putFloat(ChargeCurrent, 0);

      if (ChargeCurrent > 200) { //mower is into the station ,in my test 410 ma are drained so possible to stop sender

        workTimeMins = 0;
        enableSender = false;
        digitalWrite(pinEnable, LOW);
        digitalWrite(pinIN1, LOW);
        digitalWrite(pinIN2, LOW);
      }
      else
      {
        workTimeMins = 0;
        enableSender = true;
        digitalWrite(pinEnable, HIGH);
        digitalWrite(pinIN1, LOW);
        digitalWrite(pinIN2, LOW);
      }
    }
    timeSeconds++;
    if ((enableSender) && (timeSeconds >= 60)) {
      if (workTimeMins < 1440) workTimeMins++;
      timeSeconds = 0;
    }
    if (enableSender) {
      oled.setTextXY(2, 0);
      oled.putString("Sender ON ");
    }
    else
    {
      oled.setTextXY(2, 0);
      oled.putString("Sender OFF");
    }
  }

  if (millis() >= nextTimeInfo) {
    nextTimeInfo = millis() + 500;
    float v = 0;
  }
  // Check if a client has connected
  WiFiClient client = server.available();
  if (client) {
    // Read the first line of the request
    String req = client.readStringUntil('\r');
    if (req=="") return;
    Serial.print("Client say  ");
    Serial.println(req);
    Serial.println("------------------------ - ");
    //client.flush();
    // Match the request
    if (req.indexOf("GET /0") != -1) {
      // WiffiRequestOn = false;
      enableSender = false;
      workTimeMins = 0;
      digitalWrite(pinEnable, LOW);
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, LOW);
      String sResponse;
      sResponse = "SENDER IS OFF";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();
    }
    if (req.indexOf("GET /1") != -1) {
      //WiffiRequestOn = 1;
      workTimeMins = 0;
      enableSender = true;
      digitalWrite(pinEnable, HIGH);
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, LOW);
      // Prepare the response
      String sResponse;
      sResponse = "SENDER IS ON";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();
    }

    if (req.indexOf("GET /?") != -1) {
      String sResponse, sHeader;
      sResponse = "MAC ADRESS = ";
      sResponse += WiFi.macAddress() ;
      sResponse += " WORKING DURATION= ";
      sResponse += workTimeMins ;
      sResponse += " PERI CURRENT Milli Amps= ";
      sResponse += PeriCurrent  ;
      sResponse += " sigDuration= ";
      sResponse += sigDuration ;
      sResponse += " sigCodeInUse= ";
      sResponse += sigCodeInUse ;
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();
    }




    if (req.indexOf("GET /sigCode/0") != -1) {
      sigCodeInUse = 0;
      changeArea(sigCodeInUse);
      // Prepare the response
      String sResponse;
      sResponse = "NOW Send Signal 0";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }
    if (req.indexOf("GET /sigCode/1") != -1) {
      sigCodeInUse = 1;
      changeArea(sigCodeInUse);
      // Prepare the response
      String sResponse;
      sResponse = "NOW Send Signal 1";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }

    if (req.indexOf("GET /sigCode/2") != -1) {
      sigCodeInUse = 2;
      changeArea(sigCodeInUse);
      // Prepare the response
      String sResponse;
      sResponse = "NOW Send Signal 2";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }

    if (req.indexOf("GET /sigCode/3") != -1) {
      sigCodeInUse = 3;
      changeArea(sigCodeInUse);
      // Prepare the response
      String sResponse;
      sResponse = "NOW Send Signal 3";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }

    if (req.indexOf("GET /sigCode/4") != -1) {
      sigCodeInUse = 4;
      changeArea(sigCodeInUse);
      // Prepare the response
      String sResponse;
      sResponse = "NOW Send Signal 4";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }
    if (req.indexOf("GET /sigDuration/104") != -1) {
      sigDuration = 104;
      timerAlarmWrite(timer, 104, true);
      // Prepare the response
      String sResponse;
      sResponse = "NOW 104 microsecond signal duration";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }

    if (req.indexOf("GET /sigDuration/50") != -1) {
      sigDuration = 50;
      timerAlarmWrite(timer, 50, true);
      // Prepare the response
      String sResponse;
      sResponse = "NOW 50 microsecond signal duration";
      // Send the response to the client
      Serial.println(sResponse);
      client.print(sResponse);
      client.flush();

    }



  }

  if ((USE_BUTTON) && (millis() > nextTimeCheckButton)) {
    nextTimeCheckButton = millis() + 100;
    if (StartButtonProcess) {
      oled.setTextXY(7, 0);
      oled.putString("StartButtonPress");
    }

    Button_pressed = digitalRead(pinPushButton);
    if (Button_pressed == 1) {
      Serial.println("Button pressed");
      //WiFiClient client = server.available();
      workTimeMins = 0;
      enableSender = true;
      digitalWrite(pinEnable, HIGH);
      digitalWrite(pinIN1, LOW);
      digitalWrite(pinIN2, LOW);
      if (client) {
        String sResponse, sHeader;
        sResponse = "BUTTON PRESSED";
        client.print(sResponse);
        client.flush();
      }
      nextTimeCheckButton = millis() + 1000;
      StartButtonProcess = true;
      nextTimeControl = millis() + 3000;
    }
  }

}
