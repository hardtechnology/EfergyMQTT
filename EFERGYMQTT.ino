//DATA Out from Receiver in pin D3 (remember 3.3v!!)
//Version 7.0 - 18th December 2016
//Based on code from:
//http://rtlsdr-dongle.blogspot.com.au/2013/11/finally-complete-working-prototype-of.html
//http://electrohome.pbworks.com/w/page/34379858/Efergy%20Elite%20Wireless%20Meter%20Hack

//This requires the following Libraries to be installed (see includes for links)
// ArduinoJSON 5.8.0
// ESP8266 1.0.0
// WiFiManager 0.12.0
// PubSubClient 2.6.0

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <SPI.h>
#include <PubSubClient.h>         //https://github.com/knolleary/pubsubclient
#include <limits.h>               //Required for our version of PulseIn
#include "wiring_private.h"       //Required for our version of PulseIn
#include "pins_arduino.h"         //Required for our version of PulseIn

#define VERSION_MAJOR 7
#define VERSION_MINOR 5

#define DEBUG 0
bool MQTT_Publish_mA = false;
bool MQTT_Publish_LOST = false;

//MQTT Server Variables - including any defaults - redefined later - so update in both locations
char mqtt_server[60];
char mqtt_port[8] = "1883";
char mqtt_username[60] = "";
char mqtt_password[60] = "";
char mqtt_clientname[60] = "EfergyMQTT";
char mqtt_willtopic[60] = "EfergyMQTT/Online";
char mqtt_efergytopic[60] = "EfergyMQTT";
char efergy_blacklist[128] = "{\"blacklist\":[]}";
char efergy_whitelist[128] = "{\"whitelist\":[]}";
char efergy_voltage[8] = "230"; //Set default Voltage for our Wattage measurements
int efergy_voltage_int = atoi(efergy_voltage);
char buf1[40]; //Temporary buffers for charactor concatenating, etc...
char buf2[40]; //Temporary buffers for charactor concatenating, etc...

//Enable us to get the VCC using  ESP.getVcc() 
ADC_MODE(ADC_VCC);

// Callback function header
void callback(char* topic, byte* payload, unsigned int length);

WiFiClient espClient;
//PubSubClient MQTTclient;
PubSubClient MQTTclient("255.255.255.255",1883, callback, espClient);

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

#define in 16          //Input pin (DATA_OUT from A72C01) on pin 2 (D3) (Pin D0 on the Wemos D1 mini)
#define MAXTX 32       //Max Transmitters we can handle (Array size) - each additional tranmitter needs 6 bytes of RAM
#define limit 67      //67 for E2 Classic - bits to Rx from Tx
#define fullupdatepkt 600 //Number of packets between full updates - 600 = 60 mins @ 6s

int p = 0;
int i = 0;
int prevlength = 0;
unsigned char bytearray[8];      //Stores the decoded 8 byte packet
bool startComm = false;          //True when a packet is in the process of being received
unsigned long processingTime;    //needs to be unsigned long to be compatible with PulseIn()
unsigned long incomingTime[limit]; //stores processing time for each bit
unsigned char bytedata;
unsigned long packetTO;           //Packet timeout
char tbyte = 0;                   //Used for storing the checksum
unsigned int TXarrayID;
int bitpos;
int bytecount; //Number of Bytes processed
int dbit;
boolean flag;
boolean MQTTloopreturn; //Use to capture the return of the MQTT PubSubClient loop routine
int looping;
int VCC_V;
int VCC_mV;
unsigned long offlineupdate;       //Millis counter for status update
unsigned long milliamps;           //Calculated current in mA - currently being processed
unsigned long watts;               //Calculated power in Watts - currently being processed
unsigned long TransmitterID;       //Identifier for the Transmitter - currently being processed
unsigned long TX_id[MAXTX];        //unsigned int - Transmitter array ID
unsigned int TX_interval[MAXTX];   //unsigned int - Transmitter array ID - used to calc offline length
unsigned long TX_age[MAXTX];       //milliseconds when last updated (stores millis()) - used for offline check
unsigned char TX_battery[MAXTX];   //Status of the Transmitter Batteries
unsigned int TX_update[MAXTX];     //Run full update every X packets received
unsigned int TX_link[MAXTX];       //Link Bit reference
unsigned int TX_lost[MAXTX];       //Number of packets lost to trigger OFFLINE
unsigned long MQTT_retry = 0;      //Stores time between MQTT reconnections
bool MQTT_Connected = false;
char mqtt_subscribe_buff[100];

