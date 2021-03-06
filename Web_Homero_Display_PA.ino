#include <SparkFunBME280.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <ButtonDebounce.h>
#include <ThingSpeak.h>

// Include NTPclient libraries
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

#define MAXSENSOR         4
#define SENSOR_IDLE       0
#define SENSOR_CONNECTING 1
#define SENSOR_CONNECTED  2
#define SENSOR_HASDATA    3
#define SENSOR_DATAREAD   4

// D5-os lab
#define BACKLIGHT_PIN 14

// LOOK FOR CHANGEME 
// in function config_init_defaultset()

// tavoli szenszor frissites (ms)
#define REMOTE_UPDATE 300000

// Global vars
#define KNOWNNET 6
String knownssid[KNOWNNET] = { "placeholderforeeprom", "sid"     , "sid"      , "sid"     , "sid"     , "sid"};
String knownpassword[KNOWNNET] = {"placeholderforeeprom", "passwd"  , "passwd" , "passwd" , "passwd" , "passwd"};

int debug = 0;
int prevcons = -1;
int i2caddr_bme280 = 0x76;

// NTP client
#define NTP_REFRESH     600000
WiFiUDP ntpUDP;
time_t now;
struct tm *now2;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org" , 2 * 3600, NTP_REFRESH);

// Legfontosabb sor!
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

// Normal vars
const int led = BUILTIN_LED;
unsigned long currentmilis, prevmilis;
unsigned long last_tsp_milis, last_blink_milis, last_rootload_milis, last_lcdupdate_milis, last_backlight_on_millis, last_wificonnect_millis;
unsigned long last_lcd_time_update_millis;

int init_bme280 = 255; // 255 sikertelen, 0 sikeres
int ledstatus = 0;

int lcd_vilagit = 0;

// 0 egyszeru modon irjuk ki
// 1 bonyulultan irjuk ki
int lcd_displaymode = 0;

uint8_t celsiusChar[8] = {0x8, 0xf4, 0x8, 0x43, 0x4, 0x4, 0x43, 0x0};
#define CELSIUS 0
uint8_t smallConsLeftChar[8] = {0x0, 0x0, 0x0, 0x1, 0x2, 0xc, 0x10, 0x1f};
#define SMALL_CONS_LEFT 1
uint8_t smallConsRightChar[8] = {0x0, 0xf, 0x11, 0xd, 0xd, 0x1, 0x1, 0x1f};
#define SMALL_CONS_RIGHT 2

uint8_t starChar[6][8] = {
  0x0, 0x3, 0x4, 0x8, 0x8, 0x10, 0x17, 0x11,
  0x1f, 0x0, 0x4, 0x4, 0xe, 0x1f, 0x1f, 0x1f,
  0x0, 0x18, 0x4, 0x2, 0x2, 0x1, 0x1d, 0x11,
  0x10, 0x10, 0x10, 0x9, 0x9, 0x4, 0x3, 0x0,
  0x1f, 0x1f, 0x1b, 0x11, 0x0, 0x0, 0x0, 0x1f,
  0x1, 0x1, 0x1, 0x12, 0x12, 0x4, 0x18, 0x0
};


struct sensordata
{
  String hostname;
  WiFiClient client;
  int status;
  int id;
  float t, tmin, tmax, p, h;
  long lastconnectedmillis;
  String ssid;
  long rssi; //signal strength
  String datastring;
} sensor[MAXSENSOR];

// melyik mezot mutassuk a t mellett
// Ezt a valtozot mindig megnoveljuk es modulo 4
// nezzuk mit irjunk ki a T melle
int display_field = 0;

WiFiClient client;
BME280 mySensor;
ESP8266WebServer server(80);

WiFiEventHandler stationDisconnectedHandler;
String wifi_event = "Wifi Event: "; // Ha van wifi disconnect event, akkor ebbe beletesszuk
String buildinfo = "";

//   Van benne EEPROM
//   Jo otletnek tunt hasznalni
//   EEPROM layout
//
struct config_data
{
  uint8_t isinited;   // jelezzuk, hogy inicializaltuk-e
  uint8_t sensorid;   // sensorid
  uint8_t tspfield; // A tinhgspeak-en ez az elso mezo ahonnan irunk, egy csatornara tobb homero is dolgozik
  uint8_t villogas;    // villogas ki vagy bekapcsolasa
  float tempadjust;       // legfontosabb mezo, mennyivel modositjuk a mersi ertekeket
  uint8_t timezone;     // idozona
  char tspapikey[50];
  char wifissid[50];
  char wifipass[50];
  char ntppool[100]; // ntp pool
  uint8_t maxsensor; // maximum number of sensors (max4)
  char extsensor1ip[100];
  char extsensor2ip[100];
  char extsensor3ip[100];
  uint8_t consimage; // which cons image to display
  unsigned long channelid;
  uint8_t lastbyte; // Ide 237-et írunk ha inicializalva van
} myeprom, defaultset, workingset;

// 24 merest tarolunk, hogy tudjuk mikor volt a nap
// leghidegebb es legmelegebb tagja
struct meres
{
  // mikor nyitjuk ki ezt az intervallumot
  // akkor kell lepni, ha 3600*1000 ms eltelt
  long millis;
  float tmax;
  String  tmaxtime;
  float tmin;
  String  tmintime;
} last24h[24];
// Ez mutatja hol jarunk  a fenti tombben.
int last24curpos = 0;

void config_init_defaultset()
{
  // CHANGEME
  strcpy(defaultset.tspapikey, "CHANGEME________");
  defaultset.tspfield = 4;
  defaultset.tempadjust = -2.5;
  defaultset.sensorid = 1;
  //defaultset.ntp_pool = "au.pool.ntp.org";
  strcpy(defaultset.ntppool, "europe.pool.ntp.org");
  // Timezone 2 CET, Timezone 10 NSW
  defaultset.timezone = 2;
  defaultset.villogas = 1;

  defaultset.maxsensor = 3;
  strcpy(defaultset.extsensor1ip, "192.168.1.191");
  strcpy(defaultset.extsensor2ip, "192.168.1.192");
  strcpy(defaultset.extsensor3ip, "192.168.1.193");

  knownssid[0].toCharArray(defaultset.wifissid, 50);
  knownpassword[0].toCharArray(defaultset.wifipass, 50);
  // CHANGEME
  defaultset.channelid = 111111;
  defaultset.consimage = 1;
}


// Ez a fuggveny kiirja a program nevet
String display_Running_Sketch (void) {
  String the_path = __FILE__;
  int slash_loc = the_path.lastIndexOf('\\');
  String the_cpp_name = the_path.substring(slash_loc + 1);
  int dot_loc = the_cpp_name.lastIndexOf('.');
  String the_sketchname = the_cpp_name.substring(0, dot_loc);

  Serial.print("\nArduino is running Sketch: ");
  Serial.println(the_sketchname);
  Serial.print("Compiled on: ");
  Serial.print(__DATE__);
  Serial.print(" at ");
  Serial.print(__TIME__);
  Serial.print("\n");

  the_sketchname += " (Compiled on: ";
  the_sketchname += __DATE__;
  the_sketchname += " ";
  the_sketchname += __TIME__;
  the_sketchname += " )";

  return (the_sketchname);
}

