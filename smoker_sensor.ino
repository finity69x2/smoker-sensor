#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <Arduino.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include "Adafruit_MAX31855.h"

// ------------------- General Setup Code ------------------------------

int ledPinBlue = D5;                // Blue LED connected to digital pin D5 - On when wifi connected
int ledPinRed = D2;                // Red LED connected to digital pin D2 - On when taking a sample (activity indicator)

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousMillis = 0;    // will store last time sensor was updated

// Updates probe readings every 10 seconds
const long interval = 10000;  

// used for averaging code
const int SAMPLE_NUMBER = 10;

WiFiClient espClient;
PubSubClient client(espClient);

// ------------------- Thermocouple Setup Code -------------------------

// MAX31855 pins     ESP8266 pins
// GND              GND
// VCC              3.3V
// SCK              GPIO12 (D6)
// CS               GPIO13 (D7)
// SO               GPIO15 (D8)
//
// pin order: (sck, cs, so)
Adafruit_MAX31855 tSmoker(D6, D7, D8);

// current box temperature, updated in loop()
float ts = 0.0;

double correctedTempF = 0;
double rawTempF = 0;

float TcoupleAve;
float TcoupleAverage  = 0;            // Holds the average voltage measurement
float TcoupleScaledAverage = 0;       // Holds Scaled value of Cm reading after Offset applied
float TcoupleTotal = 0;               // Total of all Cm readings in averaging code
int   TcoupleSamples[SAMPLE_NUMBER];  // Array to hold each voltage measurement
const float TcOffset = 1.0;           // If Needed


// -------------------- Thermistor B Setup Code ------------------------

// visit here for calculating the Co-efficients and Beta Values if not supplied by the vendor
//
// https://www.thinksrs.com/downloads/programs/Therm%20Calc/NTCCalibrator/NTCcalculator.htm
//

// Pin Setup
int ThermistorPin = A0;

// Constants
const float Cmax = 1024.102; // max counts for analog input pin @ 3.3vdc

// Put your thermister constant values here
const float Rb = 99600.0; // Value of voltage divider resistor
const float Offset = 1.0; // Was .9345 for thermistor A

// Only used if you use the Beta Value for calculation:
//const float Beta = 3936.28; 
//const float Ro = 99406.98; // Ro = Room Temperature (25C/77F) Resistance of the thermister
//const float To = 298.15;   // To = Room Temperature in K (25C/77F) - don't change

// Only used if you use the Steinhart-Hart Coefficients for calculation:
const float C1 = 0.8949830252e-3; // C1 = coefficient A
const float C2 = 1.909803133e-4;  // C2 = coefficient B
const float C3 = 1.688635553e-7;  // C3 = coefficient C

//put your linearization equation constants here
const int HighCutoff = 115; // value above which you use the high linearization equation
const float Mhigh = .26;    // slope value for the high side equation
const float Mlow = .05;     // slope value for the low side equation
const float Bhigh = 38.23;  // y-intercept value for the high side equation
const float Blow = 11.68;   // y-intercept value for the low side equation


// Measured and Calculated Values
float Rmave, logRmave, Tcave, Tfave, TfLinear; //, Tcbeta, Tfbeta;
float CmAverage  = 0;            // Holds the average voltage measurement
float CsAverage = 0;             // Holds Scaled value of Cm reading after Offset applied
float CmTotal = 0;               // Total of all Cm readings in averaging code
int   CmSamples[SAMPLE_NUMBER];  // Array to hold each voltage measurement


// current food temperature, updated in loop()
float tf = 0.0;


// -------------------- WifiManager Setup Code -----------------------

#define TRIGGER_PIN 0     // used to call the WifiManager on-demand

//define your default values here, if there are different values in config.json, they are overwritten.
//bool useMqtt = false;
//char mqtt_server[40];
//char mqtt_port[6];
//char mqtt_user[40];
//char mqtt_password[40];
// NEW ITEMS TO ADD TOPIC PARAMETERS
//char client_name[40];
//char temperature_smoker_topic[40];
//char temperature_food_topic[40];
//char will_topic[40];