//unsigned long bitTimer;

void setup() {
  Serial.begin(74880);
  pinMode(in, INPUT);
  void RESET_TX_DB(); //Ensure our in Memory Transmitter Database is cleared (zeroed)
  
  //This ensures our serial has had time to get ready  
  yield();
  delay(200);
  
  Serial.println("\nBOOTING Please Wait...");

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.print("Mounting FS...");
  delay(50);

  if (SPIFFS.begin()) {
    Serial.println("Done.");
    if (SPIFFS.exists("/config.json")) { //Check if we have a pre-existing configuration saved
      //file exists, reading and loading
      Serial.print("Reading Config...");
      File configFile = SPIFFS.open("/config.json", "r"); //Open the config file
      if (configFile) {
        Serial.print("File Open...");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size); //Save contents of config file into buf
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get()); //Decode the config file into JSON config
        if (json.success()) {
          Serial.println("JSON Read OK.");
          if (DEBUG) { json.printTo(Serial); Serial.println(""); }
          //Copy our parameters into arrays
          if (json.containsKey("mqtt_server")) { strcpy(mqtt_server, json["mqtt_server"]); }
          if (json.containsKey("mqtt_port")) { strcpy(mqtt_port, json["mqtt_port"]); }
          if (json.containsKey("mqtt_username")) { strcpy(mqtt_username, json["mqtt_username"]); }
          if (json.containsKey("mqtt_password")) { strcpy(mqtt_password, json["mqtt_password"]); }
          if (json.containsKey("mqtt_clientname")) { strcpy(mqtt_clientname, json["mqtt_clientname"]); }
          if (json.containsKey("mqtt_willtopic")) { strcpy(mqtt_willtopic, json["mqtt_willtopic"]); }
          if (json.containsKey("mqtt_efergytopic")) { strcpy(mqtt_efergytopic, json["mqtt_efergytopic"]); }
          if (json.containsKey("efergy_voltage")) { strcpy(efergy_voltage, json["efergy_voltage"]); }
          efergy_voltage_int = atoi(efergy_voltage);
          
          if ( efergy_voltage_int < 5 || efergy_voltage_int > 1000 ) { //Set Default Voltage if out of range
            strcpy(efergy_voltage,"230");
            if (DEBUG) { Serial.println("!!!Setting Default Voltage - range out of bounds"); }
          }
          //Display Configured Settings on our Serial Port
          Serial.print("MQTT Server:        ");
          Serial.println(mqtt_server);
          Serial.print("MQTT Port:          ");
          Serial.println(mqtt_port);
          Serial.print("MQTT Client Name:   ");
          Serial.println(mqtt_clientname);
          Serial.print("MQTT Will Topic :   ");
          Serial.println(mqtt_willtopic);
          Serial.print("MQTT Efergy Topic : ");
          Serial.println(mqtt_efergytopic);
          if ( strlen(mqtt_username) > 0 ) {
            Serial.print("MQTT Username:      ");
            Serial.println(mqtt_username);
            Serial.print("MQTT Password:      ");
            //for (int pw=0;pw<strlen(mqtt_password);pw++) { Serial.print("*"); } //Indicate number of characters in password
            Serial.print("************");
            Serial.println("");
          }
          //JSON to Serial for Voltage
          Serial.print("{\"VOLTAGE\":");
          Serial.print(efergy_voltage_int);
          Serial.println("}");

          
        } else {
          Serial.println("failed to load json config");
        }
      } else {
          Serial.println("failed to load config file.");
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

    WiFiManagerParameter custom_text_MQTT1("MQTT Server Configuration<br>");
  WiFiManagerParameter custom_mqtt_server("Server", "mqtt server", mqtt_server, sizeof(mqtt_server));
  WiFiManagerParameter custom_mqtt_port("Port", "mqtt port", mqtt_port, sizeof(mqtt_port));
  WiFiManagerParameter custom_mqtt_username("Username", "mqtt username", mqtt_username, sizeof(mqtt_username));
  WiFiManagerParameter custom_mqtt_password("Password", "mqtt password", mqtt_password, sizeof(mqtt_password));
  WiFiManagerParameter custom_mqtt_clientname("ClientName", "mqtt clientname", mqtt_clientname, sizeof(mqtt_clientname));
  WiFiManagerParameter custom_mqtt_willtopic("WillTopic", "mqtt willtopic", mqtt_willtopic, sizeof(mqtt_willtopic));
  WiFiManagerParameter custom_mqtt_efergytopic("EfergyTopic", "mqtt efergytopic", mqtt_efergytopic, sizeof(mqtt_efergytopic));
  WiFiManagerParameter custom_text_EFERGY1("<br>Efergy Configuration<br>");
  WiFiManagerParameter custom_efergy_voltage("Voltage", "efergy voltage", efergy_voltage, sizeof(efergy_voltage));
  
  Serial.print("ESP8266 VCC:");
  VCC_V = ESP.getVcc() / 1000;
  VCC_mV = ESP.getVcc() - (VCC_V * 1000);
  Serial.print(VCC_V);
  Serial.print(".");
  Serial.print(VCC_mV);
  Serial.println("V");
  
  Serial.println("Starting WifiManager...");
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);  //set config save notify callback

  //Set Custom Parameters
  wifiManager.addParameter(&custom_text_MQTT1);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_clientname);
  wifiManager.addParameter(&custom_mqtt_willtopic);
  wifiManager.addParameter(&custom_mqtt_efergytopic);
  wifiManager.addParameter(&custom_text_EFERGY1);
  wifiManager.addParameter(&custom_efergy_voltage);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //set minimum quality of signal so it ignores AP's under that quality defaults to 8%
  //wifiManager.setMinimumSignalQuality(8);
  
  //sets timeout until configuration portal gets turned off useful to make it all retry or go to sleep in seconds
  wifiManager.setTimeout(300);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("EfergyMQTT", "TTQMygrefE")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_clientname, custom_mqtt_clientname.getValue());
  strcpy(mqtt_willtopic, custom_mqtt_willtopic.getValue());
  strcpy(mqtt_efergytopic, custom_mqtt_efergytopic.getValue());
  strcpy(efergy_voltage, custom_efergy_voltage.getValue());
  efergy_voltage_int = atoi(efergy_voltage);
  
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_clientname"] = mqtt_clientname;
    json["mqtt_willtopic"] = mqtt_willtopic;
    json["mqtt_efergytopic"] = mqtt_efergytopic;
    json["efergy_voltage"] = efergy_voltage;
    
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) { Serial.println("failed to open config file for writing");  }
    
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    Serial.println("\nRebooting. Please Wait...");
    delay(1000);
    ESP.restart();
    delay(500); //We should NEVER get here
    Serial.println("REBOOT FAILED");
  }
  
  offlineupdate = 60000; //Set first offline status update after approx 60 seconds
  
  MQTTclient.setServer(mqtt_server, atoi(mqtt_port)); //#################Need to Convert Configured IP
  mqtt_pubsubclient_reconnect(); //Make sure we are connected to our MQTT Server

  if (DEBUG) {
    Serial.println("DEBUG=ON - T=Timeout, S=Start, b=bit, o=Rxtimeout, L=loop routine, E=End of Packet");
  }
  Serial.println("Running...");
}