// Ez a fuggveny debug cellal kiirja annak a config_data strukutanak
// a tartalmat amire pointer kap
void serial_print_config(struct config_data *p)
{
  Serial.print("\nSize of config data: ");
  Serial.print(sizeof(config_data));
  Serial.print(" bytes \n .isinited: ");
  Serial.print(p->isinited);
  Serial.print("\n .sensorid: ");
  Serial.print(p->sensorid);
  Serial.print("\n .tspfield:");
  Serial.print(p->tspfield);
  Serial.print("\n .villogas: ");
  Serial.print(p->villogas);
  Serial.print("\n .tempadjust: ");
  Serial.print(p->tempadjust);
  Serial.print("\n .timezone: ");
  Serial.print(p->timezone);
  Serial.print("\n .tspapikey: ");
  Serial.print(p->tspapikey);
  Serial.print("\n .wifissid: ");
  Serial.print(p->wifissid);
  Serial.print("\n .wifipass: ");
  Serial.print(p->wifipass);
  Serial.print("\n .ntppool: ");
  Serial.print(p->ntppool);
  Serial.print("\n .maxsensor: ");
  Serial.print(p->maxsensor);
  Serial.print("\n .extsensor1ip: ");
  Serial.print(p->extsensor1ip);
  Serial.print("\n .extsensor2ip: ");
  Serial.print(p->extsensor2ip);
  Serial.print("\n .extsensor3ip: ");
  Serial.print(p->extsensor3ip);
  Serial.print("\n .consimage: ");
  Serial.print(p->consimage);
  Serial.print("\n .channelid: ");
  Serial.print(p->channelid);
  Serial.print("\n .lastbyte: ");
  Serial.print(p->lastbyte);
}

// Ez a függvény a globális EEPROM objektummal dolgozik
// kiolvassa belőle az értékeket és beleteszi a globalis myeprom
// struktúrába
boolean eeprom_read()
{
  Serial.print("\neeprom read: reading ");
  EEPROM.get(0, myeprom);

  Serial.print("\neeprom read, contents of myeprom: ");
  serial_print_config(&myeprom);
  if ( myeprom.isinited == 1 && myeprom.lastbyte == 237) return true;
  return false;
}

// Ez a függvény a globális EEPROM objektummal dolgozik
// Beírja és commitálja a globalis myeprom
// struktúrából az értékeket
boolean eeprom_write()
{

  Serial.print("\neeprom write: put ");

  myeprom.isinited = 1;
  myeprom.lastbyte = 237;

  EEPROM.put(0, myeprom);

  Serial.print("\neeprom write: commiting. ");
  EEPROM.commit();

  Serial.print("\neeprom write: reading back ");
  eeprom_read();
}