//flag for saving data
bool shouldSaveConfig = false;


// -------------------- Webserver Setup Code -------------------------

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .dht-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body>
  <h2>Smoker Temperatures</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Smoker Box Temperature</span> 
  </p>
  <p>
    <span id="box_temp">%BOXTEMP%</span>
    <sup class="units">&deg;F</sup>
  </p>
  <p>
    <i class="fas fa-thermometer-half" style="color:#00add6;"></i> 
    <span class="dht-labels">Food Temperature</span>
  </p>
  <p>
    <span id="food_temp">%FOODTEMP%</span>
    <sup class="units">&deg;F</sup>
  </p>
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("box_temp").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/box_temp", true);
  xhttp.send();
}, 10000 ) ;

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("food_temp").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/food_temp", true);
  xhttp.send();
}, 10000 ) ;
</script>
</html>)rawliteral";

// Replaces placeholder with probe values
String processor(const String& var){
  //Serial.println(var);
  if(var == "BOXTEMP"){
    return String(ts);
  }
  else if(var == "FOODTEMP"){
    return String(tf);
  }
  return String();
}

// ---------------------- Functions -----------------------------

//------ callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//---------- WIFI AP ------------

void config_ap() {
  // Put wifimanager stuff here....

  digitalWrite(ledPinBlue, LOW);   // sets the LED off
  
  Serial.println();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          //useMqtt = json["useMqtt"];
          //strcpy(mqtt_server, json["mqtt_server"]);
          //strcpy(mqtt_port, json["mqtt_port"]);
          //strcpy(mqtt_user, json["mqtt_user"]);
          //strcpy(mqtt_password, json["mqtt_password"]);
          // NEW PARAMETERS FOR MQTT TOPICS
          //strcpy(client_name, json["client_name"]);
          //strcpy(temperature_smoker_topic, json["temperature_smoker_topic"]);
          //strcpy(temperature_food_topic, json["temperature_food_topic"]);
          //strcpy(will_topic, json["will_topic"]);
          
          

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  //set up check box
  //char customhtml[24] = "type=\"checkbox\"";
  //  if (useMqtt) {
  //    strcat(customhtml, " checked");
  //  }

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  //WiFiManagerParameter custom_use_mqtt("use_mqtt", "use mqtt", "T", 2, customhtml, WFM_LABEL_AFTER);
  //WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  //WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  //WiFiManagerParameter custom_mqtt_user("mqtt_username", "mqtt user", mqtt_user, 40);
  //WiFiManagerParameter custom_mqtt_password("mqtt_password", "mqtt password", mqtt_password, 40);
  // NEW PARAMETERS FOR MQTT TOPICS
  //WiFiManagerParameter custom_client_name("client_name", "client_name", client_name, 40);  
  //WiFiManagerParameter custom_temperature_smoker_topic("temperature_smoker_topic", "temperature smoker topic", temperature_smoker_topic, 40);  
  //WiFiManagerParameter custom_temperature_food_topic("temperature_food_topic", "temperature food topic", temperature_food_topic, 40);  
  //WiFiManagerParameter custom_will_topic("will_topic", "will topic", will_topic, 40);
  
  
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  //wifiManager.addParameter(&custom_use_mqtt);
  //wifiManager.addParameter(&custom_mqtt_server);
  //wifiManager.addParameter(&custom_mqtt_port);
  //wifiManager.addParameter(&custom_mqtt_user);
  //wifiManager.addParameter(&custom_mqtt_password);
  // NEW PARAMETERS FOR MQTT TOPICS
  //wifiManager.addParameter(&custom_client_name);  
  //wifiManager.addParameter(&custom_temperature_smoker_topic);  
  //wifiManager.addParameter(&custom_temperature_food_topic);  
  //wifiManager.addParameter(&custom_will_topic);
  
  

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("SmokerConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  digitalWrite(ledPinBlue, HIGH);   // sets the LED on
  Serial.println("blue light on");

  //read updated parameters
  //useMqtt = (strncmp(custom_use_mqtt.getValue(), "T", 1) == 0);
  //useMqtt = custom_use_mqtt.getValue();
  //strcpy(mqtt_server, custom_mqtt_server.getValue());
  //strcpy(mqtt_port, custom_mqtt_port.getValue());
  //strcpy(mqtt_user, custom_mqtt_user.getValue());
  //strcpy(mqtt_password, custom_mqtt_password.getValue());
  // NEW PARAMETERS FOR MQTT TOPICS
  //strcpy(client_name, custom_client_name.getValue());  
  //strcpy(temperature_smoker_topic, custom_temperature_smoker_topic.getValue());  
  //strcpy(temperature_food_topic, custom_temperature_food_topic.getValue());  
  //strcpy(will_topic, custom_will_topic.getValue());
  
  
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    //json["use_mqtt"] = useMqtt;
    //json["mqtt_server"] = mqtt_server;
    //json["mqtt_port"] = mqtt_port;
    //json["mqtt_user"] = mqtt_user;
    //json["mqtt_password"] = mqtt_password;
    // NEW PARAMETERS FOR MQTT TOPICS
    //json["client_name"] = client_name;    
    //json["temperature_smoker_topic"] = temperature_smoker_topic;    
    //json["temperature_food_topic"] = temperature_food_topic;    
    //json["will_topic"] = will_topic;
      

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

}

//------------ SETUP CODE (RUN ONCE) -------------

void setup() {
  // put your setup code here, to run once:
  pinMode(ledPinBlue, OUTPUT);
  pinMode(ledPinRed, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT);
  
  Serial.begin(115200);

  config_ap();
 
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/box_temp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(ts).c_str());
  });
  server.on("/food_temp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(tf).c_str());
  });

  // Start server
  server.begin();
  Serial.println("Server started");

  //unsigned int mqtt_port_int = atoi (mqtt_port);

  //client.setServer(mqtt_server, mqtt_port_int);

}

