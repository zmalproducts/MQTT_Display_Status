#include <Arduino.h>

 /*
    LilyGo Ink Screen Series Test
        - Created by Lewis he
        - Display Size: 212x104px
*/

// According to the board, cancel the corresponding macro definition
#define LILYGO_T5_V213

#include <boards.h>
#include <GxEPD.h>
//#include <Adafruit_GFX.h>
#include <SD.h>
#include <FS.h>

#include <GxDEPG0213BN/GxDEPG0213BN.h>    // 2.13" b/w  form DKE GROUP
#include GxEPD_BitmapExamples

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

/*********************** I/O Setup *******************************************/

#define PIN_VBAT 35             // Battery Voltage pin

/*********************** Sleep Setup *****************************************/
// user define
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  15      /* Time ESP32 will go to sleep (in seconds) */

/************************* WiFi Access Point *********************************/

#define WLAN_SSID       "galaxyhalo"
#define WLAN_PASS       "oplopok1"
#define WLAN_HOSTNAME   "DISPLAY_1"
/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER      "192.168.5.190"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    "mqttusr"
#define AIO_KEY         "subscribe"
#define MQTT_CONN_KEEPALIVE 300

/*********************** Program Setup ************************************/
# define ST_WL_CON 0
# define ST_MQ_CON 1
# define ST_WT_MSG 2
# define ST_WT_SLP 3

#define LED_CODE_SETUP_DONE 1
#define LED_CODE_WL_CON 10 
#define LED_CODE_WL_NCON 11
#define LED_CODE_ERROR 9999

/**************************************************************************/
  

GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RSET);
GxEPD_Class display(io, EPD_RSET, EPD_BUSY);

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Setup a feed for subscribing to changes.
Adafruit_MQTT_Subscribe outsidetemp = Adafruit_MQTT_Subscribe(&mqtt, "/sensors/TempOut_C");
Adafruit_MQTT_Subscribe outsidehum = Adafruit_MQTT_Subscribe(&mqtt, "/sensors/RelHumOut_Perc");
Adafruit_MQTT_Subscribe pressout = Adafruit_MQTT_Subscribe(&mqtt, "/sensors/PressOutNorm_hPa");


struct singleDataRecord {
  double Value;                       // Value as double  
  bool recieved;                      // 0 = no, 1 = yes  
};

typedef struct singleDataRecord SingleDataRecord;

struct singleDataRecordStatus {
  unsigned long ulTimestampLastMessageRecieved;
};

typedef struct singleDataRecordStatus SingleDataRecordStatus;

struct ValueStatus {  
  double adValueThresh[5];    // full @ max
  unsigned int status;  
};

typedef struct ValueStatus valueStatus;

enum enMQTTSubscription {
    MqSubOutsideTemp = 0,
    MqSubOutsideHumidity = 1,
    MqSubOutsidePressure = 2,
    MqSubNone = 32767,
};

enum enLocalValues {
    LvBatteryVoltage = 0,
    LvWlanRssi = 1,
};

RTC_DATA_ATTR int bootCount = 0;

const int CIMAXMQTTRECORDS = 3;
const int CIMAXLOCALRECORDS = 2;


SingleDataRecord stMqttRcvValues[CIMAXMQTTRECORDS];

RTC_DATA_ATTR singleDataRecordStatus stMqttRcvValuesStat[CIMAXMQTTRECORDS] = {0,0,0};


SingleDataRecord stLocaDataBus[CIMAXLOCALRECORDS];

valueStatus stBattery;        // ValueStatus
valueStatus stWlanStrength;    // ValueStatus



/**functions ***********************************************************************************************************************************/


void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason){
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_Io"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void LEDBlink(int pin, unsigned long cul_PauseTime, unsigned int repetitions){

volatile int cnt = 1;

  for (cnt; cnt <= repetitions; cnt ++){
    digitalWrite(pin, HIGH);   // Turn the LED on by making the voltage HIGH
    delay(cul_PauseTime);            // Wait for a second
    digitalWrite(pin, LOW);  // Turn the LED off by making the voltage LOW
    delay(cul_PauseTime);            // Wait for a second
  } 
  delay(300); 
}

