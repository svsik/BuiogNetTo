/* -------------------------------------
  Net-To-...
  ----------------------------------------*/


// TEST/DEBUG defines
// ------------------
#define _DEBUG_

// framework selection
// -----------------------
#define _IOT_BLYNK_


// INCLUDES
// ========

// Added by me
#include <Arduino.h>
#include <SPI.h>

// BME Sensor
#include <Adafruit_BME280.h>
#define BME280_I2C_ADDRESS 0x76
// End added by me

// TaskScheduler options:
//#define _TASK_TIMECRITICAL    // Enable monitoring scheduling overruns
#define _TASK_SLEEP_ON_IDLE_RUN // Enable 1 ms SLEEP_IDLE powerdowns between tasks if no callback methods were invoked during the pass
#define _TASK_STATUS_REQUEST    // Compile with support for StatusRequest functionality - triggering tasks on status change events in addition to time only
//#define _TASK_WDT_IDS         // Compile with support for wdt control points and task ids
//#define _TASK_LTS_POINTER     // Compile with support for local task storage pointer
#define _TASK_PRIORITY          // Support for layered scheduling priority
//#define _TASK_MICRO_RES       // Support for microsecond resolutionMM
#include <TaskScheduler.h>

#include <EEPROM.h>
#include <AvgFilter.h>

#include <TimeLib.h>            // just including Time.h does not work anymore for now() method (for some reason)
#include <Timezone.h>
#include <RTClib.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <FS.h>

#include <B_Net_To_Settings.h>


// DEFINES AND GLOBAL VARIABLES
// ============================

// Natural constants
#define STATIC_TIME     1451606400UL  // set initial time to Jan 1, 2016
#define HALF_HOUR       1800          // seconds
#define SD3             10            // SD3 pin on NodeMCU is GPIO10
#define NIGHT_TOLERANCE 5             // minutes after night start and before it ends

/*
  Compensating factor for imperfections of built-in RTC:
  Comparing epoch times between internal RTC and external RTC after 10 minute deep sleep
  Internal RTC:1483693345
  External RTC:1483693318
  Internal RTC is *ahead* meaning that the device will wake up *earlier* than necessary,
  so we need to *add* time to sleep request to compensate
  (Internal - External)/10 min = 27 sec / 600 sec = 0.045 seconds/second or 45000 uSec/sec of sleep
*/

// Hour of the day to "go to sleep" (i.e., do not operate after this hour)
#define GOTOSLEEP      24     // hour to go to sleep
#define GOTOSLEEP_MIN  0      // hour to go to sleep
#define GOTOSLEEP_MAX  24     // hour to go to sleep

// Hour of the day to "wake up" (i.e., operate after this hour)
#define WAKEUP         0      // hour to wake up
#define WAKEUP_MIN     0      // hour to wake up
#define WAKEUP_MAX     24     // hour to wake up

// Number of hours to add to wake up time on a weekend
#define WEEKENDADJ      0     // number of hours to add for the wakeup on a weekend
#define WEEKENDADJ_MIN  0     // number of hours to add for the wakeup on a weekend
#define WEEKENDADJ_MAX  12    // number of hours to add for the wakeup on a weekend

// Wifi
// ----
#define CONNECT_TIMEOUT 60    //seconds
#define NTP_TIMEOUT     60    //seconds
#define CONNECT_BLINK   1000  // ms
#define WL_PERIOD       100   // ms
#define CONNECT_INTRVL  2000  // check every X ms
#define NTPUPDT_INTRVL  5000  // check every X ms

// EEPROM token for parameters
const char *CToken =  "APIS03\0"; // Eeprom token: Automatic Plant Irrigation System

String ssid, pwd;

#define  TICKER          15                 // probe soil humidity every 30 minutes

#define  SLEEP_TOUT       TASK_MINUTE       // in powersave mode the system will enter powersave mode after 1 minute
#define  SLEEP_TOUT_CONN (TASK_MINUTE * 5)  // in powersave mode: if http connection is detected - the system will stay on for this amount of time
#define  CONFIG_DELAY    (TASK_MINUTE * 15) // in powersave mode: initial delay to allow parameter configuration (after first power on)
#define  HTTP_ERRORS     5                 // reset the device if this many http errors were detected sequentially (resets every time http is successful)

const char* CWakeReasonSleep = "Deep-Sleep Wake";   // reason code for device wake up
const char* CWakeReasonReset = "External System";   // reason code for device cold reset
bool        coldBoot;                               // a flag indicating system was hard reset as opposed to warm boot or woke up from timer


// Forward declaration of all methods
// ----------------------------------
void cfgChkAP();
void cfgChkConnect();
void cfgChkNTP();
void cfgFinish();
void cfgInit();
void cfgSetAP();
void configurePins();
void connectTOut();
void connectedChk();
void connectionEnforcer();
void cpParams(byte*, byte*);
void enableWebServer();
void disableWebServer();
bool doNtpUpdateCheck();
void doNtpUpdateInit();
void doSetTime(unsigned long);
String getContentType(String);
time_t getTime();
void handleClientCallback();
void handleConfnetworksave();
void handleConfwatersave();
void handleFileCreate();
void handleFileDelete();
void handleFileList();
bool handleFileRead(String);
void handleFileUpload();
bool httpRequest(String&);
int indexParser (int, char*, int*);
void initiate_reconnect();
void initiate_wifi_connection();
void iot_online();
void iot_report();
void iot_sleep();
void iot_status_update(String&);
bool isNight();
void loadParameters();
void loop();
void measureRestart();
int networkParser (int aTag, char* buf, int len);
void ntpTOut();
void ntpUpdate();
void parseFile(char*, int (*)(int, char*, int));
void parseFile(char*, int);
void ping(unsigned long aTimeout);
void postWaterCallback();
void printTime(DateTime aTime);
void probePowerOff();
void probePowerOn();
void recalcRtcComp();
void resetDevice();
void saveParameters();
void saveTimeToRTC (unsigned long aUt);
void onClientConnected(const WiFiEventSoftAPModeStationConnected&);
void onClientDisconnected(const WiFiEventSoftAPModeStationDisconnected&);
void onDisconnected(const WiFiEventStationModeDisconnected&);
unsigned long sendNTPpacket(IPAddress&);
void serverHandleNotFound();
void serverHandleRoot();
void serverTOut();
void setup();
void sleepCallback();
void testTicker();
void ticker();
bool useLed();

// Task Scheduling
// ---------------
//StatusRequest measurementsReady;    // event signalling that all measurements (soil and water level) are done and values are ready
//StatusRequest probeIdle;            // event signalling that the humidity probe is idle (not under voltage)
StatusRequest pageLoaded;           // event signalling completion of the html page load (to reset after the reset picture fully loads)
StatusRequest wateringDone;         // event signalling watering procedure has completed
StatusRequest httpError;            // event counting the number of http put errors - to reset device if too many errors occured

Scheduler ts;                       // TaskScheduler scheduler object

Task tConfigure     (TASK_IMMEDIATE, TASK_ONCE, &cfgInit, &ts, true);
// Task tLedBlink      (TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ts, false, &ledOnEnable, &ledOnDisable);
Task tTimeout       (TASK_IMMEDIATE, TASK_FOREVER, NULL, &ts);
Task tNtpUpdater    (NTPUPDT_INTRVL, NTP_TIMEOUT, &ntpUpdate, &ts);
Task tConnected     (TASK_SECOND, CONNECT_TIMEOUT, &connectedChk, &ts);
Task tHandleClients (TASK_SECOND, TASK_FOREVER, &handleClientCallback, &ts);
Task tTicker        (TICKER * TASK_MINUTE, TASK_FOREVER, &ticker, &ts);
// Task tMeasure       (&measureCallback, &ts);
Task tMeasureRestart(&measureRestart, &ts);
Task tPostWater     (&postWaterCallback, &ts);
// Task tMesrLevel     (WL_PERIOD, TASK_FOREVER, &measureWL, &ts, false, &measureWLOnEnable, NULL);
// Task tMesrMoisture  (TASK_SECOND, TASK_FOREVER, &measureMS, &ts, false, &measureMSOnEnable, &measureMSOnDisable);
// Task tWater         (TASK_IMMEDIATE, TASK_ONCE, &waterCallback, &ts, false, &waterOnEnable, &waterOnDisable);
// Task tLeakChecker   (TASK_SECOND, TASK_FOREVER, &waterleakChecker, &ts, false);
Task tSleep          (&sleepCallback, &ts);
Task tIotReport     (TASK_IMMEDIATE, TASK_ONCE, &iot_report, &ts);
Task tReset         (&resetDevice, &ts);
Task tErrorReset    (&resetDevice, &ts);
Task tEnforcer      (TICKER * 2 * TASK_MINUTE, TASK_ONCE, &connectionEnforcer, &ts);


// Parameters
// ----------
typedef struct {
  // token
  char      token[7];   //  6 digit token = APISxx, where xx is a version + '\0'

  // watering parameters
  byte      high;           //  high humidity mark - stop watering
  byte      low;            //  low humidity mark - start watering
  byte      retries;        //  number of watering runs before give up (if high not reached)
  byte      watertime;      //  pumping duration
  byte      saturate;       //  saturation duration
  byte      gotosleep;      //  hour to go goodnight (e.g., 22)
  byte      wakeup;         //  hour to wake up (e.g., 08)
  byte      wkendadj;       //  weekend wake up adjustment time
  int       wl_depth;       //  water container depth (empty), in mm
  int       wl_low;         //  water container level low mark, in mm
  //  12 bytes

  // wifi parameters
  byte      is_ap;          //  is this system configured to be an access point or connect to a network
  char      ssid[32];       //  SSID of the network to connect to -OR- SSID of the AP to create
  char      pwd[65];        //  Password for the network to be connected to -OR- AP password
  char      ssid_ap[32];    //  SSID of the network to connect to -OR- SSID of the AP to create
  char      pwd_ap[65];     //  Password for the network to be connected to -OR- AP password
  // 195 bytes

  // power parameters
  bool      powersave;      // false: run continuosly, true: run in a power saving mode
  // 2 bytes

  // other parameters
  int       ping_interval;  // soil humidity check interval, minutes
  byte      led_use;        // LED use option: 0 - depending on night mode, 1 - always, 2 - never
  char      ntp_server[32]; // hostname of the ntp server
  // 35 bytes
  // total of 246 bytes
} TParameters;

TParameters parameters;

typedef struct {
  unsigned long magic;      // 4b: token: 1234567890 for validation
  unsigned long ccode;      // 4b: control code for the magic number = ~magic
  unsigned long ttime;      // 4b: unix time at the time of reset
  bool          hasntp;     // 2b: was the time ntp driven?
  unsigned long ntptime;    // 4b: epoch time at the last NTP update
  long          rtccomp;    // 4b: current rtc_comp value
  bool          rtcreq;     // 2b: rtc recalc requested
  // Total of 22 bytes
} TTimeStored;

TTimeStored   time_restore;

#define       MAGIC_NUMBER  1234567890UL
#define       WAKE_DELAY    0            // number of seconds spent waking up