void loop() {
  //Loop through and store the high pulse length
  processingTime = Efergy_pulseIn(in,HIGH,10000);  //Returns unsigned long - 10 millisecond timeout
  if (processingTime > 480UL ) {
    startComm = true; //If the High Pulse is greater than 450uS - mark as start of packet
    incomingTime[0] = processingTime;
    p = 1;
    if (DEBUG) { Serial.print("\nSb");}
    //Process individual bits
    while ( startComm ) {
      processingTime = Efergy_pulseIn(in,HIGH,600);  //Returns unsigned long - 300uS timeout  
      if ( processingTime == 0 ) { //With An Active Signal we will basically never timeout
        if (DEBUG){Serial.print("T");}
        if (DEBUG){prevlength = p;} //Store this packet length in prevlength for debugging
        looping = looping + 200;
        RESET_PKT(); 
      } else if ( processingTime > 480UL ) { //Start of new packet
        incomingTime[0] = processingTime;
        p = 1;
      } else if (processingTime > 20 ) {
        incomingTime[p] = processingTime; //Save time of each subsequent bit received
        p++;
        //If packet has been received (67 bits) - mark it to be processed
        if (DEBUG){Serial.print("b"); }
        if (p > limit) {
          startComm = false;
          if (DEBUG){prevlength = p;} //Store this packet length in prevlength for debugging
          yield();
          flag = true;
          if (DEBUG){Serial.print("E"); }
        } //end of limit if
      } //end of proctime > 30
    } //End of StartComm == true
    
  } else if ( processingTime == 0 ) { //End of packet RX
    looping = looping + 400;
    yield();
  }
  
  //If a complete packet has been receied (flag = 1) then process the individual bits into a byte array
  if ( flag == true ) {
    dbit = 0;
    bitpos = 0;
    bytecount = 0;
    bytedata = 0;
    for (int k = 1; k <= limit; k++) { //Start at 1 because the first bit (0) is our long 500uS start
      if (incomingTime[k] != 0) {
        if (incomingTime[k] > 20UL ) { //Original Code was 20 - smallest is about 70us - so 40 to be safe with loop overheads
          dbit++;
          bitpos++;
          bytedata = bytedata << 1;
          if (incomingTime[k] > 85UL) { // 0 is approx 70uS, 1 is approx 140uS
            bytedata = bytedata | 0x1;
          }
          if (bitpos > 7) {
            bytearray[bytecount] = bytedata;
            bytedata = 0;
            bitpos = 0;
            bytecount++;
          }
        }
      }
    }
    
    if ( bytecount == 8 && checksumOK(bytearray)) { //Process Received Packet  
      flag = false;
      TransmitterID = (((unsigned int)bytearray[1] * 256) + (unsigned int)bytearray[2]); //Make 16-bit Transmitter ID
      //Transmitter update JSON
      TXarrayID = GetTXarrayID(TransmitterID,bytearray[0]); //Get our Array ID for the Transmitter
      if (TXarrayID) {
        TX_update[TXarrayID]++;
        if (TX_update[TXarrayID] > fullupdatepkt) {
          TX_update[TXarrayID] = 0;
        }
        TX_age[TXarrayID] = millis();
        TX_lost[TXarrayID] = 0;
        milliamps = ((1000 * ((unsigned long)((bytearray[4] * 256) + (unsigned long)bytearray[5]))) / power2(bytearray[6]));
        watts = ( milliamps * efergy_voltage_int ) / 1000;
        Serial.print(F("{\"TX\":"));
        Serial.print(TransmitterID);
        Serial.print(F(",\"W\":"));
        Serial.print(watts);
        if (MQTT_Publish_mA) {
          Serial.print(F(",\"mA\":"));
          Serial.print(milliamps);
        }
        if ( watts > 25000 || milliamps > 100000 ) { //If our calculations are out of normal boundaries - log the full data
          MQTT_RAW(bytearray);
        }
        //Transmit Intervals
        if ( (bytearray[3] & 0x30) == 0x10) { //xx11xxxx = 12 seconds
          i = 12;
        } else if ( (bytearray[3] & 0x30) == 0x20) { //xx10xxxx = 18 seconds
          i = 18;
        } else if ( (bytearray[3] & 0x30) == 0x00) { //xx00xxxx = 6 seconds
          i = 6;
        } else {
          i = 0;
        }
        
        if ( TX_interval[TXarrayID] != i || TX_update[TXarrayID] == fullupdatepkt) {
          Serial.print(F(",\"I\":"));
          Serial.print(i);
          TX_interval[TXarrayID] = i;
          MQTT_Pub("Interval",i);
        }
        //Print out if the locate(LINK) bit is set on the transmitter - include Interval
        if ( (bytearray[3] & 0x80) == 0x80 && TX_link[TXarrayID] == false) {
          Serial.print(F(",\"LINK\":1"));
          if ( MQTT_Connected == true ) {
            sprintf(buf1,"%s/Link",mqtt_clientname);
            sprintf(buf2,"%lu",TransmitterID);
            MQTTclient.publish(buf1,buf2,true);
          }
          TX_link[TXarrayID] = true;
        } else if ( (bytearray[3] & 0x80) == 0x00 ) {
          TX_link[TXarrayID] = false;
        }
        
        //Check the Battery status of the Transmitter - False means low battery
        if ( (bytearray[3] & 0x40) == 0x40) {
          i = 2;  //Battery - 2=ok, 1=bad, 0=unknown
        } else {
          i = 1;
        }
        
        if ( TX_battery[TXarrayID] != i || TX_update[TXarrayID] == fullupdatepkt  ) {
          TX_battery[TXarrayID] = i;
          if ( i == 2 ) {
            MQTT_Pub("BattOK",true);
            Serial.print(F(",\"BOK\":true"));
          } else {
            MQTT_Pub("BattOK",false);
            Serial.print(F(",\"BOK\":false"));
          }
        }
        
        //End of JSON Update string
        Serial.println("}");
        
        //Publish Standard MQTT Updates
        if (MQTT_Publish_mA) {MQTT_Pub("mA",milliamps); }
        MQTT_Pub("Watt", watts);

        
      } else {
        //Invalid Transmitter ID - most likely we are overloaded
        if (DEBUG){Serial.print("INVID");}
        RESET_PKT();
      }
    } else {
      //We failed the Checksum test on an 8 byte Packet
      flag = false;
      if (DEBUG){
        Serial.print("CS");
        Serial_BitTimes(limit);
        Serial_RAW(bytearray);
      }
      RESET_PKT();
    }

  } //End of FLAG == true Routine


  //Run regular Routines - only if not receiving a packet - runs at least once per second on pulse timeout
  if ( startComm == false) {
    if (looping > 4000 ) {
      looping = 0;
      if (DEBUG) {Serial.print("L");}
      //Update Offline Transmitters
      if ( offlineupdate < millis() ) {
        offlineupdate = (millis() + 10000UL); //Set  offline status update every 30 seconds
        i = 1;
        while ( i < MAXTX ) {
          if (TX_id[i] > 0 ) {
            if ( TX_lost[i] >= 3 ) {  //If we miss 3 transmittions - mark offline
              Serial.print(F("{\"OFF\":"));
              Serial.print(TX_id[i]);
              Serial.println("}");
              TransmitterID = TX_id[i];
              TX_id[i] = 0;
              MQTT_Pub("Online",false); //Mark Transmitter as offline
              MQTT_Pub("Watt","");      //Clear Retained message
              if (MQTT_Publish_mA) {MQTT_Pub("mA",""); }  //Clear Retained message
              MQTT_Pub("Interval","");  //Clear Retained message
            }
          }
          i++;
        }
      }
      //End of Offline Routine
  
      //Display Lost Packets
      i = 1;
      while ( i < MAXTX ) {
        if (TX_id[i] > 0 ) {   
          if ( (  TX_age[i] + ( (unsigned long)TX_interval[i] * 1010 ) ) < millis() ) {  //If we miss a transmission note it
            TransmitterID = TX_id[i];
            TX_age[i] = millis();
            TX_lost[i]++;
            Serial.print(F("{\"TX\":"));
            Serial.print(TransmitterID);
            Serial.print(",\"LOST\":");
            Serial.print(TX_lost[i]);
            Serial.println("}");
            if (MQTT_Publish_LOST) { //If we publish a NULL for Lost Messages
              MQTT_Pub("Watt","");      //Clear Retained message
              if (MQTT_Publish_mA) {MQTT_Pub("mA",""); }  //Clear Retained message
            }
          }
        }
        i++;
      }
      //End of Lost packet routine    
      
      //Keep connection to MQTT Server
      if ( !MQTTclient.connected()) { mqtt_pubsubclient_reconnect(); }
      MQTTclient.loop();
      //End of MQTT Keep Alive
      yield();
    
    
    }
  }   //End of - Not receiving a packet routine
  looping++;
} //End of MAIN LOOP