/*
//---------- MQTT CONNECTION -------------------

void reconnect() {
   // Loop until we're reconnected
   while (!client.connected()) {
      Serial.print("Attempting MQTT connection…");

      // Generate client name based on MAC address and last 8 bits of microsecond counter
      //  String clientName;  
      //  clientName += "esp8266-";
      //  uint8_t mac[6];
      //  WiFi.macAddress(mac);
      //  clientName += macToStr(mac);
      //  clientName += "-";  
      //  clientName += String(micros() & 0xff, 16);
      Serial.print("Connecting to ");
      Serial.print(mqtt_server);
      Serial.print(" as ");
      Serial.println(client_name);

      // Attempt to connect
      // If you do not want to use a username and password, change next line to
      //if (client.connect((char*) clientName.c_str())) {
      //if (client.connect("homeassistant","homeassistant","05081985"))
      //if (client.connect("ESP8266Client", mqtt_user, mqtt_password))
      if (client.connect(client_name, mqtt_user, mqtt_password, will_topic, 1, 1, "Offline")){
         Serial.println("connected");
      } 
      else {
         Serial.print("failed, rc=");
         Serial.print(client.state());
         Serial.println(" try again in 5 seconds");
         // Wait 5 seconds before retrying
         delay(5000);
         // is configuration portal requested?
         if ( digitalRead(TRIGGER_PIN) == LOW ) {
           //digitalWrite(ledPinBlue, LOW);   // sets the LED off
           //reset settings - for testing
           WiFiManager wifiManager;
           wifiManager.resetSettings();
           config_ap();
        }
      }
   }

   client.publish(will_topic, "Online", true);
}
*/

//---------- MAIN PROGRAM LOOP -------------