// typedef struct {
//   time_t    water_start;  // time watering run started
//   byte      hum_start;    // humidity at the start
//   int       wl_start;     // water level when started, in mm
//   byte      num_runs;     // number of runs it took to water
//   byte      run_duration; // run durations
//   time_t    water_end;    // time watering stopped
//   byte      hum_end;      // humidity at the end of run
//   int       wl_end;       // water level when ended, in mm
// } TWaterLog;
//
// TWaterLog water_log;

// Support for timezones:
// ======================
//Irish Time
TimeChangeRule myDST = {"EDT", Last, Sun, Mar, 2, +60};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", Last, Sun, Oct, 2, 0};  //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);


// NTP Related Definitions
// =======================
#define NTP_PACKET_SIZE  48 // NTP time stamp is in the first 48 bytes of the message

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
IPAddress     timeServerIP; // time.nist.gov NTP server address
const char*   ntpServerName = "time.nist.gov";
byte          packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

#define LOCAL_NTP_PORT  2390
#define LOCAL_HTTP_PORT 80
#define LOCAL_DNS_PORT  53

IPAddress apIP(192, 168, 1, 1);   // Local IP address for the Access Point mode

ESP8266WebServer server(LOCAL_HTTP_PORT);           // local webserver
DNSServer dnsServer;                                // local dns server for AP mode

RTC_Millis rtc;

time_t        bootTime;                             // Timestamp of the device start up
time_t        tickTime;                             // Timestamp of the current tick
unsigned long epoch;                                // Time reported by NTP
bool          nightMode = false;                    // Determine whether it is night once during tick, and then use the variable instead.
// unsigned long ledOnDuration, ledOffDuration;        // durations for LED on and off states - for fancy blinking
bool          tickerIdleRun = true;                 // Indicates that a ticker run is idle (i.e. will do measurment w/o watering - for more accurate IoT update in time)

// Execution
bool connectedToAP = false;                         // if connected to AP, presumably can query and set the time via NTP
bool runningAsAP   = false;                         // running as AP, internet is not available, no correct time is available
bool hasNtp;                                        // Indicates that device was able to update time from NTP servers at some point
long rtcComp;                                       // dynamically calculated microseconds of adjustment per 1 second of deep sleep to compensate for internal RTC error
bool rtcCompRequested;

// WiFi specific events
WiFiEventHandler disconnectedEventHandler;
WiFiEventHandler clientConnectedEventHandler;
WiFiEventHandler clientDisconnectedEventHandler;
unsigned int numClients;

// Ultrasonic measurment related
unsigned long distance;

// Code
// ------------------------------------------------------------------

/*-------
  New Code
  -------*/


// bmp8266
Adafruit_BME280 bme; // I2C

void initBMP280() {
  Wire.begin();
  Serial.println(F("BMP280 test"));

  if (!bme.begin(BME280_I2C_ADDRESS)) {
    do {
      Serial.println(F("Could not find a valid BMP280 sensor, check wiring! Retrying in 5s"));
      delay(5000);
    } while (!bme.begin(BME280_I2C_ADDRESS));
  }

  do {
    delay(10);
    Serial.print(F("Taking measurements"));
    delay(10);
    Serial.print(F("."));
    delay(10);
    Serial.print(F("."));
    delay(10);
    Serial.println(F("."));
    Serial.println();
  } while (!bme.readTemperature() || !bme.readPressure() || !bme.readHumidity());

  Serial.print(bme.readTemperature());
  Serial.println(F(" *C"));
  Serial.print(bme.readPressure());
  Serial.println(F(" Pa"));
  Serial.print(bme.readAltitude(1013.25)); // this should be adjusted to your local forcase
  Serial.println(F(" m"));
  Serial.print(bme.readHumidity()); // this should be adjusted to your local forcase
  Serial.println(F(" %"));
}

/*-------------
  End New Code
  ------------- */

// Utility methods
// ---------------

// /**
//    Calculates water level based on the distance to water surface
//    measured by the utrasonic sensor
// */
//
// #define   WL_SAMPLES  10
// long      wlData[WL_SAMPLES];           // Water level is averaged based on 5 measurements
// avgFilter wl(WL_SAMPLES, wlData);       // Average filter for water level measurements
//
// void ping(unsigned long aTimeout) {     // in micros
//
//   pinMode(SR04_ECHO_PIN, OUTPUT);
//   digitalWrite(SR04_ECHO_PIN, LOW);
//   delayMicroseconds(2);
//   digitalWrite(SR04_ECHO_PIN, HIGH);
//   delayMicroseconds(5);
//   digitalWrite(SR04_ECHO_PIN, LOW);
//   pinMode(SR04_ECHO_PIN, INPUT);
//   delayMicroseconds(5);
//
//   long duration, cwl;
//
//   duration = pulseIn(SR04_ECHO_PIN, HIGH, aTimeout);
//   if ( duration == 0 ) duration = aTimeout;
//   cwl  = parameters.wl_depth - ( duration * 100UL / 580UL + 10UL );
//   if ( cwl < 0 ) cwl = 0;
//   if ( cwl > parameters.wl_depth ) cwl = parameters.wl_depth;
//   currentWaterLevel = wl.value(cwl);
//
// #ifdef _DEBUG_
//    long d, mm;
//    d = pulseStop - pulseStart;
//    mm = d * US_TO_MM_FACTOR / 1000;
//    Serial.print(millis());
//    Serial.print(F(": waterLevelCalc. d = "));
//    Serial.println(d);
//    Serial.print(F("mm = "));
//    Serial.println(mm);
//    Serial.print(F("cwl = "));
//    Serial.println(currentWaterLevel);
// #endif
// }


// // Pump Motor methods
// // ------------------
//
// /**
//    Turns pump motor on/off depending on the state
//
//    @param: aState: true - turn the pump on; false - turn the pump off
// */
// void motorState(bool aState) {
//   digitalWrite(PUMP_PWR_PIN, aState ? HIGH : LOW);
// }
//
// /**
//    Turn pump on
// */
// void motorOn() {
//   motorState(true);
// }
//
// /**
//    Turn pump off
// */
// void motorOff() {
//   motorState(false);
// }
//
// /**
//    Turns the power on soil humidity probe ON
// */
// void probePowerOn() {
//   digitalWrite(MOISTURE_PWR_PIN, HIGH);
// }
//
// /**
//    Turns the power on soil humidity probe OFF
// */
// void probePowerOff() {
//   digitalWrite(MOISTURE_PWR_PIN, LOW);
// }
//
// /**
//    Retruns TRUE if there is water in the tray under the flower pot
// */
// bool hasLeaked() {
//
//   pinMode(LEAK_PIN, INPUT_PULLUP);
//   delay(5);
//
//   bool hasleaked = ( digitalRead(LEAK_PIN) == LOW );
//
// #ifdef _DEBUG_
//   Serial.print(millis());
//   Serial.print(F(": hasLeaked = ")); Serial.println(hasleaked ? "YES" : "NO");
//   Serial.print(F("LEAK_PIN = ")); Serial.println(digitalRead(LEAK_PIN));
// #endif
//
//   pinMode(LEAK_PIN, OUTPUT);
//   digitalWrite(LEAK_PIN, LOW);
//
//   return hasleaked;
// }

// /**
//    Returns TRUE if all conditions permit watering run
// */
// bool canWater() {
//   return !( hasLeaked() || currentWaterLevel < parameters.wl_low || isNight() );
// }

/**
   Returns TRUE if it is night time according to local clock and night parameters
*/
bool isNight() {
  nightMode = false;
  if ( hasNtp ) {

    time_t tnow = myTZ.toLocal( now() );

    //  Allow 5 min "grace" period for wakeup/go to sleep due to clock imperfections
    //  Defined as NIGHT_TOLERANCE

    int hr = hour(tnow) * 60 + minute(tnow);
    int wkp = parameters.wakeup * 60 - NIGHT_TOLERANCE; // wakep could occur up to 5 min earlier
    int gts = /*parameters.gotosleep*/24 * 60 + NIGHT_TOLERANCE; // go to sleep could occur up to 5 min later

#ifdef _DEBUG_
       Serial.print(millis());
       Serial.print(F(": isNight. hr="));
       Serial.println(hr);
       //  return false;
#endif

    //  Add adjusting hours to the wakeup time for Saturday and Sunday
    if ( weekday(tnow) == dowSunday || weekday(tnow) == dowSaturday ) wkp += parameters.wkendadj * 60;
    nightMode = ( hr >= gts || hr < wkp );

#ifdef _DEBUG_
       Serial.print(F("parameters.gotosleep = ")); Serial.println(parameters.gotosleep);
       Serial.print(F("wkp = ")); Serial.println(wkp);
       Serial.print(F("nightMode = ")); Serial.println(nightMode);
       return false;
#endif

  }
  return nightMode;
}

// /**
//    Determines if and how the LEDs should be used
// */
// bool useLed() {
//
//   if ( parameters.led_use == 0 ) return nightMode;  // 0 - depending on Night Mode
//   else if ( parameters.led_use == 1 ) return false; // 1 - always
//
//   return true;
// }


// // 3 Color LED methods
// // -------------------
//
// /**
//    Displays appropriate color on the LED
//
//    @param: aR - Reg LED component. LEDOFF to turn LED color off; LEDSTAY to keep current, LEDON to turn on
//    @param: aG - Green LED component. LEDOFF to turn LED color off; LEDSTAY to keep current, LEDON to turn on
//    @param: aB - Blue LED component. LEDOFF to turn LED color off; LEDSTAY to keep current, LEDON to turn on
// */
// int rgbRed = LEDOFF, rgbGreen = LEDOFF, rgbBlue = LEDOFF;
// void led(int aR = LEDSTAY, int aG = LEDSTAY, int aB = LEDSTAY) {
//   if (aR >= 0) rgbRed = aR;
//   if (aG >= 0) rgbGreen = aG;
//   if (aB >= 0) rgbBlue = aB;
//
//   if (rgbRed < 0) rgbRed = LEDOFF; if (rgbRed > LEDON) rgbRed = LEDON;
//   if (rgbGreen < 0) rgbGreen = LEDOFF; if (rgbGreen > LEDON) rgbGreen = LEDON;
//   if (rgbBlue < 0) rgbBlue = LEDOFF; if (rgbBlue > LEDON) rgbBlue = LEDON;
//
//   if ( useLed() ) {
//     digitalWrite(RGBLED_RED_PIN, LEDOFF);
//     digitalWrite(RGBLED_GREEN_PIN, LEDOFF);
//     digitalWrite(RGBLED_BLUE_PIN, LEDOFF);
//   }
//   else {
//     digitalWrite(RGBLED_RED_PIN, rgbRed <= LEDOFF ? LOW : HIGH );
//     digitalWrite(RGBLED_GREEN_PIN, rgbGreen <= LEDOFF ? LOW : HIGH );
//     digitalWrite(RGBLED_BLUE_PIN, rgbBlue <= LEDOFF ? LOW : HIGH );
//   }
// }
//
// /**
//    Turns all LED colors OFF
// */
// void ledOff() {
//   led(LEDOFF, LEDOFF, LEDOFF);
// }