//Power of a number - normal Arduino function uses float and 2KB of Flash - this is much smaller
unsigned long power2 (unsigned char exp1) {
  unsigned long pow1 = 1048576;
  exp1 = exp1 + 5;
  for (int x = exp1; x > 0; x--) {
    pow1 = pow1 / 2;
  }
  return pow1;
}


//Lookup Transmitter ID in our array, or create one if new
unsigned int GetTXarrayID(unsigned long TransmitterID, unsigned char TXtype) {
  unsigned int w = 1;
  unsigned int ID = 0;
  while ( ID == 0 && w < MAXTX ) {
    if ( TX_id[w] == TransmitterID ) { ID = w;  }
    w++;
  }
  if ( ID == 0 ) {
    w = 1;
    while ( w < MAXTX && ID == 0 ) {
      if ( TX_id[w] == 0 ) {
        ID = w;
        TX_id[w] = TransmitterID;
        TX_age[w] = millis();
        TX_battery[w] = 0;
        TX_update[w] = 0;
        TX_lost[w] = 0;
        TX_link[w] = false;
        Serial.print(F("{\"NEW\":"));
        Serial.print(TransmitterID);
        Serial.print(",\"TYPE\":");
        Serial.print(TXtype);
        Serial.println("}");
        MQTT_Pub("Online",true);
        MQTT_Pub("Type",TXtype);
      }
      w++;
    }
  }
  return ID;
}