// Kiirja, hogy hol vannak a csatlakoztatott
// I2C eszközök
void scan_I2Cbus()
{
  // Scan the I2C bus, return the address of the first device
  byte error, address;
  int nDevices;

  Serial.println("Scanning I2C bus...");

  nDevices = 0;
  for (address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.print("  !\n");
      nDevices++;
    }
    else if (error == 4)
    {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
}



int setup_bme280(byte address)
{
  bool initres;
  float t;
  //***Driver settings********************************//
  //commInterface can be I2C_MODE or SPI_MODE
  //specify chipSelectPin using arduino pin names
  //specify I2C address.  Can be 0x77(default) or 0x76

  //For I2C, enable the following and disable the SPI section
  //mySensor.settings.commInterface = I2C_MODE;
  // mySensor.settings.I2CAddress = 0x76;
  // mySensor.settings.I2CAddress = address;


  mySensor.reset();
  //mySensor.settings.commInterface = I2C_MODE;
  mySensor.setI2CAddress(address);

  //For SPI enable the following and dissable the I2C section
  //mySensor.settings.commInterface = SPI_MODE;
  //mySensor.settings.chipSelectPin = 10;


  //***Operation settings*****************************//

  //runMode can be:
  //  0, Sleep mode
  //  1 or 2, Forced mode
  //  3, Normal mode
  mySensor.setMode (MODE_FORCED); //sleep mode

  //tStandby can be:
  //  0, 0.5ms
  //  1, 62.5ms
  //  2, 125ms
  //  3, 250ms
  //  4, 500ms
  //  5, 1000ms
  //  6, 10ms
  //  7, 20ms
  mySensor.setStandbyTime(3);

  //filter can be off or number of FIR coefficients to use:
  //  0, filter off
  //  1, coefficients = 2
  //  2, coefficients = 4
  //  3, coefficients = 8
  //  4, coefficients = 16
  mySensor.setFilter(0);

  //tempOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.setTempOverSample(3);

  //pressOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.setPressureOverSample(1);

  //humidOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.setHumidityOverSample(1);

  delay(50);  //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.         Serial.begin(57600);


  Serial.print("\nStarting BME280 at address 0x");
  Serial.print(address);
  //Calling .begin() causes the settings to be loaded
  initres = mySensor.beginI2C();
  mySensor.setMode(MODE_SLEEP);

  if ( initres == true )
  {
    Serial.print(": SUCCESS");
    init_bme280 = 0;
  }
  else
  {
    Serial.print(": FAILED");
  }
  t = readadjustedtemp();
  Serial.print("\nsetup_bme280: Temp: ");
  Serial.print(t);
  Serial.print("\nsetup_bme280: Humidity: ");
  Serial.print(readadjustedhumidity());
  Serial.print("\nsetup_bme280: Pressure: ");
  Serial.print(readadjustedpressure());
  Serial.print("\nsetup_bme280: finished\n");
  return init_bme280;
}

// forced módban működő szenzorral mér
// igazított értéket ad vissza
float readadjustedtemp()
{
  float t = 0.0;

  mySensor.setMode(MODE_FORCED); //Wake up sensor and take reading
  while (mySensor.isMeasuring() == false) ; //Wait for sensor to start measurment
  while (mySensor.isMeasuring() == true) ; //Hang out while sensor completes the reading

  t = mySensor.readTempC();
  Serial.print("\nreadadjustedtemp: Sensor reports:");
  Serial.print( t );
  Serial.print(" adjusting to ");
  Serial.print(t + workingset.tempadjust);
  return ( t + workingset.tempadjust );
}

float readadjustedhumidity()
{
  return mySensor.readFloatHumidity();
}

float readadjustedpressure()
{
  return mySensor.readFloatPressure();
}
// Log to ThingSpeak
// debug móban, vagy ha ures a tspapikey
// nem tolt fel adatot
void logtsp()
{

  float t, h, p;
  String str_f1 = "field";
  String str_f2 = "field";
  String str_f3 = "field";

  currentmilis = millis();

  t = readadjustedtemp();
  h = readadjustedhumidity();
  p = readadjustedpressure();

  if ( debug == 1) return;
  if ( workingset.tspapikey == "" )
  {
    Serial.print("\n Skipping thingspeak  data upload.");
    return;
  }

 // ThingSpeak.begin(client);
  ThingSpeak.setField(workingset.tspfield, t);
  ThingSpeak.setField(workingset.tspfield + 1, h);
  ThingSpeak.setField(workingset.tspfield + 2, p);

  int x = ThingSpeak.writeFields(workingset.channelid , workingset.tspapikey);
  if (x == 200) {
    Serial.print("\nChannel update successful.");
    Serial.print("\nData sent to thingspeak ");
    Serial.print(" field:");
    Serial.print(workingset.tspfield);
    Serial.print(" t:");
    Serial.print(t);
    Serial.print(" h:");
    Serial.print(h);
    Serial.print(" p:");
    Serial.print(p);
  }
  else {
    Serial.println("\nProblem updating channel. HTTP error code " + String(x));
  }
  last_tsp_milis = currentmilis;
}

// Return a string with easy to read time
// Convert millis to day, hr, mm, ss, ms
String easyreadtime(long millis)
{
  long dd, hh, mm, ss, ms;
  long d = 86400000;
  long h = 3600000;
  long m = 60000;
  long s = 1000;
  String retval = "";

  dd = millis / d;
  if ( dd != 0 ) retval += String(dd) + "d ";

  hh = ( millis % d) / h;
  if ( hh != 0 ) retval += String(hh) + "h ";

  mm = (( millis % d ) % h) / m;
  if ( mm != 0 ) retval += String(mm) + "m ";

  ss = ((( millis % d ) % h) % m) / s;
  retval += String(ss) + "s";

  // Serial.println(retval);
  return retval;
}

// Ez a függvény generálja
// a "root" vagy főoldalt
void handleRoot() {
  float t;
  String message = "<html><head> <title> Webhomero </title> </head>";
  String myip = "";

  currentmilis = millis();
  digitalWrite(led, 0);

  myip = WiFi.localIP().toString();
  message += "\n<body> Hello from esp8266!";
  message += "\n<br> This is sensor: ";
  message += workingset.sensorid;
  message += "\n<br> Serial menu is available 115200 baud to enter new ssid";

  message += "\n<br><br><a href=\"http://" + myip + "/setup\"> Be&aacute;llit&aacute;sok /setup</a>";
  message += "\n<br><a href=\"http://" + myip + "/t\"> M&eacute;rt adatok /t</a>";
  message += "\n<br><a href=\"http://" + myip + "/print24\"> M&eacute;rt adatok 24 ora alatt/t</a>";
  message += "\n<br><a href=\"http://" + myip + "/collectnew\"> Uj adatok gyujtese /collectnew</a>";
  message += "\n<br><a href=\"http://" + myip + "/backlight\"> Hattervilagitas be /backlight</a>";
  message += "\n<br><a href=\"http://" + myip + "/reconnect\"> Ujracsatlakozas a legerosebb wifihez /reconnect </a> Visszaadja a fooldalt majd 10s mulva ujracsatlakozik";


  Serial.print("\n\nhandleRoot Uptime (s): ");
  Serial.print( easyreadtime( currentmilis ) );
  t = readadjustedtemp();
  Serial.print("\nhandleRoot Temp: ");
  Serial.print( t );
  Serial.print("\nhandleRoot Temp adjusted with: ");
  Serial.print( workingset.tempadjust);
  Serial.print("\nhandleRoot Humidity: ");
  Serial.print(readadjustedhumidity());
  Serial.print("\nhandleRoot Pressure: ");
  Serial.print(readadjustedpressure());

  message += "\n<br><br> Temp: ";
  message += readadjustedtemp();
  message += "\n<br> Temp adjusted: ";
  message += workingset.tempadjust;
  message += "\n<br> Humidity: ";
  message += readadjustedhumidity();
  message += "\n<br> Pressure: ";
  message += readadjustedpressure();

  message += "\n<br> Sensor[0].t: ";
  message += sensor[0].t;
  message += "\n<br> Sensor[1].t: ";
  message += sensor[1].t;
  message += "\n<br> Sensor[2].t: ";
  message += sensor[2].t;

  message += "<br><br>\n ";
  message += wifi_event;

  message += "<br> Wifi SSID:";
  message += WiFi.SSID();
  message += "<br> Wifi signal strength (dBm): ";
  message += WiFi.RSSI();

  message += "\n<br><br> Uptime : ";
  message += easyreadtime( currentmilis );
  //message += currentmilis / 1000;
  message += "\n<br> Previous rootload ";
  message += easyreadtime( currentmilis - last_rootload_milis );
  message += " ago.";

  // message += (currentmilis - last_rootload_milis) / 1000;
  message += "\n<br> Previous TSP update ";
  message += easyreadtime ( currentmilis - last_tsp_milis);
  message += " ago.";

  message += "\n<br> Last 24 hrs measures at position: ";
  message += last24curpos;

  //message += (currentmilis - last_tsp_milis) / 1000;

  message += "\n<br>\n<br> Sketch: ";
  message += buildinfo;

  message += "\n<br>\n<br> Page generated at: ";
  message += get_string_time();

  message += "\n</body></html>\n";

  server.send(200, "text/html", message);
  last_rootload_milis = currentmilis;
}

void handleNotFound() {
  digitalWrite(led, 0);
  String message = "File Not Found\n\n";
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

// Ezt a függvényt hívjuk meg, amikor
// csak a mérési érékeket akarjuk kiírni
void handlet() {
  digitalWrite(led, 0);
  String message = "";
  message += "ID:";
  message += workingset.sensorid;
  message += ";T:";
  message += readadjustedtemp();
  message += ";H:";
  message += readadjustedhumidity();
  message += ";P:";
  message += readadjustedpressure();
  message += ";";
  message += minmax_last24h();
  message += ";SSID:";
  message += WiFi.SSID();
  message += ";RSSI:";
  message += WiFi.RSSI();
  message += ";";

  server.send(200, "text/plain", message);
}

void handlereconnect() {
  handleRoot();
  delay(10000);
  findandconnectstrongestwifi();
}

void handleprint24() {
  digitalWrite(led, 0);
  String message = print_last24h();
  server.send(200, "text/plain", message);
}

void handlecollectnewdata() {
  digitalWrite(led, 0);
  request_remote_sensor_data();
  handleRoot();
  //server.send(200, "text/plain", "Collecting new info for lcd");
}


// Ezt a függvényt hívjuk meg, amikor
// be akarjuk állítani az eeprom értékeit
void handleset() {
  int i = 0;
  int tempint = 0;
  int commit = 0;
  String message = "Settings \n\n";
  String msg2 = "";
  //String str1;
  String s2;

  String myip = "";

  digitalWrite(led, 0);
  myip = WiFi.localIP().toString();

  digitalWrite(led, 0);

  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";

    if ( server.argName(i) == "adjust" )
    {
      s2 = (String)server.arg(i);
      myeprom.tempadjust = s2.toFloat();
      msg2 += "\nadjust updated: ";
      msg2 += myeprom.tempadjust;
    }

    if ( server.argName(i) == "tspapikey" )
    {
      s2 = (String)server.arg(i);
      s2.toCharArray(myeprom.tspapikey, 50);
      msg2 += "\ntspapikey updated: ";
      msg2 += myeprom.tspapikey;
    }

    if ( server.argName(i) == "channelid" )
    {
      s2 = (String)server.arg(i);
      // toInt returns a long in fact
      myeprom.channelid = s2.toInt();
      msg2 += "\nchannelid updated: ";
      msg2 += myeprom.channelid;
    }

    if ( server.argName(i) == "wifissid" )
    {
      knownssid[0] = (String)server.arg(i);
      knownssid[0].toCharArray(myeprom.wifissid, 50);
      msg2 += "\nwifissid updated: ";
      msg2 += myeprom.wifissid;
    }

    if ( server.argName(i) == "wifipass" )
    {
      knownpassword[0] = (String)server.arg(i);
      knownpassword[0].toCharArray(myeprom.wifipass, 50);
      msg2 += "\nwifipass updated: ";
      msg2 += myeprom.wifipass;
    }

    if ( server.argName(i) == "ntppool" )
    {
      s2 = (String)server.arg(i);
      s2.toCharArray(myeprom.ntppool, 100);
      msg2 += "\nntppool updated: ";
      msg2 += myeprom.ntppool;
    }

    if ( server.argName(i) == "tspfield" )
    {
      s2 = (String)server.arg(i);
      myeprom.tspfield = s2.toInt();
      msg2 += "\ntspfield updated: ";
      msg2 += myeprom.tspfield;
    }

    if ( server.argName(i) == "villogas" )
    {
      s2 = (String)server.arg(i);
      myeprom.villogas = s2.toInt();
      msg2 += "\nvillogas updated: ";
      msg2 += myeprom.villogas;
    }

    if ( server.argName(i) == "timezone" )
    {
      s2 = (String)server.arg(i);
      myeprom.timezone = s2.toInt();
      msg2 += "\ntimezone updated: ";
      msg2 += myeprom.timezone;
      timeClient.setTimeOffset(myeprom.timezone * 3600);
    }
    if ( server.argName(i) == "sensorid" )
    {
      s2 = (String)server.arg(i);
      myeprom.sensorid = s2.toInt();
      msg2 += "\nsensorid updated: ";
      msg2 += myeprom.sensorid;
    }

    if ( server.argName(i) == "maxsensor" )
    {
      s2 = (String)server.arg(i);
      tempint = s2.toInt();
      myeprom.maxsensor = tempint;
      msg2 += "\nmaxsensor: ";
      msg2 += myeprom.maxsensor;
    }

    if ( server.argName(i) == "extsensor1ip" )
    {
      s2 = (String)server.arg(i);
      s2.toCharArray(myeprom.extsensor1ip, 100);
      msg2 += "\nextsensor1ip updated: ";
      msg2 += myeprom.extsensor1ip;
    }

    if ( server.argName(i) == "extsensor2ip" )
    {
      s2 = (String)server.arg(i);
      s2.toCharArray(myeprom.extsensor2ip, 100);
      msg2 += "\nextsensor2ip updated: ";
      msg2 += myeprom.extsensor1ip;
    }

    if ( server.argName(i) == "extsensor3ip" )
    {
      s2 = (String)server.arg(i);
      s2.toCharArray(myeprom.extsensor3ip, 100);
      msg2 += "\nextsensor3ip updated: ";
      msg2 += myeprom.extsensor3ip;
    }

    if ( server.argName(i) == "consimage" )
    {
      s2 = (String)server.arg(i);
      tempint = s2.toInt();
      myeprom.consimage = tempint;
      msg2 += "\nconsimage: ";
      msg2 += myeprom.consimage;
    }

    if ( server.argName(i) == "store" )
    {
      s2 = (String)server.arg(i);
      if ( s2 == "Save and commit" )
      {
        commit = 1;
        msg2 += "\nwill commit to eeprom. ";
      }
      else
      {
        msg2 += "\nwill NOT commit to eeprom. ";
      }
    }

  }

  message += msg2;

  if ( commit == 1) eeprom_write();

  // Copy myeprom data to workingset all at once
  message += "\nCopying eeprom data to workingset";

  memcpy(&workingset, &myeprom, sizeof(struct config_data));

  Serial.print("\n handleset: content of workingset config\n");
  serial_print_config(&workingset);
  knownssid[0] = workingset.wifissid;
  knownpassword[0] = workingset.wifipass;
  timeClient.setTimeOffset(workingset.timezone * 3600);
  //  timeClient.setPoolServerName(workingset.ntppool);
  if ( workingset.maxsensor > MAXSENSOR ) workingset.maxsensor = MAXSENSOR;
  sensor[0].hostname = "localhost";
  sensor[0].status = SENSOR_IDLE;
  sensor[1].hostname = workingset.extsensor1ip;
  sensor[1].status = SENSOR_IDLE;
  sensor[2].hostname = workingset.extsensor2ip;
  sensor[2].status = SENSOR_IDLE;
  sensor[3].hostname = workingset.extsensor3ip;
  sensor[3].status = SENSOR_IDLE;

  server.send(200, "text/plain", message);
}

// Altalanos beállító oldal
// Kiir egy html formot
// ahol a legfontosabb érékeket be tudjuk állítani
void handleSetup()
{
  digitalWrite(led, 0);
  String message = "<html> \
\n<head> \
\n<title> Webhomero setup </title> \
\n</head> \
\n<body> \
\n<table> \
\n<form action=\"/set\" method=\"get\"> \
\n  <tr><td>Temperature adjust: </td>\
\n  <td><input type=\"text\" name=\"adjust\" maxlength=\"5\" value=\"";
  message += workingset.tempadjust;
  message += "\"></td></tr> \
\n  <tr><td>ThingSpeak ChannelID: </td>\
\n  <td><input type=\"text\" name=\"channelid\" maxlength=\"50\" value=\"";
  message += workingset.channelid;
  message += "\"></td></tr> \
\n  <tr><td>ThingSpeak Write API key: </td>\
\n  <td><input type=\"text\" name=\"tspapikey\" maxlength=\"50\" value=\"";
  message += workingset.tspapikey;
  message += "\"></td></tr> \
\n  <tr><td>ThingSpeak starting filed number (if empty skip upload) :</td> \
\n  <td><input type=\"text\" name=\"tspfield\" maxlength=\"5\" value=\"";
  message += workingset.tspfield;
  message += "\"></td></tr> \
\n  <tr><td>Villogas: </td>\
\n  <td><input type=\"text\" name=\"villogas\" maxlength=\"1\" value=\"";
  message += workingset.villogas;
  message += "\"></td></tr> \
\n  <tr><td>SensorID: </td>\
\n  <td><input type=\"text\" name=\"sensorid\" maxlength=\"2\" value=\"";
  message += workingset.sensorid;
  message += "\"></td></tr> \
\n  <tr><td>Timezone: 2 CEST 3 CET 10 nsw</td>\
\n  <td><input type=\"text\" name=\"timezone\" maxlength=\"2\" value=\"";
  message += workingset.timezone;
  message += "\"></td></tr> \
\n  <tr><td>Wifi SSID: </td>\
\n  <td><input type=\"text\" name=\"wifissid\" maxlength=\"50\" value=\"";
  message += knownssid[0];
  message += "\"></td></tr> \
\n  <tr><td>Wifi Pass</td>\
\n  <td><input type=\"text\" name=\"wifipass\" maxlength=\"50\" value=\"";
  message += knownpassword[0];
  message += "\"></td></tr> \
\n  <tr><td>NTP pool</td>\
\n  <td><input type=\"text\" name=\"ntppool\" maxlength=\"100\" value=\"";
  message += workingset.ntppool;
  message += "\"></td></tr> \
\n  <tr><td>Number of sensors (including this)</td>\
\n  <td><input type=\"text\" name=\"maxsensor\" maxlength=\"1\" value=\"";
  message += workingset.maxsensor;
  message += "\"></td></tr> \
\n  <tr><td>External Sensor 1 IP addr</td>\
\n  <td><input type=\"text\" name=\"extsensor1ip\" maxlength=\"100\" value=\"";
  message += sensor[1].hostname; \
  message += "\"></td></tr>  \
\n  <tr><td>External Sensor 2 IP addr</td>\
\n  <td><input type=\"text\" name=\"extsensor2ip\" maxlength=\"100\" value=\"";
  message += sensor[2].hostname; \
  message += "\"></td></tr> \
\n  <tr><td>External Sensor 3 IP addr</td>\
\n  <td><input type=\"text\" name=\"extsensor3ip\" maxlength=\"100\" value=\"";
  message += sensor[3].hostname; \
  message += "\"></td></tr> \
\n  <tr><td>Cons image to display 0:none 1:small 2:big</td>\
\n  <td><input type=\"text\" name=\"consimage\" maxlength=\"1\" value=\"";
  message += workingset.consimage; \
  message += "\"></td></tr> \
\
\n  <tr><td>\
\n  <input type=\"submit\" name=\"submit\" value=\"Save\"></td></tr> \
\n  <tr><td>\
\n  <input type=\"submit\" name=\"store\" value=\"Save and commit\"></td></tr> \
\n</form> \
\n</table> \
\n</body> \
\n<html>";

  server.send(200, "text/html", message);
}

void handlebacklight()
{
  last_backlight_on_millis = millis();
  lcd.backlight();
  lcd_vilagit = 1;
  handleRoot();
}

// nem tudom jó lesz-e
// ha lejár a MAC lease, akkor kidob-e...
void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected& evt) {
  int res = 1;
  Serial.print("Station disconnected: ");
  wifi_event += "\nWifi disconnect at (s):";
  wifi_event += millis() / 1000;
  findandconnectstrongestwifi();
}