// Parameter methods
// -----------------

/**
   Loads parameters from EEPROM
*/
void loadParameters() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": loadParameters."));
#endif
  // Let's see if we have the parameters stored already
  // First lets read the token.

  EEPROM.get(0, parameters);
  if (strcmp(CToken, parameters.token) != 0) {      // tokens do not match - load defaults
    // Write down token and defaults
    strncpy(parameters.token, CToken, 7);

    // parameters.high = (byte) STOPWATER;
    // parameters.low = (byte) NEEDWATER;
    // parameters.retries = (byte) RETRIES;
    // parameters.watertime = (byte) WATERTIME;
    // parameters.saturate = (byte) SATURATE;
    parameters.gotosleep = (byte) GOTOSLEEP;
    parameters.wakeup = (byte) WAKEUP;
    parameters.wkendadj = (byte) WEEKENDADJ;
    // parameters.wl_depth = (int) WLDEPTH;
    // parameters.wl_low = (int) WLLOW;

    parameters.is_ap = 0;                           // not an Access Point
    //    parameters.is_ap = 1;                     // Access Point
    strncpy(parameters.ssid, CSsid, 32);
    strncpy(parameters.pwd, CPwd, 65);
    ssid = CDSsid + String( ESP.getChipId() );
    strncpy(parameters.ssid_ap, ssid.c_str(), 32);
    strncpy(parameters.pwd_ap, CDPwd, 65);

    parameters.powersave = false;

    parameters.ping_interval = TICKER;
    parameters.led_use = 0;
    strncpy(parameters.ntp_server, ntpServerName, 32);

    saveParameters();
  }
}

/**
   Saves parameters to the EEPROM
*/
void saveParameters() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": saveParameters."));
  Serial.print(F("Token: ")); Serial.println(parameters.token);
  Serial.print(F("High : ")); Serial.println(parameters.high);
  Serial.print(F("Low  : ")); Serial.println(parameters.low);
  Serial.print(F("Retries: ")); Serial.println(parameters.retries);
  Serial.print(F("WaterTm: ")); Serial.println(parameters.watertime);
  Serial.print(F("Saturate: ")); Serial.println(parameters.saturate);
  Serial.print(F("Gotosleep: ")); Serial.println(parameters.gotosleep);
  Serial.print(F("Wakeup: ")); Serial.println(parameters.wakeup);
  Serial.print(F("WkendAdj: ")); Serial.println(parameters.wkendadj);
  Serial.print(F("WL Depth: ")); Serial.println(parameters.wl_depth);
  Serial.print(F("WL Low: ")); Serial.println(parameters.wl_low);
  Serial.print(F("SSID: ")); Serial.println(parameters.ssid);
  Serial.print(F("PWD: ")); Serial.println(parameters.pwd);
  Serial.print(F("AP SSID: ")); Serial.println(parameters.ssid_ap);
  Serial.print(F("AP PWD: ")); Serial.println(parameters.pwd_ap);
  Serial.print(F("PWRSave: ")); Serial.println(parameters.powersave);
  Serial.print(F("Ping Interval: ")); Serial.println(parameters.ping_interval);
  Serial.print(F("Use LED: ")); Serial.println(parameters.led_use);
  Serial.print(F("NTP Server: ")); Serial.println(parameters.ntp_server);
#endif

  EEPROM.put(0, parameters);
  EEPROM.commit();
}


// Configuration methods
// ---------------------

/**
   Initiates connection to the wireless network of choice
   -or- sets up the requested Access Point mode
   Passes control to connection checking callback method
*/
void cfgInit() {  // Initiate connection

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgInit."));
#endif

  ssid = parameters.ssid;
  pwd = parameters.pwd;

#ifdef _DEBUG_
  Serial.println(F("WiFi parameters: "));
  Serial.print(F("SSID: ")); Serial.println(ssid);
  Serial.print(F("PWD : ")); Serial.println(pwd);

  Serial.println(F("AP parameters: "));
  Serial.print(F("SSID: ")); Serial.println(parameters.ssid_ap);
  Serial.print(F("PWD : ")); Serial.println(parameters.pwd_ap);
#endif

  runningAsAP   = false;      // true if setting up as an Access Point was successful
  connectedToAP = false;      // true if connection to the wireless network of choice was successful

  if (parameters.is_ap == 0) { // If not explicitly requested to be an Access Point
    // attempt to connect to an exiting AP
    initiate_wifi_connection();

    // // flash led green
    // rgbRed = 0; rgbGreen = LEDON; rgbBlue = 0;
    // ledOnDuration = CONNECT_BLINK;
    // ledOffDuration = CONNECT_BLINK;
    // tLedBlink.set(TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ledOnEnable, &ledOnDisable);
    // tLedBlink.enable();

    // check connection periodically
    tConfigure.set(CONNECT_INTRVL, TASK_FOREVER, &cfgChkConnect);
    tConfigure.enableDelayed();

    // set a connection attempts timeout
    tTimeout.set(CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, &connectTOut);
    tTimeout.enableDelayed();

    httpError.setWaiting( HTTP_ERRORS );
    tErrorReset.waitFor( &httpError );
  }
  else {
    // Set the AP parameters and setup the AP mode
    ssid = parameters.ssid_ap;
    pwd = parameters.pwd_ap;
    tConfigure.yield(&cfgSetAP);
  }
}

/**
   Initial WIFI connection to the AP
*/
void initiate_wifi_connection() {
  WiFi.hostname( CHost );
  WiFi.mode( WIFI_STA );
  WiFi.persistent ( true );
  //  yield();
  WiFi.begin( ssid.c_str(), pwd.c_str() );
  yield();
}

/**
   Periodically check if the connection was established
   Re-request connection to AP on every 5th iteration
*/
void cfgChkConnect() {  // Wait for connection to AP

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgChkConnect."));
#endif

  if (WiFi.status() == WL_CONNECTED) {
    disconnectedEventHandler = WiFi.onStationModeDisconnected(&onDisconnected);
    connectedToAP = true;

#ifdef _DEBUG_
    Serial.print(F("Connected to AP. Local ip: "));
    Serial.println(WiFi.localIP());
#endif

    // // flash led green rapidly
    // rgbRed = 0; rgbGreen = LEDON; rgbBlue = 0;
    // ledOnDuration = CONNECT_BLINK / 4;
    // ledOffDuration = ledOnDuration;
    // tLedBlink.set(TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ledOnEnable, &ledOnDisable);
    // tLedBlink.enable();

    // check connection periodically
    tConfigure.set(CONNECT_INTRVL, TASK_FOREVER, &cfgChkNTP);
    tConfigure.restartDelayed();

    // set timeout
    tTimeout.set(NTP_TIMEOUT * TASK_SECOND, TASK_ONCE, &ntpTOut);
    tTimeout.enableDelayed();
  }
  else {
    //    delay(100);
    //    yield();
    if (tConfigure.getRunCounter() % 5 == 0) {

#ifdef _DEBUG_
      Serial.println(F("Re-requesting connection to AP..."));
#endif

      //      wifi_set_phy_mode(PHY_MODE_11G);
      //      WiFi.setOutputPower(20.5);
      //      WiFi.setAutoConnect(false);
      WiFi.disconnect();
      yield();
      initiate_wifi_connection();
    }
  }
}

/**
   Initiates call to NTP server to get current time
*/
void doNtpUpdateInit() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": doNtpUpdateInit."));
  Serial.print(F("connectedToAP="));
  Serial.println(connectedToAP);
#endif

  if ( connectedToAP ) {  // only if connected to wifi network
    // request NTP update
    udp.begin(LOCAL_NTP_PORT);
    if ( WiFi.hostByName(parameters.ntp_server, timeServerIP) ) { //get a random server from the pool

#ifdef _DEBUG_
      Serial.print(F("timeServerIP = "));
      Serial.println(timeServerIP);
#endif

      sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    }
  }
}

/**
   Sends an NTP request to the time server at the given address
*/
unsigned long sendNTPpacket(IPAddress & address)
{

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": sendNTPpacket."));
#endif

  //  Serial.println(F("sending NTP packet..."));
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  yield();
}

/**
   Checks if NTP packet has been received.
   If so, set the time.
   If not, re-request on every 5th iteration
*/
void cfgChkNTP() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgChkNTP."));
#endif
  if (connectedToAP ) {
    if ( tConfigure.isFirstIteration() || tConfigure.getRunCounter() % 5 == 0 ) {
      udp.stop();
      doNtpUpdateInit();
    }
    if ( doNtpUpdateCheck() ) {
      recalcRtcComp();
      doSetTime(epoch);
      time_restore.ntptime = epoch;

#ifdef _DEBUG_
      Serial.println(F("NTP Update successful"));
#endif

      bootTime = now();
      hasNtp = true;

      // tLedBlink.disable();
      tTimeout.disable();
      tConfigure.yieldOnce(&cfgFinish);
      udp.stop();
      isNight();
    }
    else {
      //      delay (100);
      // delay commented out sincce TaskScheduler yields on every pass
    }
  }
}


void recalcRtcComp() {

  if ( hasNtp && rtcCompRequested) {
    long d = (long) getTime() - epoch;
    long D = epoch - time_restore.ntptime;
    rtcComp = time_restore.rtccomp + ( d * 1000000L / D );
    rtcCompRequested = false;

#ifdef _DEBUG_
    Serial.println(F("Recalculated RTC comp"));
    Serial.print(F("d = ")); Serial.println(d);
    Serial.print(F("D = ")); Serial.println(D);
    Serial.print(F("rtc_comp = ")); Serial.println(rtcComp);
#endif
  }
}

#ifdef _DEBUG_
/**
   Print time in the YYYY-MM-DD HH-MM-SS format

   @parap: aTime - time to be printed
*/
void printTime(DateTime aTime) {
  //    DateTime now = rtc.now();

  //    Serial.print(F("UTC date/time: "));
  Serial.print(aTime.year(), DEC);
  Serial.print('-');
  Serial.print(aTime.month(), DEC);
  Serial.print('-');
  Serial.print(aTime.day(), DEC);
  Serial.print(' ');
  Serial.print(aTime.hour(), DEC);
  Serial.print(':');
  Serial.print(aTime.minute(), DEC);
  Serial.print(':');
  Serial.print(aTime.second(), DEC);
  //    Serial.println();

  //    Serial.print(F(" seconds since 1970: "));
  //    Serial.println(now.unixtime());

}
#endif

/**
   Sets the current time on the software RTC and TIME library
*/
void doSetTime(unsigned long aTime) {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": doSetTime."));
#endif

  rtc.adjust( DateTime(aTime) );
  setTime( rtc.now().unixtime() );

#ifdef _DEBUG_
  printTime(now());
  Serial.println();
#endif
}