void loop(){

  // is configuration portal requested?
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    //digitalWrite(ledPinBlue, LOW);   // sets the LED off
    //reset settings - for testing
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    config_ap();
  }
  
  // check status of wifi and reconnect if needed  
  if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(ledPinBlue, LOW);   // sets the LED off
      Serial.println("blue light off");
      Serial.println("no wifi connection...");
      delay(500);
      digitalWrite(ledPinBlue, HIGH);   // sets the LED on
      Serial.println("blue light on");
      delay(500);
   }

  delay(1000);

  // check status of mqtt and reconnect if needed
  //if (useMqtt){
  //  if (!client.connected()) {
  //      reconnect();
  //  }
  //}
  
 
  // run mqtt client
  //if (useMqtt){
  //  client.loop();
  //}
  
  // ----------------------- Begin Sensor Readings ------------------------------------

  // --------------------- Thermocouple Measurement Code ----------------------------

  // ---- Thermocouple linearization ----
  
  // Initialize variables.
  int i = 0; // Counter for arrays
  double internalTemp = tSmoker.readInternal(); // Read the internal temperature of the MAX31855.
  double rawTemp = tSmoker.readCelsius(); // Read the temperature of the thermocouple. This temp is compensated for cold junction temperature.
  double thermocoupleVoltage= 0;
  double internalVoltage = 0;
  double correctedTemp = 0;
   
  // Check to make sure thermocouple is working correctly.
  if (isnan(rawTemp)) {
     Serial.println("Something wrong with thermocouple!");
  }
  else {
    // Steps 1 & 2. Subtract cold junction temperature from the raw thermocouple temperature.
    thermocoupleVoltage = (rawTemp - internalTemp)*0.041276;  // C * mv/C = mV
 
    // Step 3. Calculate the cold junction equivalent thermocouple voltage.
 
    if (internalTemp >= 0) { // For positive temperatures use appropriate NIST coefficients
      // Coefficients and equations available from http://srdata.nist.gov/its90/download/type_k.tab
 
      double c[] = {-0.176004136860E-01,  0.389212049750E-01,  0.185587700320E-04, -0.994575928740E-07,  0.318409457190E-09, -0.560728448890E-12,  0.560750590590E-15, -0.320207200030E-18,  0.971511471520E-22, -0.121047212750E-25};
 
      // Count the the number of coefficients. There are 10 coefficients for positive temperatures (plus three exponential coefficients),
      // but there are 11 coefficients for negative temperatures.
      int cLength = sizeof(c) / sizeof(c[0]);
 
      // Exponential coefficients. Only used for positive temperatures.
      double a0 =  0.118597600000E+00;
      double a1 = -0.118343200000E-03;
      double a2 =  0.126968600000E+03;
  
      // From NIST: E = sum(i=0 to n) c_i t^i + a0 exp(a1 (t - a2)^2), where E is the thermocouple voltage in mV and t is the temperature in degrees C.
      // In this case, E is the cold junction equivalent thermocouple voltage.
      // Alternative form: C0 + C1*internalTemp + C2*internalTemp^2 + C3*internalTemp^3 + ... + C10*internaltemp^10 + A0*e^(A1*(internalTemp - A2)^2)
      // This loop sums up the c_i t^i components.
      for (i = 0; i < cLength; i++) {
        internalVoltage += c[i] * pow(internalTemp, i);
      }
        
      // This section adds the a0 exp(a1 (t - a2)^2) components.
      internalVoltage += a0 * exp(a1 * pow((internalTemp - a2), 2));
    }
    else if (internalTemp < 0) { // for negative temperatures
      double c[] = {0.000000000000E+00,  0.394501280250E-01,  0.236223735980E-04, -0.328589067840E-06, -0.499048287770E-08, -0.675090591730E-10, -0.574103274280E-12, -0.310888728940E-14, -0.104516093650E-16, -0.198892668780E-19, -0.163226974860E-22};
        
      // Count the number of coefficients.
      int cLength = sizeof(c) / sizeof(c[0]);
 
      // Below 0 degrees Celsius, the NIST formula is simpler and has no exponential components: E = sum(i=0 to n) c_i t^i
      for (i = 0; i < cLength; i++) {
        internalVoltage += c[i] * pow(internalTemp, i) ;
      }
    }
 
    // Step 4. Add the cold junction equivalent thermocouple voltage calculated in step 3 to the thermocouple voltage calculated in step 2.
    double totalVoltage = thermocoupleVoltage + internalVoltage;
 
    // Step 5. Use the result of step 4 and the NIST voltage-to-temperature (inverse) coefficients to calculate the cold junction compensated, linearized temperature value.
    // The equation is in the form correctedTemp = d_0 + d_1*E + d_2*E^2 + ... + d_n*E^n, where E is the totalVoltage in mV and correctedTemp is in degrees C.
    // NIST uses different coefficients for different temperature subranges: (-200 to 0C), (0 to 500C) and (500 to 1372C).
    if (totalVoltage < 0) { // Temperature is between -200 and 0C.
      double d[] = {0.0000000E+00, 2.5173462E+01, -1.1662878E+00, -1.0833638E+00, -8.9773540E-01, -3.7342377E-01, -8.6632643E-02, -1.0450598E-02, -5.1920577E-04, 0.0000000E+00};
      int dLength = sizeof(d) / sizeof(d[0]);
      for (i = 0; i < dLength; i++) {
        correctedTemp += d[i] * pow(totalVoltage, i);
      }
    }
    else if (totalVoltage < 20.644) { // Temperature is between 0C and 500C.
      double d[] = {0.000000E+00, 2.508355E+01, 7.860106E-02, -2.503131E-01, 8.315270E-02, -1.228034E-02, 9.804036E-04, -4.413030E-05, 1.057734E-06, -1.052755E-08};
      int dLength = sizeof(d) / sizeof(d[0]);
      for (i = 0; i < dLength; i++) {
        correctedTemp += d[i] * pow(totalVoltage, i);
      }
    }
    else if (totalVoltage < 54.886 ) { // Temperature is between 500C and 1372C.
      double d[] = {-1.318058E+02, 4.830222E+01, -1.646031E+00, 5.464731E-02, -9.650715E-04, 8.802193E-06, -3.110810E-08, 0.000000E+00, 0.000000E+00, 0.000000E+00};
      int dLength = sizeof(d) / sizeof(d[0]);
      for (i = 0; i < dLength; i++) {
        correctedTemp += d[i] * pow(totalVoltage, i);
      }
    } 
    else { // NIST only has data for K-type thermocouples from -200C to +1372C. If the temperature is not in that range, set temp to impossible value.
      // Error handling should be improved.
      Serial.print("Temperature is out of range. This should never happen.");
      correctedTemp = NAN;
    }
  }

  rawTempF = (1.8*rawTemp) + 32;
  correctedTempF = (1.8*correctedTemp) + 32;

  // ---- Thermocouple averaging ----

  //float TcoupleAve;
  //float TcoupleAverage  = 0;            // float CmAverage  = 0;            // Holds the average voltage measurement
  //float TcoupleScaledAverage = 0;       // float CsAverage = 0;             // Holds Scaled value of Tcouple reading after Offset applied
  //float TcoupleTotal = 0;               // float CmTotal = 0;               // Total of all Tcouple readings in averaging code
  //int   TcoupleSamples[SAMPLE_NUMBER];  // int   CmSamples[SAMPLE_NUMBER];  // Array to hold each  measurement

  TcoupleTotal = 0;  //CmTotal = 0;
  
  for (int i = 0; i < SAMPLE_NUMBER; i++) {
    TcoupleSamples[i] = correctedTempF;  // read Corrected Thermocouple reading and store  //CmSamples[i] = analogRead(ThermistorPin);  // read from pin and store
    delay(100);        // wait 100 milliseconds
  }

  /* Then, we will simply average all of those samples up for a "stiffer" measurement. */
  for (int i = 0; i < SAMPLE_NUMBER; i++) {
    TcoupleTotal += TcoupleSamples[i];      // add all samples up . . . //CmTotal += CmSamples[i];      // add all samples up . . .
  }
    
  TcoupleAverage = TcoupleTotal/SAMPLE_NUMBER;        // . . . average it w/ divide// CmAverage = CmTotal/SAMPLE_NUMBER;        // . . . average it w/ divide

  TcoupleScaledAverage = TcoupleAverage * TcOffset;   // CsAverage = CmAverage * Offset;
                
  //-------------------

  // ---------------------- Thermister Measurement Code -------------------------

  // Measured and Calculated Values
  // float Rmave, logRmave, Tcave, Tfave;
  // float CmAverage  = 0;            // Holds the average voltage measurement
  // float CsAverage = 0;             // Holds Scaled value of Cm reading after Offset applied
  // float CmTotal = 0;               // Total of all Cm readings in averaging code
  // int   CmSamples[SAMPLE_NUMBER];  // Array to hold each voltage measurement

  CmTotal = 0;
  
  for (int i = 0; i < SAMPLE_NUMBER; i++) {
    CmSamples[i] = analogRead(ThermistorPin);  // read from pin and store
    delay(100);        // wait 100 milliseconds
  }

  /* Then, we will simply average all of those samples up for a "stiffer" measurement. */
  for (int i = 0; i < SAMPLE_NUMBER; i++) {
    CmTotal += CmSamples[i];      // add all samples up . . .
  }
    
  CmAverage = CmTotal/SAMPLE_NUMBER;        // . . . average it w/ divide

  CsAverage = CmAverage * Offset;
  /* Here we calculate the thermistor’s resistance */
  Rmave = Rb * ( (Cmax / CsAverage) - 1);
    
  // if using Steinhart-Hart values
  logRmave = log(Rmave);
  Tcave = (1.0/(C1 + C2*logRmave + C3*pow(logRmave, 3)))-273.15;
    
  // if using Beta value
  //Tcave = (Beta * To)/(Beta + (To * log(Rmave/Ro)))-273.15;
    
  Tfave = (1.8*Tcave) + 32;

  if (Tfave > HighCutoff) {
    TfLinear = Tfave - (Mhigh * Tfave - Bhigh);
  }
  else {
    TfLinear = Tfave - (Mlow * Tfave - Blow);
  }
    
  // ------- Check to see if it's been longer than the interval time and update sensor readings to external devices (webserver and mqtt if used)  --
  // ------- This code is only run every 10 seconds  -----------------------------------------------------------------------------------------------
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {

    // save the last time you updated the sensor values
    previousMillis = currentMillis;

    ts = TcoupleScaledAverage;
    //ts = correctedTempF;

    //tf = Tfave;
    tf = TfLinear;
    
    // flash red LED to indicate activity
    digitalWrite(ledPinRed, HIGH);   // sets the LED on
    delay(1000);
    digitalWrite(ledPinRed, LOW);   // sets the LED off

    Serial.println("---------------------------------------------");
    Serial.println("");

    Serial.print("Raw Smoker Temp in C = ");
    Serial.print(rawTemp, 5);
    Serial.println();

    Serial.print("Corrected Smoker Temp in C = ");
    Serial.print(correctedTemp, 5);
    Serial.println("");

    Serial.print("Raw Smoker Temp in F = ");
    Serial.println(rawTempF, 5);
    Serial.println();

    Serial.print("Corrected Smoker Temp in F = ");
    Serial.println(correctedTempF, 5);
    Serial.println("");

    Serial.print("Smoker Temp (as published) = ");
    Serial.print(ts);
    Serial.print(" F");
    Serial.println();
    Serial.println();

    Serial.print("Ave Thermistor Resistance = ");
    Serial.print(Rmave);
    Serial.print(" Ohms");
    Serial.println();
    
    Serial.print("Food Temp (as published) = ");
    Serial.print(tf);
    Serial.print(" F");
    Serial.println("");

    Serial.println("");
    
    Serial.println("Network Configuration: ");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Host Name: ");
    Serial.println(WiFi.hostname());
    //Serial.print("useMqtt = ");
    //Serial.println(useMqtt);
    //if (useMqtt){
    //  Serial.print("Mqtt Status = ");
    //  Serial.println(client.state());
    //}
    //Serial.println("");
    

    // publish topics to mqtt broker
    //if (useMqtt){
    //  client.publish(temperature_smoker_topic, String(ts).c_str(), false);
    //  client.publish(temperature_food_topic, String(tf).c_str(), false);
    //}
  }
}