void LEDShowStatusCode(int LEDStateCode){

  switch (LEDStateCode) {
  
   //connect to WLAN
   case LED_CODE_SETUP_DONE:      
      LEDBlink(LED_PIN, 200, 2);            
      break;  
  
   //connect to WLAN
   case LED_CODE_WL_CON:      
      LEDBlink(LED_PIN, 100, 3);      
      delay (500);
      LEDBlink(LED_PIN, 50, 1);
      
   break;  

   //connect to WLAN
   case LED_CODE_WL_NCON:
      LEDBlink(LED_PIN, 100, 3);   
      delay(500);
      LEDBlink(LED_PIN, 50, 2);      
   break;  


   //connect to WLAN
   case LED_CODE_ERROR:
      LEDBlink(LED_PIN, 200, 5);           
   break;

   default:
   break;
  }
}

void getBatteryVoltage(SingleDataRecord *Value){
     
  Value->Value = ((double)analogRead(PIN_VBAT) * 3600. / 4095. * 2.) / 1000 ;
  Value->recieved   = true;

}

void getWlanRssi(SingleDataRecord *Value){

  Value->Value = (double)WiFi.RSSI();
  Value->recieved   = true;

  Serial.println("RSSI: ");
  Serial.print(Value->Value);
}

int igetRssiStatus(){
  volatile double rssi = 0;

 getWlanRssi(&stLocaDataBus[LvWlanRssi]);
  rssi = stLocaDataBus[LvWlanRssi].Value;

  if (rssi <= stWlanStrength.adValueThresh[0]){       
    stWlanStrength.status = 0;  
  }
  if ((rssi > stWlanStrength.adValueThresh[0]) &&     
        (rssi <= stWlanStrength.adValueThresh[1])){
    stWlanStrength.status = 1;  
  }
  if ((rssi > stWlanStrength.adValueThresh[1]) &&     
        (rssi <= stWlanStrength.adValueThresh[2])){
    stWlanStrength.status = 2;  
  }  
  if ((rssi > stWlanStrength.adValueThresh[2]) &&     
        (rssi <= stWlanStrength.adValueThresh[3])){
    stWlanStrength.status = 3;  
  }
  if (rssi > stWlanStrength.adValueThresh[3]){        
    stWlanStrength.status = 4;  
  }  
  Serial.print("stWlanStrength: ");
  Serial.println(rssi);

  Serial.print("adValueThresh[0]: ");
  Serial.println(stWlanStrength.adValueThresh[0]);  

  Serial.print("adValueThresh[3]: ");
  Serial.println(stWlanStrength.adValueThresh[3]);

  Serial.print("stBattery.adValueThresh[3]: ");
  Serial.println(stBattery.adValueThresh[0]);

  Serial.print("status: ");
  Serial.println(stWlanStrength.status);

return stWlanStrength.status;

}



int igetBatStatus(){

double voltage = 0;

  getBatteryVoltage(&stLocaDataBus[LvBatteryVoltage]);
   
  voltage = stLocaDataBus[LvBatteryVoltage].Value;  

  if (voltage <= stBattery.adValueThresh[0]){       // [    ]
    stBattery.status = 0;  
  }
  if ((voltage > stBattery.adValueThresh[0]) &&     // [   |]
        (voltage <= stBattery.adValueThresh[1])){
    stBattery.status = 1;  
  }
  if ((voltage > stBattery.adValueThresh[1]) &&     // [  ||]
        (voltage <= stBattery.adValueThresh[2])){
    stBattery.status = 2;  
  }  
  if ((voltage > stBattery.adValueThresh[2]) &&     // [ |||]
        (voltage <= stBattery.adValueThresh[3])){
    stBattery.status = 3;  
  }
  if (voltage > stBattery.adValueThresh[3]){        // [||||]
    stBattery.status = 4;  
  }  
  Serial.print("vBat: ");
  Serial.println(voltage);

  Serial.print("status: ");
  Serial.println(stBattery.status);

return stBattery.status;

}