/**
   Checks if NTP packet has been received durint periodic NTP updates
   If so, set the time.
   If not, re-request on every 5th iteration
*/
bool doNtpUpdateCheck() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": doNtpUpdateCheck."));
#endif

  yield();
  int cb = udp.parsePacket();
  if (cb) {
#ifdef _DEBUG_
    Serial.print(F("Packet received, length="));
    Serial.println(cb);
#endif

    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;

#ifdef _DEBUG_
    // print Unix time:
    Serial.print(F("NTP unix time    = ")); Serial.println(epoch);
    time_t tnow = now();
    Serial.print(F("System unix time = ")); Serial.println(tnow);
#endif
    return (epoch != 0);
  }
  return false;
}

/**
   TIME library service function to periodically adjust the clock
*/
time_t getTime() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": getTime."));
#endif

  return rtc.now().unixtime();
}


/**
   Initiate periodic NTP update.
*/
void ntpUpdate() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": ntpUpdate."));
#endif
  if ( connectedToAP ) {
    if ( tNtpUpdater.isFirstIteration() || tNtpUpdater.getRunCounter() % 5 == 0) {
      udp.stop();
      doNtpUpdateInit();
    }
    if ( doNtpUpdateCheck() ) {
      recalcRtcComp();
      doSetTime(epoch);
      time_restore.ntptime = epoch;
      tNtpUpdater.disable();
      udp.stop();

#ifdef _DEBUG_
      Serial.println(F("NTP update successful"));
#endif

      //  Align ticker period to the respective closk period

      long rest = ts.timeUntilNextIteration(tTicker);
#ifdef _DEBUG_
      Serial.print("Remaining time to tick = "); Serial.println(rest);
#endif
      if (rest > 0) {
        time_t nxttick1 = now() + rest / 1000L;
        time_t ticker = parameters.ping_interval * TASK_MINUTE / 1000L;
        time_t nxttick2 = nxttick1 - nxttick1 % ticker;
        time_t dly = (nxttick2 - now()) * 1000L;
        while (dly < 0)
          dly += ( parameters.ping_interval * TASK_MINUTE );
        tTicker.delay( dly );

#ifdef _DEBUG_
        Serial.print("Original next tick, sec = "); Serial.println(nxttick1);
        Serial.print("Adjusted next tick, sec = "); Serial.println(nxttick2);
        Serial.print("Adjusted delay, ms      = "); Serial.println(dly);
#endif
      }

      if ( !hasNtp ) tTicker.restartDelayed( TASK_SECOND * ADC_SETTLE_TOUT );
      hasNtp = true;
      isNight();
    }
  }
  else {
    tNtpUpdater.disable();
    udp.stop();
  }
}



/**
   Initiate configuration of the device as Soft Access Point
*/
bool globRet;       // Global return code variable
void cfgSetAP() {   // Configure as an Access Point

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgSetAP."));
#endif

  WiFi.disconnect();
  yield();
  wifi_set_phy_mode(PHY_MODE_11G);
  WiFi.setOutputPower(20.5);
  //  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if ( pwd.length() == 0 )
    globRet = WiFi.softAP( ssid.c_str());
  else
    globRet = WiFi.softAP( ssid.c_str(), pwd.c_str() );

  dnsServer.setTTL(300);
  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  dnsServer.start(LOCAL_DNS_PORT, CHost, apIP);
  yield();

  // // flash led blue
  // rgbRed = 0; rgbGreen = LEDON; rgbBlue = LEDON;
  // ledOnDuration = CONNECT_BLINK;
  // ledOffDuration = ledOnDuration;
  // tLedBlink.set(TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ledOnEnable, &ledOnDisable);
  // tLedBlink.enable();

  // check connection periodically
  tConfigure.set(CONNECT_INTRVL, TASK_FOREVER, &cfgChkAP);
  tConfigure.enableDelayed();

  // set timeout
  tTimeout.set(CONNECT_TIMEOUT * TASK_SECOND, TASK_ONCE, &serverTOut);
  tTimeout.enableDelayed();
}

/**
   Checks if AP configuration was successful.
*/
void cfgChkAP() {  // Wait for AP mode to happen

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgChkAP."));
#endif

  if (globRet) {
    // tLedBlink.disable();
    tTimeout.disable();
    tConfigure.yieldOnce(&cfgFinish);

#ifdef _DEBUG_
    Serial.print(F("Running as AP. Local ip: "));
    Serial.println(WiFi.softAPIP());
#endif
    runningAsAP = true;
    clientConnectedEventHandler = WiFi.onSoftAPModeStationConnected(&onClientConnected);
    clientDisconnectedEventHandler = WiFi.onSoftAPModeStationDisconnected(&onClientDisconnected);
  }
}


/**
   Finish device initial configuration:
   - launch web server
   - launch DNS server for AP mode for "plant.io" domain
   - initiate periodic measurement and watering runs
*/
void cfgFinish() {  // WiFi config successful

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": cfgFinish."));
#endif

  // led();                      // flash last status colors for 1 second
  // delay(TASK_SECOND);
  // ledOff();
  // delay(50);
  // led(LEDON, LEDON, LEDON);   // flash white LED indicating end of configuration
  // delay(500);
  // ledOff();

  SPIFFS.begin();

  enableWebServer();

  // Set ticker task according to the parameters interval
  tTicker.setInterval( parameters.ping_interval * TASK_MINUTE );
  tTicker.restartDelayed( TASK_SECOND * ADC_SETTLE_TOUT );  // give watering 10 seconds stabilize ADC after LED use

  tEnforcer.setInterval( parameters.ping_interval * TASK_MINUTE * 2 );

  //  iot_online();                 // report status to IoT engine
}


/**
   Enables web server on port 80 and sets all the event handling methods
*/
void enableWebServer() {

#ifdef _DEBUG_
  Serial.println(F("Starting webserver"));
#endif

  server.on("/", serverHandleRoot);
  server.on("/confnetworksave", HTTP_POST, handleConfnetworksave);
  server.on("/confwatersave", HTTP_POST, handleConfwatersave);
  server.on("/reset", HTTP_POST, resetDevice);
  server.onNotFound(serverHandleNotFound);


  // Code below enables browsing, uploading and downloading files to the internal filesystem.
  // This code has been borrowed from the esp8266 examples


  // =============== FSBrowse code ====================

  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  // ==================================================

  server.begin();

  if ( connectedToAP ) tHandleClients.restart();

#ifdef _DEBUG_
  Serial.println("HTTP server started");
#endif
}

/**
   Disables web server
*/
void disableWebServer () {
#ifdef _DEBUG_
  Serial.println(F("Stopping webserver"));
#endif

  server.stop();  // disable server not to affect measurements
  tHandleClients.disable();
}


/**
  THIS IS THE HEARTBEAT TASK:
  ===========================
  ###########################
  Ticker periodically initiates a sequence of events for watering and iot reporting.

  Current sequence of events:
  1. Run measurments and water if necessary
  2. Report to IoT after watering is done or not required
  3. Update NTP time if connected
  4. Deep sleep after a timeout if requested via parameters
*/

StatusRequest *networkReady;      // a pointer to the Status Request to be used to initiate NTP and IOT updates
// depending on the mode (running as AP or connected to one), all the NTP and IOT
// updates will be waiting for a confirmation that network connectivity is up and running
// In AP mode, only watering should be done since there is no NTP or IOT updates

void ticker() {
#ifdef _DEBUG_
  Serial.println();
  Serial.print(millis());
  Serial.println(F(": TICKER"));
  Serial.println(F("================"));
  Serial.println();
  time_t tnow = myTZ.toLocal( now() );
  Serial.print(F("Current local time: ")); printTime(DateTime(tnow)); Serial.println();
  Serial.print(F("Connected to AP: ")); Serial.println(connectedToAP);
  Serial.print(F("Running   as AP: ")); Serial.println(runningAsAP);

  //  resetDevice();
#endif

  tickTime = now();                     // This is when the tick occurred
  isNight();                            // Update the static nightMode flag

  // to-do: figure out how to disable and re-enable webserver. so far it seems it loses ability to service requests once disabled
  // disableWebServer();

  tMeasureRestart.restart();            // all measurements are initiated. 'wateringDone' event will trigger once all measurements and possiobly water completes
  wateringDone.setWaiting();

  // If connection was lost and onDisconnect event did not catch it, we are going to re-establish connection after watering is done
  // to avoid impacting measurements with high power WiFi operations

  networkReady = &wateringDone;
  if ( parameters.is_ap == 0 && !runningAsAP ) {
    tConnected.waitFor( &wateringDone, TASK_SECOND, CONNECT_TIMEOUT );
    networkReady = tConnected.getInternalStatusRequest();
  }
  //  tPostWater.waitFor( networkReady );   // Task initiating special functions after the watering is done and network re-connected
  tIotReport.waitFor( networkReady );   // server will restart whe IoT report is ready


  if ( !(tTicker.isFirstIteration() && hasNtp) ) { // No need to run another ntp update right after the first one if ntp information was obtained
    tNtpUpdater.waitFor( networkReady, NTPUPDT_INTRVL, NTP_TIMEOUT ); // update NTP
  }

  if ( parameters.powersave ) {
    tSleep.waitForDelayed( networkReady, coldBoot ? CONFIG_DELAY : SLEEP_TOUT, TASK_ONCE );  // use special CONFIG_DELAY sleep delay on the first iteration allocting
    // additional time for configuration updates after reset
    coldBoot = false;
  }
}

/**

*/
void postWaterCallback() {
  enableWebServer();
}

// /**
//   Blinks LED with the predefined color and predefined ON and OFF delays
// */
// bool flip = false;
// void cfgLed() {
//   if (flip) {
//     led();
//     tLedBlink.setInterval( ledOnDuration );
//   }
//   else {
//     ledOnDisable();
//     tLedBlink.setInterval( ledOffDuration );
//   }
//   flip = !flip;
// }
//
// /**
//   Ensures that blinking start with LED ON state
// */
// bool ledOnEnable() {
//   flip = true;
//   return true;
// }
//
// /**
//   Ensures LED is OFF after blinking is disabled
// */
// void ledOnDisable() {
//   digitalWrite(RGBLED_RED_PIN, LEDOFF);
//   digitalWrite(RGBLED_GREEN_PIN, LEDOFF);
//   digitalWrite(RGBLED_BLUE_PIN, LEDOFF);
//   flip = false;
// }

/**
  Connect to AP timeout.
  Tries to set up the device as Access Point
  NOTE: ALWAYS USES THE DEFAULT AP SSID/PASSWORD
*/
void connectTOut() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": connectTOut."));
#endif

  connectedToAP = false;
  WiFi.disconnect();
  ssid = CDSsid + String( ESP.getChipId() );
  pwd = CDPwd;

#ifdef _DEBUG_
  Serial.println( F("Setting up AP:") );
  Serial.print(F("SSID: ")); Serial.println(ssid);
  Serial.print(F("PWD : ")); Serial.println(pwd);
  Serial.print(F("Chip ID: ")); Serial.println(ESP.getChipId());
#endif

  // tLedBlink.disable();
  tConfigure.yield(&cfgSetAP);  // configure as server
}