void printScanResult(int networksFound)
{
  Serial.printf("\n%d network(s) found\n", networksFound);
  for (int i = 0; i < networksFound; i++)
  {
    Serial.printf("%d: %s, Ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
  }
}


// visszaadja a legerosebb network sorszamat
// amely erosebben sugaroz mint a masodik ertek
// parameter: hany halozatot talaltunk korabban, max erosseg
// a max erosseg azert kell, ha nem sikerul a legerosebbhez
// csatlakozni, megyunk a gyengebbre
int strongestsignalbelowdbm(int networksFound, int maxdbm)
{
  int retval = -1;
  // lehetetlenul alacsony ertek
  long myrssi = -10000;

  for (int i = 0; i < networksFound; i++)
  {

    // Lehet olyan, hogy egy ilyen eros halot mar lattunk
    // Direkt rontunk rajta egyet
    //if(  myrssi == WiFi.RSSI(i) )
    //{
    //  WiFi.RSSI(i)=WiFi.RSSI(i)-1;
    //}

    if (  myrssi < WiFi.RSSI(i) &&  maxdbm > WiFi.RSSI(i) )
    {
      retval = i;
      myrssi = WiFi.RSSI(i);
    }
  }
  if ( retval > -1) Serial.printf("\nStrongest signal below: (%ddBm) %s, Ch:%d (%ddBm) %s\n", maxdbm, WiFi.SSID(retval).c_str(), WiFi.channel(retval), WiFi.RSSI(retval), WiFi.encryptionType(retval) == ENC_TYPE_NONE ? "open" : "");
  else Serial.printf("\nNo signal below: (%ddBm) retval: %d", maxdbm, retval);
  return retval;
}

int connecttowifi(const char *ssid, const char *password)
{
  int waittime = 0;
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  Serial.println("\n");
  Serial.println("\nConnceting to Wifi: ");
  Serial.println(ssid);

  // Wait for connection approx 2 minutes
  while (WiFi.status() != WL_CONNECTED &&  waittime < 120 ) {
    delay(500);
    waittime = waittime + 1;
    Serial.print(".");
  }
  if ( WiFi.status() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifi_event += " Wifi connected ";;
    wifi_event += millis() / 1000;
    wifi_event += " (s) after power-on";
    wifi_event += " Wifi IP:";
    wifi_event += String(WiFi.localIP().toString());
    return 0;
  }
  else
  {
    Serial.println("");
    Serial.print("Failed to connect to ");
    Serial.println(ssid);
    return 1;
  }

}


int findandconnectstrongestwifi()
{
  int i, j, n, target;
  int isconnected = 1 ;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);


  //WiFi.scanNetworks(async, show_hidden)
  // sync scan, with showing hidden networks
  n = WiFi.scanNetworks(false, true);
  if ( n == -1 )
  {
    Serial.print("\n No WiFi network in range ...");
    return -1;
  }
  printScanResult(n);

  // Let's find the absolute strongest first
  target = strongestsignalbelowdbm(n, 0);


  //Trying to connect
  //Max 3 times / target
  do
  {
    // Let's see if we have the password for this network
    for (i = 0; i < KNOWNNET && isconnected != 0; i++)
    {
      //Serial.printf("\nDo we have password for target %s -> entry %s .",WiFi.SSID(target).c_str(), knownssid[i].c_str());
      if ( knownssid[i] == WiFi.SSID(target) )
      {
        // Megvan az SSID az ismertek kozott
        // Megprobalunk csatlakozni
        Serial.printf("\nFound password for %s .", WiFi.SSID(target).c_str());
        isconnected = connecttowifi(knownssid[i].c_str(), knownpassword[i].c_str());
        if ( isconnected == 0 )
        {
          Serial.print("\nConnected");
        }
        // Break the for loop
        break;
      }
      else
      {
        // Serial.print("SSIDs do not match.");
      }
    }
    //Serial.print("\nScanned password database(end of for)");
    // Ha nem sikerült csatlakozni
    if ( isconnected != 0 )
    {
      // Keresünk új célpontot
      Serial.print("\nNot connected, looking for new target");
      target = strongestsignalbelowdbm(n, WiFi.RSSI(target));

      // Ha nincs mar target abbahagyjuk a while ciklust
      if ( target == -1 ) break;
    }
  } while ( isconnected != 0 );

  if ( isconnected == 0 )
  {
    Serial.print("\nSuccessfully connected");
    return 0;
  }
  else
  {
    Serial.print("\nFailed to connect");
    return -1;
  }
}