void initDataStructures(void){
Serial.println("initDataStructures: ");
volatile int cnt=0;

// Mqtt recieved values
  for (cnt = 0; cnt <= CIMAXMQTTRECORDS-1; cnt++){
    stMqttRcvValues[cnt].recieved = false;
    stMqttRcvValues[cnt].Value = 0;
  }

// local Values  
  for (cnt = 0; cnt <= CIMAXLOCALRECORDS-1; cnt++){
    stLocaDataBus[cnt].recieved = false;
    stLocaDataBus[cnt].Value = 0;
  }  

//BatteryStatus
  stBattery.adValueThresh[0] = 3.0;
  stBattery.adValueThresh[1] = 3.5;
  stBattery.adValueThresh[2] = 3.55;
  stBattery.adValueThresh[3] = 3.6;
  stBattery.status = 0;

// Wlan Status
  stWlanStrength.adValueThresh[0] = -95.0;
  stWlanStrength.adValueThresh[1] = -80.0;
  stWlanStrength.adValueThresh[2] = -73.0;
  stWlanStrength.adValueThresh[3] = -60.0;
  stWlanStrength.status = 0;

 Serial.print("stWlanStrength.adValueThresh[0]: ");
 Serial.println(stWlanStrength.adValueThresh[0]);
}

int checkRevcievedStatusAllMessages(SingleDataRecord Vaules[], int n){
volatile int iRecCtr = 0;
//scan if the all messages were recieved
 for (int i=0; i<=n-1; i++){
  if (Vaules[i].recieved == true){
    //Serial.print("checkRevcievedStatusAllMessages recieved[i]: "); Serial.print(Vaules[i].recieved); Serial.print("checkRevcievedStatusAllMessages i: "); Serial.print(i); Serial.print(" iRecCtr: "); Serial.println(iRecCtr); 
    iRecCtr++;
  }
 }

 //Serial.printf("iRecCtr"); Serial.println(iRecCtr); 
 if (iRecCtr == n){
   return 1;
 }

return 0;
}

void StoreValue(enMQTTSubscription enrecievedValue, double value){

 stMqttRcvValues[enrecievedValue].Value = value;
 stMqttRcvValues[enrecievedValue].recieved = true;                          // set value recieved flag

  Serial.print(F("stored value ")); Serial.print(value); Serial.print(F(" in ")); Serial.print(enrecievedValue); Serial.print(F(" recv flag ")); Serial.println(stMqttRcvValues[enrecievedValue].recieved); 
}
 
// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");
    
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
      // if wlan is not connected, try to connect Serial.println("reconnect Wlan");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print("Wlan lost, reconnect ... ");
          WiFi.reconnect();
          delay(500);         
          Serial.print(".");
        }
                
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
  }
  Serial.println("MQTT Connected!");
}

void printScreen(){
  int headerline = 0;
  int firstlineY = 52;
  int secondlineY;
  int thirdLineY;  

  int startposBatteryX = 25;
  int startposBatteryY = 0;

  int startposSignalX = 0;
  int startposSignalY = 0;

  int signalstrength = 4;

  secondlineY = firstlineY + 34;
  thirdLineY = secondlineY + 34; 


  int hpalineY1 = thirdLineY-14;
  int hpalineY2 = hpalineY1 + 11;

  int batstatint = 0;
  batstatint = 4-stBattery.status;

  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);  

   
  // draw ValueStatus

  display.drawRect(startposBatteryX, startposBatteryY +2, 2, 6, 0); // +pole line
  display.fillRect(startposBatteryX +2, startposBatteryY +0, 22, 10, 0); // frame
  if (batstatint != 0){

  display.fillRect(startposBatteryX + 3, startposBatteryY +1 , 5 * batstatint , 8, 0xFFFF); // frame
  }

  // draw signal strength
  // -- draw antenna
  display.drawLine(startposSignalX, startposSignalY+0, startposSignalX + 6, startposSignalY +0, 0x0000);
  display.drawLine(startposSignalX, startposSignalY +1, startposSignalX + 3, startposSignalY +6, 0x0000);
  display.drawLine(startposSignalX + 3, startposSignalY +6, startposSignalX +6,  startposSignalY + 1, 0x0000);
  display.drawLine(startposSignalX + 3, startposSignalY +6, startposSignalX +3,  startposSignalY + 10, 0x0000);


  signalstrength = stWlanStrength.status;
  // draw bars 
  for (int i = 0; i<=signalstrength; i++){     
    // strength = 0 -> no bar    
    // else -> bar with rising height from strength 1..4
    if (i != 0){  
        // (9+3*(i-1)) stat bars beginn at 9 px; bar width = 2px, 1 px space = new bar beginns every 3 px. 
        // i-1 -> first bar beginns at 9 px 

        // negative width (-2*i) bc drawing rectange from bottom to top; each bar is 2px (..2*i) higher than the previeous one
        display.fillRect(startposSignalX + (9+3*(i-1)), startposSignalY + 10 , 2, -(2*i), 0x0000);   // balken
    }   
  
  }  
  
  // print mqtt values
  display.setFont(&FreeSansBold24pt7b);
  display.setCursor(114, firstlineY);
  display.printf("%.1f", stMqttRcvValues[MqSubOutsideTemp].Value); display.setCursor(210, firstlineY); display.printf("C");   
  display.setFont(&FreeSans18pt7b);
  display.setCursor(118, secondlineY);
  display.printf("%.1f", stMqttRcvValues[MqSubOutsideHumidity].Value); display.setCursor(212, secondlineY); display.print("%");
  display.setCursor(100, thirdLineY);
  display.printf("%.1f", stMqttRcvValues[MqSubOutsidePressure].Value); 
  display.setFont(&FreeSans9pt7b);
  display.setCursor(223, hpalineY1);
  display.print("h");
  display.setCursor(218, hpalineY2);
  display.print("pa");
  display.update();
}