/**
  NTP update timeout.
  Suppose to turn LED AMBER
*/
void ntpTOut() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": ntpTOut."));
#endif

  // tLedBlink.disable();
  // rgbRed = LEDON; rgbGreen = 182 * (LEDON / 255); rgbBlue = 0;  // configure as yellow
  tConfigure.yieldOnce(&cfgFinish);  // configure as server
}

/**
  Soft AP timeout - at this point device has no connectivity and will reset itsef
  after 2 ticker intervals
*/
void serverTOut() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": serverTOut."));
#endif

  // The main task is still is to water the plant, even if there is no connectivity
  // so we allow some time to do just that

  // tLedBlink.disable();
  tTimeout.disable();

  // rgbRed = LEDON; rgbGreen = LEDOFF; rgbBlue = LEDON;  // configure as yellow
  tConfigure.yieldOnce(&cfgFinish);
  runningAsAP = false;
  tEnforcer.restartDelayed();
}


/**
  This event is called when a station disconnects from wireless network
*/
void onDisconnected(const WiFiEventStationModeDisconnected & event) {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": onDisconnected."));
#endif
  if ( /*!wateringDone.pending()*/true ) {
    initiate_reconnect();
  }
}

/**
   Initiates a task which tries to re-connect to the Access Point in the event of disconnect
*/
void initiate_reconnect() {
  if ( connectedToAP ) {

#ifdef _DEBUG_
    Serial.print(millis());
    Serial.println(F(": initiate_reconnect."));
#endif

    WiFi.disconnect();
    tConnected.restartDelayed(TASK_SECOND);
    connectedToAP = false;
  }
}

/**
  This event is called when a station connects to Soft AP
*/
void onClientConnected(const WiFiEventSoftAPModeStationConnected & event) {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": onClientConnected."));
#endif

  numClients++;
  if ( numClients ) tHandleClients.restart();
}


/**
  This event is called when a station disconnects from Soft AP
*/
void onClientDisconnected(const WiFiEventSoftAPModeStationDisconnected & event) {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": onClientDisconnected."));
#endif

  if (numClients) {
    if ( --numClients == 0 ) tHandleClients.disable();
  }
}

/**
  Attempts to reconnect to wireless network after connection is lost
*/
void connectedChk() {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": connectedChk."));
#endif

  if (WiFi.status() == WL_CONNECTED) {
    connectedToAP = true;
    //    tTimeout.disable();
    tConnected.disable();
    tEnforcer.disable();

#ifdef _DEBUG_
    Serial.print(millis());
    Serial.println(F(": Successfully reconnected"));
#endif

    return;
  }

  if ( tConnected.isFirstIteration() ) {
    connectedToAP = false;
    initiate_wifi_connection();

    //    tSleep.delay();
    return;
  }

  if ( tConnected.getRunCounter() % 5 == 0 ) {
    WiFi.disconnect();
    yield();
    initiate_wifi_connection();
  }

  if ( tConnected.isLastIteration() ) {

#ifdef _DEBUG_
    Serial.print(millis());
    Serial.println(F(": connectedChkTOut."));
#endif

    connectedToAP = false;
    if ( !tEnforcer.isEnabled() ) {
      tEnforcer.restartDelayed();
    }
  }
}

/**
  Deep Sleep callback
  Current watering results and projected ntp time at wake up are stored
  Device is put into deep sleep mode after that
*/
void sleepCallback() {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": sleepCallback."));
#endif

  iot_sleep();

  time_t tnow = now();
  //  time_t ticker = TICKER * TASK_MINUTE / 1000UL;
  time_t ticker = parameters.ping_interval * TASK_MINUTE / 1000UL;
  time_t tdesired = tickTime - tickTime % ticker + ticker;  // align desired time with ticker interval (if ticker is 30 min, align with 30 min on the clock)

  if ( tdesired > tnow ) {

    unsigned long delta = tdesired - tnow;
    if ( delta < ticker / 2 ) {
      delta += ticker;
      tdesired += ticker;
    }

    time_restore.rtcreq = true;
    saveTimeToRTC( tdesired );

    ts.disableAll();
    WiFi.disconnect();
    SPIFFS.end();

#ifdef _DEBUG_
    Serial.println(F("Storing TTimeStored structure to RTC memory"));
    Serial.print(F("Magic number      :")); Serial.println(time_restore.magic);
    Serial.print(F("ttime (desired)   :")); Serial.println(time_restore.ttime);
    Serial.print(F("Epoch time at Tick:")); Serial.println(tickTime);
    Serial.print(F("Epoch time Now    :")); Serial.println(tnow);
    Serial.print(F("Ticket interval   :")); Serial.println(ticker);
    Serial.print(F("Sleep interval    :")); Serial.println(delta);

    Serial.flush();
    delay(10);
    Serial.end();
#endif

    // !-------------------!-------------------------------------------!
    // tick                tnow                                       tick+interval
    // <--- tnow - tick --> <               tick+interval

    ESP.deepSleep( delta * ( 1000000UL + rtcComp ), RF_NO_CAL );
    //    yield();
    //    delay(1000);
  }
  else {
    resetDevice();
  }
}

/**
  Saves time to RTC memory

  @param: aUt UTC timestamp to be saved to RTC memory
*/
void saveTimeToRTC (unsigned long aUt) {
  time_restore.magic = MAGIC_NUMBER;
  time_restore.ccode = ~time_restore.magic;
  time_restore.ttime = aUt;
  time_restore.hasntp = hasNtp;
  //  time_restore.ntptime = getTime();
  time_restore.rtccomp = rtcComp;
  byte *p = (byte*) &time_restore;
  //  ESP.rtcUserMemoryWrite( 64, (uint32_t*) p, sizeof(TTimeStored) );
  ESP.rtcUserMemoryWrite( 0, (uint32_t*) p, sizeof(TTimeStored) );
  // p = (byte*) &water_log;
  //  ESP.rtcUserMemoryWrite( 128, (uint32_t*) p, sizeof(TWaterLog) );
  // ESP.rtcUserMemoryWrite( 64, (uint32_t*) p, sizeof(TWaterLog) );
}

/**
  Resets the device
  Current watering results and requested ntp time at wake up are stored
*/
void resetDevice() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": resetDevice."));
#endif

  time_restore.rtcreq = false;
  saveTimeToRTC(now());

#ifdef _DEBUG_
  Serial.println(F("Storing TTimeStored structure to RTC memory"));
  Serial.print(F("Magic number      :")); Serial.println(time_restore.magic);
  Serial.print(F("ttime (desired)   :")); Serial.println(time_restore.ttime);
  Serial.print(F("Epoch time at Tick:")); Serial.println(tickTime);
  Serial.print(F("Epoch time Now    :")); Serial.println(now());

  Serial.flush();
  delay(10);
  Serial.end();
#endif

  ts.disableAll();
  WiFi.disconnect();
  SPIFFS.end();
  ESP.reset(); // If device cannot reconnect to the AP, it is reset to try again and/or become an Access Point
  //  delay(100);
}


/**
   Checks if device is running in the correct connectivity mode and resets it otherwise
*/
void connectionEnforcer() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": connectionEnforcer."));
#endif

  if ( parameters.is_ap == 0 && !connectedToAP ) { // device should be connected to the WiFi

#ifdef _DEBUG_
    Serial.println(F("Device should be connected to the WiFi. Resetting."));
#endif

    resetDevice();
  }

  if ( parameters.is_ap == 1 && !runningAsAP ) { // device should be running in local AP mode

#ifdef _DEBUG_
    Serial.println(F("Device should be running in local AP mode. Resetting."));
#endif

    resetDevice();
  }
}

// // Water section
// // =============
//
// /**
//   Initialize average filter at the beginning of the measurement
// */
// bool measureWLOnEnable() {
//   wl.initialize();
//   return true;
// }
//
// /**
//   Initiate water level measurement with a timeout of 4 milliseconds (or ~ XX cm)
//   Allows collection of minimal necessary samples to properly average the waterlevel
// */
// void measureWL() {
//
// #ifdef _DEBUG_
//    Serial.print(millis());
//    Serial.println(F(": measureWL.));
// #endif
//
//   //  pulseComplete.setWaiting();
//   ping( PING_TIMEOUT );
//
//   //  tWaterLevel.waitFor( tPing.getInternalStatusRequest() );
//   if (tMesrLevel.getRunCounter() == (2 * WL_SAMPLES + 1) ) measurementsReady.signal();
// }
//
// /**
//   Measured current humidity based on the ADC reading and updates the average filtered value
// */
// // Measurment and Watering
// #define HUM_SAMPLES  5
// long  humData[HUM_SAMPLES];
// avgFilter hum(HUM_SAMPLES, humData);
//
// long measureHumidity()
// {
//   long r, l;
//
//   l = analogRead( MOISTURE_PIN );
//
// #ifdef _DEBUG_
//   //  Serial.print(millis());
//   //  Serial.print(F(": measureHumidity. l = "));
//   //  Serial.println(l);
// #endif
//
//   if (l > SENSOR_OUT_VALUE) return 0;
//
//   r = (1023 - l) * 100L / 1023L;
// #ifdef _DEBUG_
//   //  Serial.print(F("% = "));
//   //  Serial.println(r);
// #endif
//   return hum.value(r);
// }
//
// /**
//   Initialize average filter at the beginning of the measurement
// */
// bool measureMSOnEnable() {
//   hum.initialize();
//   probePowerOn();
//   return true;
// }
//
// /**
//   When Soil humidity measurement finishes, turns off power on the humidity probe, signals that probe is idle and disables water level measurement
// */
// void measureMSOnDisable() {
//   probePowerOff();
//   probeIdle.signal();
//   tMesrLevel.disable();
// }
//
//
// /**
//   Primes the humidity probe for 1 second
//   then signals that humidity measurement value is ready
// */
// void measureMS() {
//
//   // One second to "warm up" the probe
  // if ( /*tMesrMoisture.isFirstIteration()*/false ) {
//     probeIdle.setWaiting();
    // tMeasureRestart.waitFor(&probeIdle);
//
//     //    hum.initialize();
//     tMesrMoisture.setInterval( TASK_SECOND / 2 );     // set measurement interval at half a second
//     tMesrMoisture.delay( TASK_SECOND );
//     return;
  // }
//
//   // then a number of measuments every 500 ms to collect an average
//   if ( tMesrMoisture.getRunCounter() <= ( 2 * HUM_SAMPLES + 1) )
//     currentHumidity = measureHumidity();
//
//   // on the last iteration signal that measurement is ready and disable self
//   if ( tMesrMoisture.getRunCounter() == ( 2 * HUM_SAMPLES + 1 ) ) {
//     measurementsReady.signal();
//     tMesrMoisture.disable();
//   }
// }
//
//
// /**
//   Main measure callback which decides whether watering should occur or stop
// */
// void measureCallback() {
// #ifdef _DEBUG_
//   Serial.print(millis());
//   Serial.println(F(": measureCallback."));
//   Serial.print(F("canWater = "));
//   if (canWater())
//     Serial.println(F("yes"));
//   else
//     Serial.println(F("no"));
//   Serial.print(F("currentHumidity = "));
//   Serial.println(currentHumidity);
//   Serial.print(F("currentWaterLevel = "));
//   Serial.println(currentWaterLevel);
//   Serial.print(F("Watering is "));
//   if (tWater.isEnabled())
//     Serial.println(F("enabled"));
//   else
//     Serial.println(F("disabled"));
// #endif
//
//   // check if we can water now and not doing it already
//   if (canWater() && currentHumidity > 0 && currentHumidity < parameters.low) {
//     if (!tWater.isEnabled()) {
//       tWater.setInterval( parameters.watertime * TASK_MINUTE );
//       tWater.setIterations( parameters.retries * 2 );
//       tWater.restartDelayed(ADC_SETTLE_TOUT * TASK_SECOND);
//       iot_report();
//     }
//     return;
//   }
//
//   // check if we are watering (and maybe should stop)
  // if ( /*tWater.isEnabled()*/false ) {