void blink()
{
  last_blink_milis = millis();
  // Az elso 50 masodpercben mindenkeppen villogunk, csak hogy tudjuk, hogy jok vagyunk
  if ( ledstatus == 1 && ( workingset.villogas == 1 || last_blink_milis < 50000 ) )  {
    digitalWrite(led, 0);
    ledstatus = 0;
  }
  else {
    digitalWrite(led, 1);
    ledstatus = 1;
  }
}

// Inicializalja azt a tombot amibe az utolso
// 24 ora legkisebb es legnagyobb homerkletet
// gyujtjuk
void init_last24h()
{
  int i = 0;
  int curmillis;
  curmillis = millis();
  for (i = 0; i < 24; i++)
  {
    last24h[i].tmax = -150;
    last24h[i].tmin = 150;
    last24h[i].millis = curmillis + i * 3600000;
    last24h[i].tmaxtime = "";
    last24h[i].tmintime = "";
  }
  last24curpos = 0;
}

// update hourly tmin and tmax
// writes values to last24h array
// rotates write postion if needed.
void update_last24h()
{
  long curmillis;
  long curhour; // Ennyi óra telt el a bekapcsolás óta
  int  pos;
  float t;

  curmillis = millis();
  curhour = curmillis / 3600000; // Ennyi óraja vagyunk bekapcsolva
  pos = (int) (curhour % 24); // ora mod 24 ide írjuk az értékeinket

  Serial.println("\nupdate_last24: Updating hourly tmax and tmin");
  t = readadjustedtemp();

  if ( last24curpos != pos )
  {
    Serial.println("\nupdate_last24: moving to next position with pointer");
    last24curpos = pos;
    last24h[last24curpos].millis = curmillis;
    last24h[last24curpos].tmax = t;
    last24h[last24curpos].tmin = t;
    last24h[last24curpos].tmaxtime = get_string_time();
    last24h[last24curpos].tmintime = get_string_time();

  }
  if ( t > last24h[last24curpos].tmax )
  {
    last24h[last24curpos].tmax = t;
    last24h[last24curpos].tmaxtime = get_string_time();
    //Serial.println("\nupdate_last24: update tmax");

  }
  if ( t < last24h[last24curpos].tmin )
  {
    last24h[last24curpos].tmin = t;
    last24h[last24curpos].tmintime = get_string_time();
    //Serial.println("\nupdate_last24: update tmin");
  }
}