bool checksumOK(unsigned char bytes[]) {
  unsigned char tbyte = 0;
  bool OK1 = false;
  for (int cs = 0; cs < 7; cs++) {
    tbyte += bytes[cs];
  }
  tbyte &= 0xff;
  if ( tbyte == bytes[7] ) {
    if ( bytes[0] == 7 || bytes[0] == 9 ) {
      OK1 = true;  
    }
    
  }
  return OK1;
}


void RESET_PKT() {
  startComm = false;
  //memset (incomingTime, -1, sizeof(incomingTime));
  memset (incomingTime, 0, sizeof(incomingTime));
  memset (bytearray, 0, sizeof(bytearray));
}


void Serial_BitTimes(int z) {
  Serial.print("{\"BituSec\":[");
  for (int y=0;y<=z;y++) {
    Serial.print(incomingTime[y]);
    if (y < z ) { Serial.print(","); } else { Serial.println("]}"); }
  }
}


void MQTT_Pub(char Topic[], unsigned long value) {  //MQTT Publish
  char topic[32];
  sprintf(topic,"%s/%lu/%s",mqtt_efergytopic,TransmitterID,Topic);
  char buf[12];
  sprintf(buf,"%lu",value);
  if ( MQTT_Connected == true ) {
    MQTTclient.publish(topic,buf,true);
  }
}