//     if ( !canWater() || currentHumidity >= parameters.high ) {
//       tWater.disable();
//     }
//   }
//   else {
    // tMeasureRestart.disable();
//     ledOff();
    // wateringDone.signal();
//   }
// }
//
// /**
//   Measurement restart method
//   Restarts measurements when previous measurements are ready
//   Since pump every pump run draws current, it may affect accuracy of the soil humidity measurement,
//   therefore no measurements takes place during the actual watering run when pump is active
// */
void measureRestart() {
  if ( /*tWater.isEnabled() && ( tWater.getRunCounter() & 1 )*/true ) {  // Do not measure durint watering run - brown-outs affect accuracy
    tMeasureRestart.restartDelayed( TASK_SECOND );
  }
//   else {
    // measurementsReady.setWaiting(2);
    // tMeasure.waitFor(&measurementsReady);
//
//     tMesrLevel.restart();
//     tMesrMoisture.restart();
//   }
// }
//
// /**
//   Main watering method - decides how long the watering and saturation runs should take
// */
// void waterCallback() {
//
// #ifdef _DEBUG_
//   Serial.print(millis());
//   Serial.println(F(": waterCallback."));
// #endif
//
//   if (tWater.getRunCounter() & 1) {
//     // during odd runs the pump needs to be turned on, and an even run configured to perform saturation
//     motorOn();
//     water_log.num_runs++;
//     tWater.setInterval( parameters.watertime * TASK_SECOND );
//
//     rgbRed = LEDOFF; rgbGreen = LEDOFF; rgbBlue = LEDON;
//     ledOnDuration = TASK_SECOND / 4;
//     ledOffDuration = ledOnDuration / 2;
//     tLedBlink.set(TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ledOnEnable, &ledOnDisable);
//     tLedBlink.enable();
//     tLeakChecker.setInterval(TASK_SECOND);
//   }
//   else { // even run
//     motorOff();
//     tWater.setInterval( parameters.saturate * TASK_MINUTE);
//
//     ledOff();
//     rgbRed = LEDOFF; rgbGreen = LEDOFF; rgbBlue = LEDON;
//     ledOnDuration = TASK_SECOND * 2;
//     ledOffDuration = TASK_SECOND / 4;
//     tLedBlink.set(TASK_IMMEDIATE, TASK_FOREVER, &cfgLed, &ledOnEnable, &ledOnDisable);
//     tLedBlink.enable();
//     tLeakChecker.setInterval(TASK_SECOND * 10);
//   }
}
//
// /**
//   Start of watering. Makes sure device is allowed to water and initiates the log entry
// */
// bool waterOnEnable() {
//
//   if ( !canWater() ) {
//     motorOff();
//     return false;
//   }
//
//   water_log.water_start = now();
//   water_log.hum_start = currentHumidity;
//   water_log.num_runs = 0;
//   water_log.run_duration = parameters.watertime;
//   water_log.wl_start = currentWaterLevel;
//
//   water_log.water_end = 0;
//   water_log.hum_end = 0;
//   water_log.wl_end = 0;
//
//   tLeakChecker.restart();
//
//   return true;
// }
//
// /**
//   Stop of watering. Turns the pump off. Makes sure log entry is completed
// */
//
// const char* logFileName = "/watering.log";
// void waterOnDisable() {
//   motorOff();
//
//   water_log.water_end = now();
//   water_log.hum_end = currentHumidity;
//   water_log.wl_end = currentWaterLevel;
//
//   tMeasure.disable();
  // tMeasureRestart.disable();
//   tLedBlink.disable();
  // wateringDone.signal();
//   tLeakChecker.disable();
//
//   if ( !SPIFFS.exists(logFileName) ) {
//     File f = SPIFFS.open(logFileName, "w");
//     f.println("Start time\tStart soil humidity,%\tStart water level,mm\tRuns\tRun duration,sec\tStop time\tStop soil humidity,%\tStop water level,mm");
//     f.close();
//   }
//   File f = SPIFFS.open(logFileName, "a");
//   if (f) {
//     char buf[128];
//     snprintf(buf, 128, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", water_log.water_start, water_log.hum_start, water_log.wl_start, water_log.num_runs, water_log.run_duration, water_log.water_end, \
//              water_log.hum_end, water_log.wl_end );
//     f.println(buf);
//     f.close();
//   }
// }
//
//
// void waterleakChecker() {
//   if ( !canWater() )
//     tWater.disable();
// }


// SERVER CODE
// ===========

/**
   Part of the wifiwebserver code - handles client connections and requests
*/
void handleClientCallback() {

#ifdef _DEBUG_
  //      Serial.print(millis());
  //      Serial.println(F(": handleClientCallback."));
#endif

  if ( runningAsAP ) dnsServer.processNextRequest();
  server.handleClient();
  if ( server.client() ) {
    //tHandleClients.forceNextIteration();
    long d = ts.timeUntilNextIteration (tSleep);

#ifdef _DEBUG_
    Serial.print(millis());
    Serial.print(F(": handleClientCallback. Rem. time to sleep = ")); Serial.println(d);
#endif

    if ( d > 0 && d < SLEEP_TOUT_CONN )
      tSleep.delay(SLEEP_TOUT_CONN);
  }
  else {
    tHandleClients.delay();
  }
}
int indexParser (int aTag, char* buf, int len);
void parseFile( char* aFileName, int (*aParser)(int aTag, char* aBuffer, int aSize) );

/**
   Hanles request for the webserver root folder
*/
void serverHandleRoot() {

#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": serverHandleRoot."));
  //
  //  SPIFFS.begin();
  //  {
  //    Dir dir = SPIFFS.openDir("/");
  //    while (dir.next()) {
  //      String fileName = dir.fileName();
  //      size_t fileSize = dir.fileSize();
  //      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), String(fileSize).c_str());
  //    }
  //    Serial.printf("\n");
  //  }
#endif

  isNight();
  // led(LEDON, LEDON , LEDON);
  parseFile( "/index.htm", &indexParser);
  // ledOff();
}


/**
   Parses html file using designated parser routine
   The parser routine is looking for a specially formatted html comment: "<!-- ###NNNN-name###-->
   Parser replaces the line next to the tag with appropriate html stream and returns
   number of lines of the HTML file to skip as a results
*/
const char* CReplaceTag = "<!-- ###"; //
const int   CRLen = 8;

//#define BUFFER_SIZE   512
#define BUFFER_SIZE   1024

void parseFile( char* aFileName, int(*aParser)(int aTag, char* aBuffer, int aSize) ) {

  if (SPIFFS.exists(aFileName)) {
    char buf[BUFFER_SIZE + 2];
    int len;

    File file = SPIFFS.open(aFileName, "r");

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    int skipNext = 0;
    while (file.available()) {

      if ( ( len = file.readBytesUntil('\n', buf, BUFFER_SIZE) ) ) {
        buf[len] = 0;

        if ( skipNext > 0 ) {   // this is where we skip the original html lines as instructed by the parser
          skipNext--;
          continue;
        }

#ifdef _DEBUG_
        //        Serial.print(F("Current html line: "));
        //        Serial.print(len);
        //        Serial.print(" : ");
        //        Serial.println(buf);
#endif

        // Tag is: |<!-- ###0001-blah blah blah|
        //          0123456789012345
        if ( strncmp(CReplaceTag, buf, CRLen) == 0 ) {            // found special tag - let's identify tag number (always 4 characters)

#ifdef _DEBUG_
          //          Serial.println(F("Found the '<!-- ###' tag."));
          //          Serial.print(F("Current html line: "));
          //          Serial.print(len);
          //          Serial.print(" : ");
          //          Serial.println(buf);
#endif

          buf[CRLen + 4] = 0;                                     // tag id number should always be 4 characters
          int tag = String(&buf[CRLen]).toInt();

#ifdef _DEBUG_
          //          Serial.print(F("Extracted tag = "));
          //          Serial.println(tag);
#endif
          skipNext = (*aParser)(tag, buf, BUFFER_SIZE);

#ifdef _DEBUG_
          //          Serial.print(F("Parser = "));
          //          Serial.println(buf);
          //          Serial.print(F("skipNext = "));
          //          Serial.println(skipNext);
#endif
        }
        server.sendContent(buf);
        yield();
      }
    }
    file.close();
  }
}

/**
   Parser for index.htm file
*/
int indexParser (int aTag, char* buf, int len) {
  time_t tnow;

  switch (aTag) {
    case 1: //0001-DATETIME
      tnow = myTZ.toLocal( now() );
      snprintf( buf, len, "%04d-%02d-%02d %02d:%02d:%02d", year(tnow), month(tnow), day(tnow), hour(tnow), minute(tnow), second(tnow) );
      break;

    case 2: //0002-SYSTEMSTATUS
      if ( /*tWater.isEnabled()*/false ) {
        if ( /*tWater.getRunCounter() & 1*/false) {
          snprintf( buf, len, "Watering");
        }
        else {
          snprintf( buf, len, "Saturating");
        }
      }
      else {
        snprintf( buf, len, "Idle");
      }
      break;

    case 3: //0003-SOILHUMIDITY
      // snprintf( buf, len, "%d%", currentHumidity);
      break;

    case 4: //0004-LEAKSTATUS
      if ( /*hasLeaked()*/false ) {
        snprintf( buf, len, "Yes");
      }
      else {
        snprintf( buf, len, "No");
      }
      break;

    case 5: //0005-WATERLEVEL
      // snprintf( buf, len, "%d mm", currentWaterLevel);
      break;

    case 14: //0014-LWSTARTTIME
      if ( /*water_log.water_start*/false ) {
        // tnow = myTZ.toLocal( water_log.water_start );
        // snprintf( buf, len, "%04d-%02d-%02d %02d:%02d:%02d", year(tnow), month(tnow), day(tnow), hour(tnow), minute(tnow), second(tnow) );
      }
      else
        buf[0] = 0;
      break;

    case 15: //0015-LWSTARTHUM
      if ( /*water_log.water_start*/false ) {
        // snprintf( buf, len, "%d%", water_log.hum_start);
      }
      else
        buf[0] = 0;
      break;

    case 16: //0016-LWSTARTLVL
      if ( /*water_log.water_start*/false ) {
        // snprintf( buf, len, "%d mm", water_log.wl_start);
      }
      else
        buf[0] = 0;
      break;

    case 17: //0017-LWRUNS
      if ( /*water_log.water_end*/false ) {
        // snprintf( buf, len, "%d", water_log.num_runs);
      }
      else
        buf[0] = 0;
      break;

    case 18: //0018-LWENDTIME
      if ( /*water_log.water_end*/false ) {
        // tnow = myTZ.toLocal( water_log.water_end );
        // snprintf( buf, len, "%04d-%02d-%02d %02d:%02d:%02d", year(tnow), month(tnow), day(tnow), hour(tnow), minute(tnow), second(tnow) );
      }
      else
        buf[0] = 0;
      break;

    case 19: //0019-LWENDHUM
      if ( /*water_log.water_end*/false ) {
        // snprintf( buf, len, "%d%", water_log.hum_end);
      }
      else
        buf[0] = 0;
      break;

    case 20: //0020-LWENDLVL
      if ( /*water_log.water_end*/false ) {
        // snprintf( buf, len, "%d mm", water_log.wl_end);
      }
      else
        buf[0] = 0;
      break;

    case 21: //0021-RUNAS
      if ( connectedToAP )
        snprintf( buf, len, "Wireless Client. ");
      else
        snprintf( buf, len, "Access Point. ");
      break;

    case 22: //0022-LOCALIP
      if ( connectedToAP )
        snprintf( buf, len, "%s", WiFi.localIP().toString().c_str() );
      else
        snprintf( buf, len, "%s", WiFi.softAPIP().toString().c_str() );
      break;
      break;

    default:
      buf[0] = 0;
      return 0;
  }
  return 1;
}

