#define nextion Serial2 //this depends on the board used, for an esp32 I used serial2.... rtfm
#define WIFI_DEBUG
#define JSON_DEBUG
#define GCALURL "https://script.google.com/macros/s/AKfycbwQPp04g40WFmNsUZmKP9-jNZImpmBkoL8-jMOQzU_KLfTfYVU/exec"
#include <Nextion.h>
#include <ArduinoJson.h>   
#include <WiFi.h>
#include <Preferences.h>
#include <TimeLib.h> 
#include <time.h> 
#include "ClockNTP.h"
#include "Debug.h"
#include "GoogleCal.h"
#define JSONBUFF 512  
#define GCALSIZE 16  
Nextion myNextion(nextion, 9600); //create a Nextion object named myNextion using the nextion serial port @ 9600bps
Preferences preferences;
////////////////////////////Structs///////////////
struct Events {
   unsigned long event;
   const char* title;
   const char* info;
};
Events GCAL[GCALSIZE];



enum enumStatus
{
  stateWiFi,
  stateNTP,
  stateOK,
  stateAlarm,
};
enumStatus Status;

////////////////////////wifi and weather settings/////////////
const char* ssid = "NETGEAR26";
const char* password =  "windytulip282";
String CityID = "4887158"; //UIUC
String APIKEY = "6f0a9c3e1614f0a092326959c055683c";
int weatherID = 0;
WiFiClient client2;
char* servername ="api.openweathermap.org"; 

////////////////////Functions////////////////

void getWeatherData() //client function to send/receive GET request data.
{
  String result ="";
  WiFiClient client2;
  const int httpPort = 80;
  if (!client2.connect(servername, httpPort)) {
        return;
    }
      // We now create a URI for the request
    String url = "/data/2.5/forecast?id="+CityID+"&units=imperial&cnt=1&APPID="+APIKEY;

       // This will send the request to the server
    client2.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + servername + "\r\n" +
                 "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client2.available() == 0) {
        if (millis() - timeout > 5000) {
            client2.stop();
            return;
        }
    }

    // Read all the lines of the reply from server
    while(client2.available()) {
        result = client2.readStringUntil('\r');
    }

result.replace('[', ' ');
result.replace(']', ' ');

char jsonArray [result.length()+1];
result.toCharArray(jsonArray,sizeof(jsonArray));
jsonArray[result.length() + 1] = '\0';

StaticJsonBuffer<1024> json_buf;
JsonObject &root = json_buf.parseObject(jsonArray);
if (!root.success())
{
  Serial.println("parseObject() failed");
}
String location = root["city"]["name"];
String temperature = root["list"]["main"]["temp"];
String weather = root["list"]["weather"]["main"];
String description = root["list"]["weather"]["description"];
String idString = root["list"]["weather"]["id"];
String timeS = root["list"]["dt_txt"];

weatherID = idString.toInt();
Serial.print("\nWeatherID: ");
Serial.print(weatherID);
//We need that in order the nextion to recognise the first command after the serial print
printWeatherIcon(weatherID);
myNextion.setComponentText("temp", temperature+"F");
Serial.println(temperature);
}

void connectWiFi()
{
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // attempt to connect to Wifi network:
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    // wait 1 second for re-trying
    delay(1000);
  }

  Serial.print("Connected to ");
  Serial.println(ssid);

}

void handleGCAL(void)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    String response = FetchGCal(GCALURL);
    GCAL_PRINT("GCAL:"); 
    GCAL_PRINTLN(response);
    process(response);
  }
}