// Segedfuggveny, kikeresi a legkisebb es
// legnagyobb homersekletet
String print_last24h()
{
  int i = 0;
  int curmillis;
  String message;

  curmillis = millis();
  for (i = 0; i < 24; i++)
  {
    message += "\n";
    message += i;
    message += " tmaxtime;";
    message += last24h[i].tmaxtime;
    message += ";tmax;";
    message += last24h[i].tmax;
    message += ";tmintime;";
    message += last24h[i].tmintime;
    message += ";tmin;";
    message += last24h[i].tmin;
  }

  message += "\n MIN and MAX:\n";
  message += minmax_last24h();
  Serial.println(message);
  return (message);
}

String minmax_last24h()
{
  int i = 0;
  int curmillis;
  String message;
  float tmin, tmax;

  tmin = last24h[0].tmin;
  tmax = last24h[0].tmax;

  for (i = 1; i < 24; i++)
  {
    if ( tmin > last24h[i].tmin ) tmin = last24h[i].tmin;
    if ( tmax < last24h[i].tmax ) tmax = last24h[i].tmax;
  }

  message += "tmin:";
  message += tmin;
  message += ";tmax:";
  message += tmax;
  //Serial.println(message);
  return (message);
}


void lcd_display_info()
{
  if ( lcd_displaymode == 0) lcd_display_info_simple();
  else lcd_display_info_full();
}

void lcd_display_cons()
{
  int i;
  switch ( workingset.consimage)
  {
    case 0:
      break;
      if (prevcons != 0)
      {
        lcd.createChar(CELSIUS, celsiusChar);
      }
      prevcons = 0;
    case 1:
      if (prevcons != 1)
      {
        lcd.createChar(CELSIUS, celsiusChar);
        lcd.createChar(SMALL_CONS_LEFT, smallConsLeftChar);
        lcd.createChar(SMALL_CONS_RIGHT, smallConsRightChar);
      }
      lcd.setCursor(18, 1);
      lcd.print((char)SMALL_CONS_LEFT);
      lcd.print((char)SMALL_CONS_RIGHT);
      prevcons = 1;
      break;
    case 2:
      if (prevcons != 2)
      {
        lcd.createChar(CELSIUS, celsiusChar);
        //
        for (i = 0; i < 6; i++) {
          lcd.createChar(i + 1, starChar[i]);
        }
      }
      lcd.setCursor(17, 0);
      for (i = 0; i < 6; i++) {
        lcd.print((char)(i + 1));
        if (i == 2) {
          lcd.setCursor(17, 1);
        }
      }
      prevcons = 2;
      break;
  }
}

void lcd_display_info_simple()
{
  last_lcdupdate_milis = millis();
  lcd.home();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(" T(itt) :");
  lcd.print(sensor[0].t);
  lcd.print((char)CELSIUS);

  lcd.setCursor(0, 1);
  lcd.print(" T(kint):");
  lcd.print(sensor[1].t);
  lcd.print((char)CELSIUS);

  // Ha több mint tíz perce nem frissült, akkor hibajelzés
  if ( last_lcdupdate_milis - sensor[1].lastconnectedmillis > 600000 ) lcd.print("!");
  else lcd.print(" ");

  lcd.setCursor(0, 2);
  lcd.print(" T(fent):");
  lcd.print(sensor[2].t);
  lcd.print((char)CELSIUS);

  // Ha több mint tíz perce nem frissült, akkor hibajelzés
  if ( last_lcdupdate_milis - sensor[2].lastconnectedmillis > 600000 ) lcd.print("!");
  else lcd.print(" ");

  if ( sensor[1].t < 30 && sensor[1].t > -5 )
  {
    lcd_display_cons();
  }

}


void lcd_display_info_full()
{
  int i;
  long lastreadmillisago;
  //Serial.print("\nlcd update");
  last_lcdupdate_milis = millis();
  lcd.home();
  lcd.clear();
  for (i = 0; i < workingset.maxsensor; i++)
  {
    lcd.setCursor(0, i);
    lcd.print("T:");
    lcd.print(sensor[i].t);

    switch ( display_field )
    {
      case 0:
        lcd.print(" Tmin:");
        lcd.print(sensor[i].tmin);
        break;
      case 1:
        lcd.print(" Tmax:");
        lcd.print(sensor[i].tmax);
        break;
      case 2:
        lcd.print(" h:");
        lcd.print(sensor[i].h);
        break;
      case 3:
        lcd.print(" p:");
        lcd.print(sensor[i].p);
        break;
      case 4:
        lcd.setCursor(0, i);
        lcd.print("                    ");
        lcd.setCursor(0, i);
        lastreadmillisago = millis() - sensor[i].lastconnectedmillis;
        lcd.print(sensor[i].hostname);
        lcd.print(" ");
        lcd.print(easyreadtime(lastreadmillisago));
        break;
      case 5:
        lcd.setCursor(0, i);
        lcd.print("                    ");
        lcd.setCursor(0, i);
        lcd.print("dBm:");
        lcd.print(sensor[i].rssi);
        lcd.print(" @ ");
        lcd.print(sensor[i].ssid);

        break;
    }
  }
  display_field++;
  display_field = display_field % 6;
  //lcd.setCursor(0,3);
  //lcd.print("Frissitesig: ");
  //lcd.print(easyreadtime(REMOTE_UPDATE- (currentmilis - last_tsp_milis)));
}

void lcd_display_time(void) {
  last_lcd_time_update_millis = millis();
  lcd.home();
  lcd.setCursor(0, 3);

  timeClient.update();

  now = timeClient.getEpochTime();
  now2 = localtime(&now);

  // a datumot csak az epoch time-bol tudjuk kiirni, mert nincs ra fuggveny az NTPClient library-ban
  lcd.print(now2->tm_year + 1900);
  lcd.print("/");
  if (now2->tm_mon + 1 < 10)
    lcd.print(0);
  lcd.print(now2->tm_mon + 1);
  lcd.print("/");
  if (now2->tm_mday < 10)
    lcd.print(0);
  lcd.print(now2->tm_mday);
  lcd.print(" ");
  lcd.print(timeClient.getFormattedTime());
}

// print the current time
// into a string
String get_string_time(void) {

  String s1;
  timeClient.update();

  now = timeClient.getEpochTime();
  now2 = localtime(&now);

  // a datumot csak az epoch time-bol tudjuk kiirni, mert nincs ra fuggveny az NTPClient library-ban
  s1 = now2->tm_year + 1900;
  s1 += "/";
  if (now2->tm_mon + 1 < 10)
    s1 += "0";

  s1 += now2->tm_mon + 1;
  s1 += "/";
  if (now2->tm_mday < 10)
    s1 += "0";

  s1 += now2->tm_mday;
  s1 += " ";
  s1 += timeClient.getFormattedTime();

  return s1;
}