/*************************************************************************************************************************************/


void setup()
{
  //  ------ Setup I/O ----------------------------------------------------------------------------
  pinMode(PIN_VBAT, INPUT);  

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Turn the LED off 

  //  ------ Setup Serial -------------------------------------------------------------------------
  Serial.begin(115200);  
  delay(10); //Take some time to open up the Serial Monitor
  Serial.println("serial running");  
  //  ------ Setup Wlan ---------------------------------------------------------------------------

  /****** Connect to WiFi access point. **********/ 
  Serial.println("Wlan setup ...");
  Serial.print("Connecting to:"); Serial.println(WLAN_SSID);
  
  WiFi.mode(WIFI_STA); // deactivate AP-Mode
  WiFi.hostname(WLAN_HOSTNAME);
  WiFi.begin(WLAN_SSID, WLAN_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    
    LEDShowStatusCode(LED_CODE_WL_NCON);
    
    Serial.print(".");
  }
  LEDShowStatusCode(LED_CODE_WL_CON);

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: "); Serial.println(WiFi.localIP());
  
  //  ------ Setup MQTT subscription for feeds. ---------------------------------------------------

  mqtt.subscribe(&outsidetemp);
  mqtt.subscribe(&outsidehum);
  mqtt.subscribe(&pressout);
  Serial.println("mqtt subscribed.");

//  ------ Setup sleep timer ----------------------------------------------------------------------
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +
  " Seconds ...");

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

++bootCount;
  Serial.println("Boot number: " + String(bootCount));
  
  
// ------ Setup display ---------------------------------------------------------------------------
  SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
  
  display.init();
  display.setTextColor(GxEPD_BLACK);

// ------ Setup Application------------------------------------------------------------------------

initDataStructures();

}

void loop(){

  MQTT_connect();
  
  // this is our 'wait for incoming subscription packets' busy subloop
  // try to spend your time here
  Adafruit_MQTT_Subscribe *subscription;    
  while ((subscription = mqtt.readSubscription(5000))) {    


    // fetched outside temperature
    if ((subscription == &outsidetemp)){
//          && (stMqttRcvValues[actualMessage].recieved == false))      
      StoreValue(MqSubOutsideTemp, atof((char*)outsidetemp.lastread));         
    
    // fetched outside humidity
    } else if(subscription == &outsidehum) {        
      StoreValue(MqSubOutsideHumidity, atof((char*)outsidehum.lastread));  
    
    // fetched outside pressure
    } else if(subscription == &pressout) {
      StoreValue(MqSubOutsidePressure, atof((char*)pressout.lastread));
    }
  }

  


  

  
  igetBatStatus();

  igetRssiStatus();


  // alle messages bekommen -> anzeigen & powerdown
  if(checkRevcievedStatusAllMessages(stMqttRcvValues, CIMAXMQTTRECORDS) == 1){
    printScreen();
    
    display.powerDown();    
    Serial.println("Going to sleep now");
    delay(1000);
    Serial.flush();     
    esp_deep_sleep_start();
  };


  // ping the server to keep the mqtt connection alive
  if(! mqtt.ping()) {
    mqtt.disconnect();
  }
}