void MQTT_Pub(char Topic[], int value) {  //MQTT Publish
  char topic[32];
  sprintf(topic,"%s/%lu/%s",mqtt_efergytopic,TransmitterID,Topic);
  char buf[8];
  sprintf(buf,"%d",value);
  if ( MQTT_Connected == true ) {
    MQTTclient.publish(topic,buf,true);
  }
}


void MQTT_Pub(char Topic[], bool value) {  //MQTT Publish
  char topic[32];
  sprintf(topic,"%s/%lu/%s",mqtt_efergytopic,TransmitterID,Topic);
  if ( MQTT_Connected == true ) { 
    if ( value ) { MQTTclient.publish(topic,"true",true); } else { MQTTclient.publish(topic,"false",true); }
  }
}

void MQTT_Pub(char Topic[], char Value[]) {  //MQTT Publish
  char topic[32];
  sprintf(topic,"%s/%lu/%s",mqtt_efergytopic,TransmitterID,Topic);
  if ( MQTT_Connected == true ) {
    MQTTclient.publish(topic,Value,true);
  }
}


void MQTT_RAW(unsigned char bytes[]) {
    char buf[40];
    sprintf(buf,"{\"RAW\":[%d,%d,%d,%d,%d,%d,%d,%d]}",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7]);
    MQTT_Pub("JSON",buf);
}


void Serial_RAW(unsigned char bytes[]) {
    if (!checksumOK(bytes)) { 
      char buf[40];
      sprintf(buf,"{\"RAW\":[%d,%d,%d,%d,%d,%d,%d,%d]}",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7]);
      Serial.print(buf);
    }
}