void setup(void) {
  int i, res = 1;
  currentmilis = 0;
  prevmilis = 0;
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);

  //gomb pinje
  pinMode(BACKLIGHT_PIN, INPUT_PULLUP);

  Serial.begin(115200);

  buildinfo = display_Running_Sketch();

  Serial.print("\nInit lcd ");  // Used to type in characters

  lcd.begin(20, 4, LCD_5x8DOTS);       // initialize the lcd for 20 chars 4 lines, turn on backlight
  lcd.backlight();
  lcd.createChar(CELSIUS, celsiusChar);



  lcd_vilagit = 1;
  lcd.clear();
  lcd.home();

  lcd.setCursor(0, 0); //Start at character 4 on line 0
  lcd.print("Hello, world!");
  delay(1000);
  lcd.setCursor(0, 0); //Start at character 4 on line 0

  // Loading a default set of configuration
  // to the default config set.

  config_init_defaultset();
  Serial.print("\nPrinting data of defaultset");
  serial_print_config(&defaultset);

  // EEprom init es egy kiolvasas
  EEPROM.begin(1024);

  if ( eeprom_read() == false )
  {
    Serial.print("\nEEPROM not initialized");
    lcd.print("EEPROM not inited!");
    // EEPROM is not OK so we will copy defaultset to workingset instead
    memcpy(&workingset, &defaultset, sizeof(struct config_data));
  }
  else
  {
    lcd.print("Loading EEPROM");
    Serial.print("\nEEPROM is initialized");
    Serial.print("\nLoading data from EEPROM");

    // EEPROM is OK so we will copy eeprom to workingset instead
    memcpy(&workingset, &myeprom, sizeof(struct config_data));
  }
  Serial.print("\nPrinting data of workingset");
  serial_print_config(&workingset);
  knownssid[0] = workingset.wifissid;
  knownpassword[0] = workingset.wifipass;
  timeClient.setTimeOffset(workingset.timezone * 3600);
  //  timeClient.setPoolServerName(workingset.ntppool);
  if ( workingset.maxsensor > MAXSENSOR ) workingset.maxsensor = MAXSENSOR;
  sensor[0].hostname = "localhost";
  sensor[0].status = SENSOR_IDLE;
  sensor[1].hostname = workingset.extsensor1ip;
  sensor[1].status = SENSOR_IDLE;
  sensor[2].hostname = workingset.extsensor2ip;
  sensor[2].status = SENSOR_IDLE;
  sensor[3].hostname = workingset.extsensor3ip;
  sensor[3].status = SENSOR_IDLE;

  lcd.setCursor(0, 1);
  lcd.print("Connecting Wifi...");

  findandconnectstrongestwifi();

  lcd.setCursor(0, 2);
  lcd.print(WiFi.localIP());

  if (MDNS.begin("esp8266")) {
    Serial.println("\nMDNS responder started");
  }



  server.on("/", handleRoot);
  server.on("/set", handleset);
  server.on("/setup", handleSetup);
  server.on("/t", handlet);
  server.on("/print24", handleprint24);
  server.on("/collectnew", handlecollectnewdata);
  server.on("/backlight", handlebacklight);
  server.on("/reconnect", handlereconnect);

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("\nHTTP server started");

  ThingSpeak.begin(client);
  Serial.println("\nThingspeak client enabled started");
  

  Wire.begin();
  Wire.setClock(400000); //Increase to fast I2C speed!

  // inicializáljuk az utolsó 24óra mérését tartalamzó
  // tömböt;
  init_last24h();

  scan_I2Cbus();
  setup_bme280(i2caddr_bme280);

  lcd.setCursor(0, 3);
  lcd.print("BME280 sensor OK");

  // NTP Client
  timeClient.begin();


  // Csak akkor csinálunk mérést, ha sikeres volt a bme280 inicializálása
  if ( init_bme280 == 0 )
  {
    logtsp();
    update_last24h();
    update_local_sensor_data();
    request_remote_sensor_data();
  }

  delay(2000);
}

void request_remote_sensor_data()
{
  int i;
  char chr_hostname[100];

  for (i = 0; i < workingset.maxsensor; i++)
  {
    if ( sensor[i].hostname != "localhost" )
    {
      if ( sensor[i].status == SENSOR_IDLE )
      {
        sensor[i].status = SENSOR_CONNECTING;
        sensor[i].hostname.toCharArray(chr_hostname, sensor[i].hostname.length() + 1);
        Serial.print("\nConnecting to remote sensor: ");
        Serial.print(chr_hostname);
        Serial.print(":");
        if (sensor[i].client.connect(chr_hostname, 80)) {

          // Make a HTTP request:
          sensor[i].client.println("GET /t HTTP/1.1");
          sensor[i].client.println("Host: webhomero.display.local");
          sensor[i].client.println("Connection: close");
          sensor[i].client.println();
          sensor[i].lastconnectedmillis = millis();
          sensor[i].status = SENSOR_CONNECTED;
          Serial.println("\nHTTP request sent");
        } else {
          // if you didn't get a connection to the server:
          Serial.println("connection failed");
          sensor[i].client.stop();
          sensor[i].status = SENSOR_IDLE;
        }
      }
    }
  }
}

int remote_sensor_data_is_available()
{
  int i;
  for (i = 0; i < workingset.maxsensor; i++)
  {
    if ( sensor[i].hostname != "localhost" )
    {
      if ( sensor[i].client.available() )
      {
        sensor[i].status = SENSOR_HASDATA;
        Serial.print("\nSensor has data:");
        Serial.print(i);
        return i;
      }
    }
    //else Serial.println("\nSkipping localhost");
  }
  return -1;
}

void read_remote_sensor_data(int i)
{
  int loc1;
  Serial.print("\nTrying to read sensor: ");
  Serial.print(i);
  if ( sensor[i].status == SENSOR_HASDATA )
  {
    while (sensor[i].client.available()  ) {
      String line = sensor[i].client.readString();
      Serial.print(line);
      loc1 = line.indexOf("ID:");
      if ( loc1 != -1  )
      {
        sensor[i].datastring = String(line.substring(loc1));
        Serial.print("\nData:"); Serial.print(sensor[i].datastring);
      }
      else
      {
        Serial.println("Data not found...");
      }
      sensor[i].status = SENSOR_DATAREAD;
      Serial.print("\nSensor connection closing:");
      Serial.print(i);
      sensor[i].client.stop();
      sensor[i].status = SENSOR_IDLE;
    }

  }
}


void close_remote_sensor_connections()
{
  int i;
  for (i = 0; i < workingset.maxsensor; i++)
  {
    if ( !sensor[i].client.connected() )
    {
      sensor[i].status = SENSOR_IDLE;
      Serial.println("\nSensor connection closing:");
      Serial.println(i);
      sensor[i].client.stop();
    }
  }
}

void update_local_sensor_data()
{
  float tmin, tmax;
  int i;

  //Serial.print("\nUpdating local sensor");
  tmin = last24h[0].tmin;
  tmax = last24h[0].tmax;

  for (i = 1; i < 24; i++)
  {
    if ( tmin > last24h[i].tmin ) tmin = last24h[i].tmin;
    if ( tmax < last24h[i].tmax ) tmax = last24h[i].tmax;
  }

  sensor[0].t = readadjustedtemp();
  sensor[0].p = readadjustedpressure();
  sensor[0].h = readadjustedhumidity();
  sensor[0].tmin = tmin;
  sensor[0].tmax = tmax;
  sensor[0].lastconnectedmillis = millis();
  sensor[0].ssid = WiFi.SSID();
  sensor[0].rssi = WiFi.RSSI();

}