void process(String response)
{
  JSON_PRINT(F("JSON:")); JSON_PRINTLN(response);

  StaticJsonBuffer<JSONBUFF> jsonBuffer;
  JsonObject& JSON = jsonBuffer.parseObject(response, 2);
  if (JSON.success())
  {
    String status = JSON["status"];
    JSON_PRINT(F("JSON:")); JSON_PRINT(status); JSON_PRINT(F(":")); JSON_PRINTLN(getNow());
    if (status == "OK")
    {
      if (JSON["event"])
      {
        for (int i=0; (i<GCALSIZE) && JSON["event"][i]; i++)
        {
          GCAL[i].event = JSON["event"][i];
          GCAL[i].title = JSON["title"][i];
          GCAL[i].info = JSON["info"][i];
          JSON_PRINT(F("EVENT: ")); JSON_PRINT(GCAL[i].event); JSON_PRINT(F(" ")); JSON_PRINT(GCAL[i].title); JSON_PRINT(F(" ")); JSON_PRINTLN(GCAL[i].info);
        }
        //int i=0;
        //JSON_PRINT(F("EVENT: ")); JSON_PRINT(GCAL[i].event); JSON_PRINT(F(" ")); JSON_PRINT(GCAL[i].title); JSON_PRINT(F(" ")); JSON_PRINTLN(GCAL[i].info);
        set_display();
      }
    }
    else if (status == "EMPTY")
    {
      JSON_PRINTLN(F("JSON:EMPTY"));
    }
    else if (status == "NOK")
    {
      JSON_PRINT(F("JSON:")); JSON_PRINTLN((const char*)JSON["error"]);
    }
    else
    {
      JSON_PRINT(F("JSON:Unknown:")); JSON_PRINTLN(response);
    }
  }
  else
  {
    JSON_PRINT(F("JSON:Parse error:")); JSON_PRINTLN(response);
  }
}


void handleNTP()
{
  if (timeStatus() == timeNotSet)
  {
    NTP_PRINTLN("NTP:Lost.");
    Status = stateNTP;
    //getNtpTime();
  }
  else
    Status = stateOK;
}


////////////////////////////devices section//////////
int number_of_hdmi = 3; //set to the number of devices you want to switch through
int number_of_usb = 3;
unsigned int current_hdmi;
unsigned int current_usb;
const char* hdmi_pictures[] = {"hdmi.pic=1","hdmi.pic=2","hdmi.pic=3"}; //these are the picture set strings in nextion
const char* usb_pictures[] = {"usb.pic=1","usb.pic=3"};
unsigned long last_parse = 0;
#define hdmi_pin 13
#define usb_pin 12
int parse_interval = 30*60*1000; //means we parse every 30 minutes

void set_display(){
  for (int i=0; (i<4); i++)
  {
    myNextion.setComponentText("task"+String(i),String(GCAL[i].title));
  }
}
void setup()
{
  preferences.begin("my-app", false);
  current_hdmi = preferences.getUInt("current_hdmi", 0);
  current_usb = preferences.getUInt("current_usb", 0);
  pinMode(hdmi_pin,OUTPUT);
  pinMode(usb_pin,OUTPUT);
  digitalWrite(hdmi_pin,HIGH);
  digitalWrite(usb_pin,LOW);
  Serial.begin(115200);
  Serial2.begin(9600);
  Serial2.flush();
  myNextion.init(); // send the initialization commands for Page 0
  myNextion.sendCommand(hdmi_pictures[current_hdmi]);
  myNextion.sendCommand(usb_pictures[current_usb]);
  myNextion.sendCommand("thup=1"); //enables wake on touch
  myNextion.sendCommand("thsp=2700"); //sets the sleep timer in seconds
  connectWiFi();
  getWeatherData();
  SetupNTP();
  handleGCAL();
}