void mqtt_pubsubclient_reconnect() {
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    // Attempt to connect
    if ( MQTT_retry < millis() ) {
      if (MQTTclient.connect(mqtt_clientname,mqtt_username,mqtt_password,mqtt_willtopic,0,1,"false")) {
        MQTT_Connected = true;
        RESET_TX_DB();
        Serial.println("{\"MQTT\":\"CONNECTED\"}");
        
        MQTTclient.publish(mqtt_willtopic,"true");    // Once connected, publish an announcement...
        MQTTclient.loop();
        yield();
        sprintf(buf1,"%s/Voltage",mqtt_efergytopic);
        MQTTclient.publish(buf1,efergy_voltage);
        MQTTclient.loop();
        sprintf(buf1,"%s/ESP8266_VCC",mqtt_clientname);
        sprintf(buf2,"%d.%d",VCC_V,VCC_mV);
        MQTTclient.publish(buf1,buf2);
        MQTTclient.loop();
        yield();
        sprintf(buf1,"%s/CONFIG/#",mqtt_clientname);
        MQTTclient.subscribe(buf1);
        MQTTclient.loop();
        yield();
      } else {
        MQTT_Connected = false;
        Serial.print("{\"MQTT\":\"");
        switch (MQTTclient.state()) { // Messages from the SubPub API information
          case -4: Serial.print(F("CONNECTION_TIMEOUT")); break;
          case -3: Serial.print(F("CONNECTION_LOST")); break;
          case -2: Serial.print(F("CONNECT_FAILED")); break;
          case -1: Serial.print(F("DISCONNECTED")); break;
          case 0: Serial.print(F("CONNECTED")); break;
          case 1: Serial.print(F("CONNECT_BAD_PROTOCOL")); break;
          case 2: Serial.print(F("CONNECT_BAD_CLIENT_ID")); break;
          case 3: Serial.print(F("CONNECT_UNAVAILABLE")); break;
          case 4: Serial.print(F("CONNECT_BAD_CREDENTIALS")); break;
          case 5: Serial.print(F("CONNECT_UNAUTHORIZED")); break;
        }
        Serial.println("\"}");
        Serial.println("Waiting 20 seconds to retry connection");
        MQTT_retry = millis() + 20000; // Wait 20 seconds before retrying
      }
    }
  }
}

//Reset Variables in the Transmitter information arrays
void RESET_TX_DB() {
  for (i = 0; i < MAXTX; i++) { //Initialise Transmitter ID array
    TX_id[i] = 0;
    TX_battery[i] = 0;
    TX_update[i] = 0;
    TX_link[i] = false;
  }
}

//Publish our software version over MQTT
#define MQTT_Pub_VERSION() \
    sprintf(buf1,"%s/VERSION",mqtt_clientname); \
    sprintf(buf2,"%d.%d",VERSION_MAJOR,VERSION_MINOR); \
    MQTTclient.publish(buf1,buf2); \
    MQTTclient.loop(); \
  