/**
   Parser for water parameters page
*/
const char *CHtmlInputText = "<input type=\"text\" value=\"%d\"";
int waterParser (int aTag, char* buf, int len) {
  time_t tnow;

  switch (aTag) {
    case 1: //0001-DATETIME
      tnow = myTZ.toLocal( now() );
      snprintf( buf, len, "%04d-%02d-%02d %02d:%02d:%02d", year(tnow), month(tnow), day(tnow), hour(tnow), minute(tnow), second(tnow) );
      break;

    case 6: //0006-NEEDWATER
      // snprintf( buf, len, CHtmlInputText, parameters.low);
      break;

    case 7: //0007-WATERTIME
      // snprintf( buf, len, CHtmlInputText, parameters.watertime);
      break;

    case 8: //0008-STOPWTER
      // snprintf( buf, len, CHtmlInputText, parameters.high);
      break;

    case 9: //0009-SATURATE
      // snprintf( buf, len, CHtmlInputText, parameters.saturate);
      break;

    case 10: //0010-RETRIES
      // snprintf( buf, len, CHtmlInputText, parameters.retries);
      break;

    case 11: //0011-GOTOSLEEP
      snprintf( buf, len, CHtmlInputText, parameters.gotosleep);
      break;

    case 12: //0012-WAKEUP
      snprintf( buf, len, CHtmlInputText, parameters.wakeup);
      break;

    case 13: //0013-WEEKENDADJ
      snprintf( buf, len, CHtmlInputText, parameters.wkendadj);
      break;

    case 20: //0020-WLDEPTH
      // snprintf( buf, len, CHtmlInputText, parameters.wl_depth);
      break;

    case 21: //0021-MINLEVEL
      // snprintf( buf, len, CHtmlInputText, parameters.wl_low);
      break;

    case 22: //0022-PING
      snprintf( buf, len, CHtmlInputText, parameters.ping_interval);
      break;

    default:
      buf[0] = 0;
      return 0;
  }
  return 1;
}

/**
   Parser for network parameters page
*/
int networkParser (int aTag, char* buf, int len) {
  time_t tnow;
  String s;

  switch (aTag) {
    case 1: //0001-DATETIME
      tnow = myTZ.toLocal( now() );
      snprintf( buf, len, "%04d-%02d-%02d %02d:%02d:%02d", year(tnow), month(tnow), day(tnow), hour(tnow), minute(tnow), second(tnow) );
      break;

    case 2: //0002-CONFIG
      //    <option value="apoint">Access Point</option><option value="client" selected>Wireless Network</option>
      s = "<option value=\"apoint\"";
      if ( parameters.is_ap ) s += " selected";
      s += ">Access Point</option><option value=\"client\"";
      if ( !parameters.is_ap ) s += " selected";
      s += ">Wireless Network</option>";
      snprintf( buf, len, "%s", s.c_str());
      break;

    case 3: //0003-POWER
      //    <option value="on">Always On</option><option value="save" selected>Power Saving</option>
      s = "<option value=\"on\"";
      if ( !parameters.powersave ) s += " selected";
      s += ">Always On</option><option value=\"save\"";
      if ( parameters.powersave ) s += " selected";
      s += ">Power Saving</option>";
      snprintf( buf, len, "%s", s.c_str());
      break;

    case 10: //0010-NTPSEERVER
      //    <input type="text" value="ntp.nist.gov"
      snprintf( buf, len, "<input type=\"text\" value=\"%s\"", parameters.ntp_server);
      break;

    case 11: //0011-LEDUSE
      //    <option value="0" selected>Daylight</option><option value="1">Always</option><option value="2">Never</option>
      s = "<option value=\"0\"";
      if ( parameters.led_use == 0 ) s += " selected";
      s += ">Daylight</option><option value=\"1\"";
      if ( parameters.led_use == 1 ) s += " selected";
      s += ">Always</option><option value=\"2\"";
      if ( parameters.led_use == 2 ) s += " selected";
      s += ">Never</option>";
      snprintf( buf, len, "%s", s.c_str());
      break;

    case 4: //0004-SSID
      //    <input type="text" value="your wifi network ssid"
      snprintf( buf, len, "<input type=\"text\" value=\"%s\"", parameters.ssid);
      break;

    case 5: //0005-PWD
      //    <input type="text" value="your wifi password"
      snprintf( buf, len, "<input type=\"text\" value=\"%s\"", parameters.pwd);
      break;

#ifdef _DEBUG_
    case 6: //0006-INFO
      s = ESP.getResetInfo();
      snprintf( buf, len, "%s\n magic no=%lu, unixtime=%lu\n", s.c_str(), time_restore.magic, time_restore.ttime);
      break;
#endif

    case 7: //0004-SSID
      //    <input type="text" value="your wifi network ssid"
      snprintf( buf, len, "<input type=\"text\" value=\"%s\"", parameters.ssid_ap);
      break;

    case 8: //0005-PWD
      //    <input type="text" value="your wifi password"
      snprintf( buf, len, "<input type=\"text\" value=\"%s\"", parameters.pwd_ap);
      break;

    default:
      buf[0] = 0;
      return 0;
  }
  return 1;
}

/**
   Handler for all requests without dedicated event handlers
   Regular file streaming (images) is supported via this method
*/
void serverHandleNotFound() {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": serverHandleNotFound."));
  Serial.print(F("Server URI: ")); Serial.println(server.uri());
#endif

  isNight();
  // led(LEDON, LEDON, LEDON);

  if ( server.uri() == "/index.htm" )   parseFile( "/index.htm", &indexParser);
  else if ( server.uri() == "/confwater.htm" )   parseFile( "/confwater.htm", &waterParser);
  else if ( server.uri() == "/confnetwork.htm" ) parseFile( "/confnetwork.htm", &networkParser);
  else if ( !handleFileRead( server.uri() ) ) {
    String message = "Page Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
  }

  // ledOff();
}

/**
   Read file from the filesystem
*/
bool handleFileRead(String path) {

  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  bool result = false;

  isNight();
  // led(LEDON, LEDON, LEDON);

  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();

    if ( path == "/restart.jpg" )  pageLoaded.signalComplete();
    result = true;
  }

  // ledOff();

  return result;
}

/**
   Copy parameters

   @param s: pointer to the spource parameter structure
   @param d: pointer to the destination parameter structure
*/
void cpParams( byte * s, byte * d ) {
  for (int i = 0; i < sizeof(TParameters); i++, s++, d++) {
    *d = *s;
  }
}

/**
   Handle updates to the3 network configuration
*/
const char* CSaved = "<html><head></head><body><script>window.alert(\"Parameters saved\");window.location.href='/index.htm';</script></body></html>";
const char* CRestart = "<html><head></head><body><script>window.alert(\"Parameters saved\");window.location.href='/confrestart.htm';</script></body></html>";
void handleConfnetworksave() {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": handleConfnetworksave."));
  Serial.println(server.uri());
  Serial.print("Server args = "); Serial.println(server.args());
  for (int i = 0; i < server.args(); i++) {
    Serial.print(server.argName(i)); Serial.print(" = "); Serial.println(server.arg(i));
  }
#endif

  isNight();
  // led(LEDON, LEDON, LEDON);

  if ( server.args() == 9 && server.uri() == "/confnetworksave" ) {

    pageLoaded.setWaiting();
    tReset.waitFor(&pageLoaded);

    TParameters temp;
    cpParams( (byte*) &parameters, (byte*) &temp );

    temp.is_ap = ( server.arg("config") == "apoint" );
    temp.powersave = ( server.arg("power") == "save" );
    strncpy(temp.ssid, server.arg("ssid").c_str(), 32);
    strncpy(temp.pwd, server.arg("pwd").c_str(), 65);
    strncpy(temp.ssid_ap, server.arg("ssidap").c_str(), 32);
    strncpy(temp.pwd_ap, server.arg("pwdap").c_str(), 65);

    strncpy(temp.ntp_server, server.arg("ntp").c_str(), 32);
    temp.led_use = server.arg("leds").toInt();

    if ( true ) { // Replace with real validation
      cpParams( (byte*) &temp, (byte*) &parameters );
      saveParameters();
    }

    server.send(200, "text/html", CRestart);
  }
  else {
    server.send(500, "text/plain", F("Invalid Arguments"));
  }

  // ledOff();
}

/**
   Handle updates to watering parameters
*/
void handleConfwatersave() {
#ifdef _DEBUG_
  Serial.print(millis());
  Serial.println(F(": handleConfwatersave."));
  Serial.println(server.uri());
  Serial.print("Server args = "); Serial.println(server.args());
  for (int i = 0; i < server.args(); i++) {
    Serial.print(server.argName(i)); Serial.print(" = "); Serial.println(server.arg(i));
  }
#endif

  TParameters temp;
  cpParams( (byte*) &parameters, (byte*) &temp );

  isNight();
  // led(LEDON, LEDON, LEDON);

  if ( server.args() == 12 && server.uri() == "/confwatersave" ) {

    // temp.low = server.arg("needwater").toInt();
    // temp.watertime = server.arg("watertime").toInt();
    // temp.high = server.arg("stopwater").toInt();
    // temp.saturate = server.arg("saturate").toInt();
    // temp.retries = server.arg("retries").toInt();
    temp.gotosleep = server.arg("gotosleep").toInt();
    temp.wakeup = server.arg("wakeup").toInt();
    temp.wkendadj = server.arg("weekendadj").toInt();
    // temp.wl_depth = server.arg("wldepth").toInt();
    temp.ping_interval = server.arg("ping").toInt();

    // ======================================
    //  BIG TO DO: Parameter validation
    // ======================================

    if ( temp.ping_interval < 5 ) temp.ping_interval = 5; // no less than 5, minutes
    if ( temp.ping_interval > 60 ) temp.ping_interval = 60; // no more than 1 hour (max sleep time for esp8266 is ~72 minutes)


    if ( true ) { // replace with parameter validation result
      cpParams( (byte*) &temp, (byte*) &parameters );
      saveParameters();

      server.send(200, "text/html", CSaved);
      isNight();
      tTicker.setInterval( parameters.ping_interval * TASK_MINUTE );
    }
    else {
      server.send(500, "text/plain", F("Invalid Parameters"));
    }
  }
  else {
    server.send(500, "text/plain", F("Invalid Arguments"));
  }

  // ledOff();
}