void loop() {
  String message = myNextion.listen(); //check for message
  if (message != "") {
    Serial.println(message);
  }
  //if the hdmi switch button is pressed this code will be sent, these are generated by the above code
  if (message == "65 0 5 0 ff ff ff") {
    current_hdmi = (current_hdmi+1)%number_of_hdmi;
    preferences.putUInt("current_hdmi", current_hdmi);
    digitalWrite(hdmi_pin,LOW);
    delay(50);
    digitalWrite(hdmi_pin,HIGH);
    myNextion.sendCommand(hdmi_pictures[current_hdmi]);
  }
  if (message == "65 0 6 0 ff ff ff") {
    current_usb = (current_usb+1)%number_of_usb;
    preferences.putUInt("current_usb", current_usb);
    digitalWrite(usb_pin,HIGH);
    delay(50);
    digitalWrite(usb_pin,LOW);
    myNextion.sendCommand(usb_pictures[current_usb]);
  }
  if (message == "65 0 2 0 ff ff ff") handleGCAL();
  if(millis()-last_parse >= parse_interval){
    last_parse = millis();
    getWeatherData();
    handleGCAL();
  }
  
}
void printWeatherIcon(int id)
{
 switch(id)
 {
  case 800: myNextion.sendCommand("weather.pic=14"); break;
  case 801: myNextion.sendCommand("weather.pic=10"); break;
  case 802: myNextion.sendCommand("weather.pic=10"); break;
  case 803: myNextion.sendCommand("weather.pic=5"); break;
  case 804: myNextion.sendCommand("weather.pic=5"); break;
  
  case 200: myNextion.sendCommand("weather.pic=15"); break;
  case 201: myNextion.sendCommand("weather.pic=15"); break;
  case 202: myNextion.sendCommand("weather.pic=15"); break;
  case 210: myNextion.sendCommand("weather.pic=15"); break;
  case 211: myNextion.sendCommand("weather.pic=15"); break;
  case 212: myNextion.sendCommand("weather.pic=15"); break;
  case 221: myNextion.sendCommand("weather.pic=15"); break;
  case 230: myNextion.sendCommand("weather.pic=15"); break;
  case 231: myNextion.sendCommand("weather.pic=15"); break;
  case 232: myNextion.sendCommand("weather.pic=15"); break;

  case 300: myNextion.sendCommand("weather.pic=11"); break;
  case 301: myNextion.sendCommand("weather.pic=11"); break;
  case 302: myNextion.sendCommand("weather.pic=11"); break;
  case 310: myNextion.sendCommand("weather.pic=11"); break;
  case 311: myNextion.sendCommand("weather.pic=11"); break;
  case 312: myNextion.sendCommand("weather.pic=11"); break;
  case 313: myNextion.sendCommand("weather.pic=11"); break;
  case 314: myNextion.sendCommand("weather.pic=11"); break;
  case 321: myNextion.sendCommand("weather.pic=11"); break;

  case 500: myNextion.sendCommand("weather.pic=13"); break;
  case 501: myNextion.sendCommand("weather.pic=13"); break;
  case 502: myNextion.sendCommand("weather.pic=13"); break;
  case 503: myNextion.sendCommand("weather.pic=13"); break;
  case 504: myNextion.sendCommand("weather.pic=13"); break;
  case 511: myNextion.sendCommand("weather.pic=11"); break;
  case 520: myNextion.sendCommand("weather.pic=9"); break;
  case 521: myNextion.sendCommand("weather.pic=9"); break;
  case 522: myNextion.sendCommand("weather.pic=7"); break;
  case 531: myNextion.sendCommand("weather.pic=7"); break;

  case 600: myNextion.sendCommand("weather.pic=8"); break;
  case 601: myNextion.sendCommand("weather.pic=12"); break;
  case 602: myNextion.sendCommand("weather.pic=12"); break;
  case 611: myNextion.sendCommand("weather.pic=8"); break;
  case 612: myNextion.sendCommand("weather.pic=8"); break;
  case 615: myNextion.sendCommand("weather.pic=8"); break;
  case 616: myNextion.sendCommand("weather.pic=8"); break;
  case 620: myNextion.sendCommand("weather.pic=8"); break;
  case 621: myNextion.sendCommand("weather.pic=12"); break;
  case 622: myNextion.sendCommand("weather.pic=12"); break;

  case 701: myNextion.sendCommand("weather.pic=6"); break;
  case 711: myNextion.sendCommand("weather.pic=6"); break;
  case 721: myNextion.sendCommand("weather.pic=6"); break;
  case 731: myNextion.sendCommand("weather.pic=6"); break;
  case 741: myNextion.sendCommand("weather.pic=6"); break;
  case 751: myNextion.sendCommand("weather.pic=6"); break;
  case 761: myNextion.sendCommand("weather.pic=6"); break;
  case 762: myNextion.sendCommand("weather.pic=6"); break;
  case 771: myNextion.sendCommand("weather.pic=6"); break;
  case 781: myNextion.sendCommand("weather.pic=6"); break;

  default:break; 
 }
}