// Callback function for MQTT Subscription on '%clientname%'
void callback(char* topic, byte* payload, unsigned int length) {
  //Convert Payload to Null terminated Array read to convert to String
  memset (mqtt_subscribe_buff, 0, sizeof(mqtt_subscribe_buff));
  for(int i=0; i<length; i++) {
    mqtt_subscribe_buff[i] = payload[i];
  }
  mqtt_subscribe_buff[i] = '\0';  
  String mqttSubValue = String(mqtt_subscribe_buff);

  Serial.print("{\"MQTT_SUB\":\"");
  Serial.print(topic);
  Serial.print("\",\"VALUE\":\"");
  Serial.print(mqttSubValue);
  Serial.println("\"}");

  //RESET TOPIC
  sprintf(buf1,"%s/CONFIG/%s",mqtt_clientname,"RESET");
  if ( strcmp(topic,buf1) == 0 ) {
    if ( strcmp(mqtt_subscribe_buff,"CONFIG") == 0 ) { //RESET Configuration so we restart into Access Point Config mode
      WiFiManager wifiManager;
      wifiManager.resetSettings();
      delay(500);
      Serial.println("Resetting...");
      delay(500);
      ESP.restart();
      delay(500);
    }
    if ( strcmp(topic,buf1) == 0 && strcmp(mqtt_subscribe_buff,"RESTART") == 0 ) {
      Serial.println("Resetting...");
      delay(500);
      ESP.restart();
      delay(500);
    }
  }
  
  //VCC TOPIC
  sprintf(buf1,"%s/CONFIG/%s",mqtt_clientname,"VCC");
  if ( strcmp(topic,buf1) == 0 ) {
    VCC_V = ESP.getVcc() / 1000;
    VCC_mV = ESP.getVcc() - (VCC_V * 1000);
    sprintf(buf1,"%s/VCC",mqtt_clientname);
    sprintf(buf2,"%d.%d",VCC_V,VCC_mV);
    MQTTclient.publish(buf1,buf2);
    MQTTclient.loop();
  }
  
  //SET Voltage
  sprintf(buf1,"%s/CONFIG/%s",mqtt_clientname,"EFERGYVOLTAGE");
  if ( strcmp(topic,buf1) == 0 ) {
    int newvoltage = atoi(mqtt_subscribe_buff);
    if (newvoltage > 5 && newvoltage < 999 ) {
      efergy_voltage_int = newvoltage;
      Serial.print("{\"VOLTAGE\":");
      Serial.print(efergy_voltage_int);
      Serial.println("}");
      UpdateConfig(); //Update the permanent Configuration file
    }
  }


  //SET MQTT Server
  sprintf(buf1,"%s/CONFIG/%s",mqtt_clientname,"MQTTSERVER");
  if ( strcmp(topic,buf1) == 0 ) {
    int newvoltage = atoi(mqtt_subscribe_buff);
    if (newvoltage > 5 && newvoltage < 999 ) {
      efergy_voltage_int = newvoltage;
      Serial.print("{\"VOLTAGE\":");
      Serial.print(efergy_voltage_int);
      Serial.println("}");
      UpdateConfig(); //Update the permanent Configuration file
    }
  }

  //SET mA output true/false
  sprintf(buf1,"%s/CONFIG/%s",mqtt_clientname,"MILLIAMP");
  if ( strcmp(topic,buf1) == 0 ) {
    Serial.print("OUTPUT: MilliAmps");
    if ( atoi(mqtt_subscribe_buff)  ) {
      MQTT_Publish_mA = true;
      Serial.println(" Enabled");
    } else {
      MQTT_Publish_mA = false;      
      Serial.println(" Disabled");
    }
  }

  //GET Software Version
  sprintf(buf1,"%s/CONFIG/VERSION",mqtt_clientname);
  if ( strcmp(topic,buf1) == 0 ) {
    MQTT_Pub_VERSION();
  }
  
  yield(); 
}

//Used for Accurate Clock cycle timing for receiving the bits from the Efergy Receiver
#define RSR_CCOUNT(r)     __asm__ __volatile__("rsr %0,ccount":"=a" (r))
static inline uint32_t get_ccount(void)
{
    uint32_t ccount;
    RSR_CCOUNT(ccount);
    return ccount;
}

#define EFERGY_PINWAIT(state) \
    while (digitalRead(pin) != (state)) { if (get_ccount() - start_cycle_count > timeout_cycles) { return 0; } }

    

// max timeout is 27 seconds at 160MHz clock and 54 seconds at 80MHz clock
unsigned long Efergy_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    if (timeout > 1000000) { timeout = 1000000; }
    const uint32_t timeout_cycles = microsecondsToClockCycles(timeout);
    const uint32_t start_cycle_count = get_ccount();
    EFERGY_PINWAIT(!state);
    EFERGY_PINWAIT(state);
    const uint32_t pulse_start_cycle_count = get_ccount();
    EFERGY_PINWAIT(!state);
    return clockCyclesToMicroseconds(get_ccount() - pulse_start_cycle_count);
}


//Permanently Update our configuration file parameters
void UpdateConfig() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  //json["mqtt_server"] = mqtt_server;
  //json["mqtt_port"] = mqtt_port;
  //json["mqtt_username"] = mqtt_username;
  //json["mqtt_password"] = mqtt_password;
  //json["mqtt_clientname"] = mqtt_clientname;
  //json["mqtt_willtopic"] = mqtt_willtopic;
  //json["mqtt_efergytopic"] = mqtt_efergytopic;
  json["efergy_voltage"] = efergy_voltage_int;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) { Serial.println("failed to open config file for writing");  }
  Serial.println("Writing Updated Configuration");
  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