void update_remote_sensor_data(int sensorid)
{
  int loc1 = 0;
  int loc2 = 0;
  int i;
  String tmp;
  String inputstr;


  Serial.print("\n"); Serial.print(sensorid); Serial.print(" interpreting datastring");
  inputstr = sensor[sensorid].datastring;
  loc1 = inputstr.indexOf("ID:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 3, loc2);
    Serial.print("\nID:"); Serial.print(tmp);
    sensor[sensorid].id = tmp.toInt();
  } else Serial.println("ID: not found!");

  loc1 = inputstr.indexOf("T:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 2, loc2);
    Serial.print(" T:"); Serial.print(tmp); Serial.print(":");
    sensor[sensorid].t = tmp.toFloat();
    Serial.print(" converted to "); Serial.print(sensor[sensorid].t);

  } else  Serial.println("T: not found!");

  loc1 = inputstr.indexOf("H:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 2);
    tmp = inputstr.substring(loc1 + 2, loc2);
    Serial.print(" H:"); Serial.print(tmp);
    sensor[sensorid].h = tmp.toFloat();

  } else Serial.println("H: not found!");

  loc1 = inputstr.indexOf("P:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 2);
    tmp = inputstr.substring(loc1 + 2, loc2);
    Serial.print(" P:"); Serial.print(tmp);
    sensor[sensorid].p = tmp.toFloat();

  } else Serial.println("p: not found!");

  loc1 = inputstr.indexOf("tmin:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 5, loc2);
    Serial.print(" tmin:"); Serial.print(tmp);
    sensor[sensorid].tmin = tmp.toFloat();

  } else  Serial.println("tmin: not found!");

  loc1 = inputstr.indexOf("tmax:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 5, loc2);
    Serial.print(" tmax:"); Serial.print(tmp);
    sensor[sensorid].tmax = tmp.toFloat();

  } else Serial.println("tmax: not found!");

  loc1 = inputstr.indexOf("SSID:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 5, loc2);
    Serial.print(" SSID:"); Serial.print(tmp);
    sensor[sensorid].ssid = tmp;

  } else Serial.println("SSID: not found!");


  loc1 = inputstr.indexOf("RSSI:");
  if ( loc1 != -1 )
  {
    // Hol van a kovetkezo ;
    loc2 = inputstr.indexOf(";", loc1 + 1);
    tmp = inputstr.substring(loc1 + 5, loc2);
    Serial.print(" RSSI dBm:"); Serial.print(tmp);
    sensor[sensorid].rssi = tmp.toInt();

  } else Serial.println("RSSI: not found!");

}

void serial_print_menu()
{
  Serial.println(F("\n\n1. Enter new SSID"));
  Serial.println(F("2. Connect to Strongest Wifi"));
  Serial.println(F("3. Commit to eeprom"));
  Serial.println(F("\n\n"));
}


void loop(void) {
  int sensorid = 1;
  int val = LOW;
  String a_input;
  char ch_input;

  currentmilis = millis();

  server.handleClient();

  if (Serial.available() > 0) {
    a_input = Serial.readString(); // read the incoming String:
    ch_input = a_input.charAt(0);

    Serial.print("\n Input karakter:");
    Serial.println(a_input);
    Serial.println(ch_input);
  }

  switch (ch_input)
  {
    case '1': // uj SSID beadasa
      Serial.print("\nPlease enter a new SSID:");
      while (Serial.available() == 0);
      a_input = Serial.readString();
      a_input.toCharArray(myeprom.wifissid, 50);
      Serial.print(myeprom.wifissid);

      Serial.print("\nPlease enter a new wifi password:");
      while (Serial.available() == 0);
      a_input = Serial.readString();
      a_input.toCharArray(myeprom.wifipass, 50);
      Serial.print(myeprom.wifipass);

      knownssid[0] = myeprom.wifissid;
      knownpassword[0] = myeprom.wifipass;
      serial_print_menu();
      break;

    case '2': // Connect Strongest Wifi
      Serial.print("\n Connecting to strongest wifi");
      findandconnectstrongestwifi();
      serial_print_menu();
      break;

    case '3': // Commit to eeprom
      Serial.println("Commiting data to eeprom");
      eeprom_write();
      serial_print_menu();
      break;

      //default: // Something was entered but we do not know what...
      //  serial_print_menu();
  }

  // Csak akkor csinálunk mérést ha sikeres volt a BME280 inicialitalasa
  // es csatlakoztunk is
  if ( init_bme280 == 0 && WiFi.status() == WL_CONNECTED )
  {
    // Log to ThinkSpeak every 300.000ms = 5min
    if ( currentmilis - last_tsp_milis > REMOTE_UPDATE )
    {
      logtsp();
      update_last24h();
      update_local_sensor_data();
      request_remote_sensor_data();
      serial_print_menu();
    }

    //
    // Van adat a tavoli gépeknél
    sensorid = remote_sensor_data_is_available();
    if ( sensorid != -1 )
    {
      read_remote_sensor_data(sensorid);
      update_remote_sensor_data(sensorid);
      sensorid = -1;
      serial_print_menu();
    }
    // Blink LED every 2 sec
    if ( currentmilis - last_blink_milis > 2000 )
    {
      blink();
      if ( debug == 1 ) readadjustedtemp();
    }

    // 5 másodpercenként van helyi mérés
    //
    if ( currentmilis - last_lcdupdate_milis > 5000)
    {
      update_local_sensor_data();
      lcd_display_info();
      // ha ez nincs itt akar 1mp-t is kell varni a kov ido frissitesre
      lcd_display_time();
    }

    // 1 perc utan kikapcsoljuk a villanyt
    if ( currentmilis - last_backlight_on_millis > 60000)
    {
      lcd.noBacklight();
      lcd_displaymode = 0;
      lcd_vilagit = 0;
    }

    // Kapcsoljuk be gombnyomásra az LCD vilagitast
    // ha 5 masodpercen belul mar felkapcsoltuk akkor hagyjuk
    // val = LOW ha meg van nyomva
    val = digitalRead(BACKLIGHT_PIN);
    if ( val == LOW )
    {
      if ( currentmilis - last_backlight_on_millis > 1000 )
      {
        Serial.print("\nButton pressed");
        if ( lcd_vilagit == 0)
        {
          // Azert nyomta meg mert vilagitott
          Serial.print("\nButton pressed, lcd light on");
          lcd.backlight();
          last_backlight_on_millis = currentmilis;
          lcd_vilagit = 1;
        }
        else
        {
          // mar vilagítunk...
          // És megint megnyomja akkor
          // akkor a bonyolult kijelzést kéri
          // vagy eppen az egyszerűt
          Serial.print("\nButton pressed, lcd light already on, change display mode");
          last_backlight_on_millis = currentmilis;
          if ( lcd_displaymode == 0 ) lcd_displaymode = 1;
          else lcd_displaymode = 0;
          lcd_display_info();
        }


      }

    }


    // every 1 sec time on LCD is updated
    if ( currentmilis - last_lcd_time_update_millis > 1000)
    {
      lcd_display_time();
    }

  }
  else
  {
    // Error: Blink Led quickly every 0.5 sec
    workingset.villogas = 1;
    if ( currentmilis - last_blink_milis > 500)
    {
      blink();
    }
    // Ha nem kapcsolodunk pl. nem sikerult az setupban
    // akkor ket percenkent azert probalkozunk
    if (  WiFi.status() != WL_CONNECTED && currentmilis - last_wificonnect_millis > 120000 )
    {
      Serial.print("\nTrying to connect to wifi");
      last_wificonnect_millis = currentmilis;
      findandconnectstrongestwifi();
    }


  }

}