/**
   Identify correct content type by file extension
*/
String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".log")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}


// Startup code
// ============

/**
   Configure pins INPUT and OUTPUT appropriately
*/
void configurePins() {
#ifdef _DEBUG_
  //  Serial.print(millis());
  //  Serial.println(F(": configurePins."));
  //  Serial.flush();
#endif

  //  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);  // Set D3 up
  //  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);  // Set D4 up

  // pinMode(PUMP_PWR_PIN, OUTPUT); digitalWrite(PUMP_PWR_PIN, LOW);

  // pinMode(MOISTURE_PIN, INPUT);
  // pinMode(MOISTURE_PWR_PIN, OUTPUT); digitalWrite(MOISTURE_PWR_PIN, LOW);

  // pinMode(RGBLED_RED_PIN, OUTPUT); digitalWrite(RGBLED_RED_PIN, LOW);
  // pinMode(RGBLED_GREEN_PIN, OUTPUT); digitalWrite(RGBLED_GREEN_PIN, LOW);
  // pinMode(RGBLED_BLUE_PIN, OUTPUT); digitalWrite(RGBLED_BLUE_PIN, LOW);

  //  Pins for Dexter US sensor are flipped between INPUT and OUTPUT and
  //  therefore are initialized during ping()
  //  pinMode(SR04_ECHO_PIN, INPUT);
  //  pinMode(SR04_TRIGGER_PIN, OUTPUT); digitalWrite(SR04_TRIGGER_PIN, LOW);

  // pinMode(LEAK_PIN, OUTPUT); digitalWrite(LEAK_PIN, LOW);
}

/**
   This is the main setup() method called at the startup

   S E T U P

*/
void setup() {
#ifdef _DEBUG_
  // Serial.begin(74880);
  Serial.begin(115200);
  //  delay(5000);
  initBMP280();
#endif

  configurePins();

  EEPROM.begin(sizeof(parameters)/* + sizeof(water_log)*/);
  loadParameters();

  rtc.begin();
  rtc.adjust( DateTime(STATIC_TIME) );
  setSyncProvider(&getTime);   // the function to get the time from the RTC

  // wl.initialize();
  // hum.initialize();

  rtcComp      = 0;
  rtcCompRequested = false;
  time_restore.ntptime = 0;   // this field will only be populated in case of sleep

  hasNtp = false;
  coldBoot = true;
  if ( ESP.getResetReason() != CWakeReasonReset ) {
    byte *p = (byte *) &time_restore;
    //    if ( ESP.rtcUserMemoryRead( 64, (uint32_t*) p, sizeof(TTimeStored) )) {
    if ( ESP.rtcUserMemoryRead( 0, (uint32_t*) p, sizeof(TTimeStored) )) {


#ifdef _DEBUG_
      Serial.println(F("Read TTimeStored structure from RTC memory"));
      Serial.print(F("Magic number:")); Serial.println(time_restore.magic);
      Serial.print(F("ttime number:")); Serial.println(time_restore.ttime);
      Serial.print(F("has NTP     :")); Serial.println(time_restore.hasntp);
      Serial.print(F("rtc_comp    :")); Serial.println(time_restore.rtccomp);
#endif

      if ( time_restore.magic == MAGIC_NUMBER && time_restore.ccode == ~(MAGIC_NUMBER) ) {
        coldBoot = false;

        doSetTime(time_restore.ttime + WAKE_DELAY);
        hasNtp = time_restore.hasntp;
        rtcComp = time_restore.rtccomp;
        rtcCompRequested = time_restore.rtcreq;

        // p = (byte *) &water_log;
        //        ESP.rtcUserMemoryRead( 128, (uint32_t*) p, sizeof(TWaterLog) );
        // ESP.rtcUserMemoryRead( 64, (uint32_t*) p, sizeof(TWaterLog) );

#ifdef _DEBUG_
        Serial.println(F("Time and Water log restored from RTC memory"));
#endif

        if ( hasNtp && /*isNight()*/false ) {  // if this is night, go right back to sleep
          tickTime = now();
          sleepCallback();
        }
      }
    }
  }
  else {

#ifdef _DEBUG_
    Serial.println(F("Erased config set AutoConnect to false"));
#endif

    ESP.eraseConfig();
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
  }

  checkUpdateTime();
  setSyncInterval(3600);      // Sync time every 1 hour with RTC

#ifdef _DEBUG_
  Serial.println(F("IoT Plant Watering System"));
#endif

  if ( !isNight() ) {
    //    for (int i = 0; i < 3; i++) {
    // led(LEDON, LEDOFF, LEDOFF); delay(500);
    // led(LEDOFF, LEDON, LEDOFF); delay(500);
    // led(LEDOFF, LEDOFF, LEDON); delay(500);
    // ledOff(); delay(500);
  }

  // Indicate TEST mode by lighting upu LED pure white for 5 seconds
#ifdef _DEBUG_
  // led(LEDON, LEDON, LEDON);
  // Serial.println(F("_DEBUG_ - lights out"));
  // delay(5000);
#endif

  // ledOff();

  ts.startNow();
}

/**
   Main loop. When using TaskScheduler only execute() method
   should be located there
*/
void loop() {
  ts.execute();
}

//#ifdef _DEBUG_
// ====================== FSBrowse code ========================

/**
   This code is borrowed from the FSBrowse example (esp8266 library examples)
   and is used to upload updated HTML files directly to esp's file system,
   and download the watering log
*/


File fsUploadFile;
void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}
void handleFileCreate() {
  if (server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if (file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileDelete() {
  if (server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if (path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if (!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileUpload() {
  if (server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

//#endif
//==========================================================================




#ifdef _IOT_BLYNK_

// IoT Blynk specific http aREST code
// ==================================


/**
   http request method for sending REST requests
*/
WiFiClient blynk_client;

bool httpRequest(const String & method,
                 const String & request,
                 String &       response)
{
#ifdef _DEBUG_
  Serial.print(F("Connecting to "));
  Serial.print(blynk_host);
  Serial.print(":");
  Serial.print(blynk_port);
  Serial.print("... ");
#endif
  if (blynk_client.connect(blynk_host, blynk_port)) {

#ifdef _DEBUG_
    Serial.println("OK");
#endif

    httpError.setWaiting( HTTP_ERRORS );
  } else {

#ifdef _DEBUG_
    Serial.println("failed");
#endif

    httpError.signal();
    return false;
  }

#ifdef _DEBUG_
  Serial.print("request: ");
  Serial.println(method + request);
#endif

  blynk_client.print(method); blynk_client.println(F(" HTTP/1.1"));
  blynk_client.print(F("Host: ")); blynk_client.println(blynk_host);
  blynk_client.println(F("Connection: close"));
  if (request.length()) {
    blynk_client.println(F("Content-Type: application/json"));
    blynk_client.print(F("Content-Length: ")); blynk_client.println(request.length());
    blynk_client.println();
    blynk_client.print(request);
  } else {
    blynk_client.println();
  }

  //Serial.println("Waiting response");
  int timeout = millis() + 5000;
  while (blynk_client.available() == 0) {
    if (timeout - millis() < 0) {
      Serial.println(">>> Client Timeout !");
      blynk_client.stop();
      return false;
    }
  }

  //Serial.println("Reading response");
  int contentLength = -1;
  while (blynk_client.available()) {
    String line = blynk_client.readStringUntil('\n');
    line.trim();
    line.toLowerCase();
    if (line.startsWith("content-length:")) {
      contentLength = line.substring(line.lastIndexOf(':') + 1).toInt();
    } else if (line.length() == 0) {
      break;
    }
  }

  uint8_t buf[512];
  response = "";
  if ( blynk_client.connected() ) {
    blynk_client.read(buf, contentLength);
    buf[contentLength] = 0;
    response = String( (char*) buf );
  }
  return true;
}
#endif

/**
   Report successful connection to WiFi
*/
void iot_online() {

  String st = "Online " + WiFi.localIP().toString();
  iot_status_update ( st );
}

/**
   Report entering sleep mode
*/
void iot_sleep() {

  String st = "PowerSave mode";
  iot_status_update ( st );
}

/**
   generic function for updating blynk status line (Virtual pin 2)
*/
void iot_status_update( String & aStatus) {
  if ( connectedToAP ) {
#ifdef _IOT_BLYNK_

    // ip: 123.123.123.123 at 12:34 12/19
    // 1234567890123456789012345678901234
    char buf[41];
    DateTime tnow = myTZ.toLocal( now() );

    snprintf( buf, 40, "%s at %02d:%02d %02d/%02d", aStatus.c_str(), tnow.hour(), tnow.minute(), tnow.month(), tnow.day() );
    String putData = String("[\"") + String(buf) + "\"]";
    String response;
    if (httpRequest(String("PUT /") + blynk_auth + "/update/V2", putData, response)) {
      if (response.length() != 0) {

#ifdef _DEBUG_
        Serial.print("WARNING: ");
        Serial.println(response);
#endif
      }
    }
#endif
  }
}

/**
   Report current humidity and water level
*/
void iot_report() {

  //  enableWebServer();
  if ( connectedToAP ) {
#ifdef _IOT_BLYNK_

    iot_online();

    String putData = String("[\"") + /*currentHumidity*/"-1" + "\"]";
    String response;

    yield();

    if (httpRequest(String("PUT /") + blynk_auth + "/update/V0", putData, response)) {
      if (response.length() != 0) {

#ifdef _DEBUG_
        Serial.print("WARNING: ");
        Serial.println(response);
#endif
      }
    }

    putData = String("[\"") + /*currentWaterLevel*/"-1" + "\"]";

    yield();

    if (httpRequest(String("PUT /") + blynk_auth + "/update/V1", putData, response)) {
      if (response.length() != 0) {

#ifdef _DEBUG_
        Serial.print("WARNING: ");
        Serial.println(response);
#endif
      }
    }

    putData = String("[\"") + (/*hasLeaked()*/false ? "255" : "0") + "\"]";

    yield();

    if (httpRequest(String("PUT /") + blynk_auth + "/update/V3", putData, response)) {
      if (response.length() != 0) {

#ifdef _DEBUG_
        Serial.print("WARNING: ");
        Serial.println(response);
#endif
      }
    }
#endif
  }
}
