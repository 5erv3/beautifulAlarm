#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <WiFi.h>
#include "esp_system.h"
#include <ezTime.h>
#include "DFRobotDFPlayerMini.h"
#include "config.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN 15
#define BUTTON_PIN 4

#define UPDT_INTERVAL (1000)
#define LOGGING  1

#define NB_ALARMS 2

#define TXT_BUF_SIZE (100 + (25 * NB_ALARMS))

#define LONGPRESS_TIME_MS  (1 * 1000)

#define SNOOZE_TIME_MS (4 * 60 * 1000)
#define MAX_SNOOZES 8

#define UPSIDE_DOWN 1

#if UPSIDE_DOWN
#define PA_SCROLL_DIR PA_SCROLL_RIGHT
#else
#define PA_SCROLL_DIR PA_SCROLL_LEFT
#endif

char buf[150];
int reconnect_counter = 0;
Timezone timezone;

MD_Parola myDisplay = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

HardwareSerial SerialDF(2);
//SoftwareSerial SerialDF(16, 17);
DFRobotDFPlayerMini myDFPlayer;

char txtbuf[TXT_BUF_SIZE];

WiFiServer server(80);

String header;

String displayState = "on";
bool display_on = true;

unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;

typedef enum alarmRepeat_e {
  ONCE = 0,
  WEEKEND = 1,
  WEEKDAY = 2,
  MAXNB
} alarmRepeat;

String alarmRepeatStr [MAXNB] = {"","Sa-So","Mo-Fr"};

typedef struct alarmElement_e {
  uint8_t hh;
  uint8_t mm;
  alarmRepeat repeat;
  String str;
  bool enabled = false;
} alarmElement;

alarmElement alarm_arr[NB_ALARMS];

void LOG(const char* logstring, bool newline = true) {
#if LOGGING
  if (newline) {
    Serial.println(logstring);
  } else {
    Serial.print(logstring);
  }
#endif
}

void setup_wifi() {
  delay(500);

  for (int i=0; i<NB_WIFI_SSID; i++){
    char* wifi_ssid = wifis[i];
    char* wifi_password = passwords[i];
  
    sprintf(buf, "Connecting to %s...", wifi_ssid);
    LOG(buf);
  
    WiFi.begin(wifi_ssid, wifi_password);
  
    reconnect_counter = 0;
  
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      reconnect_counter ++;
      if (reconnect_counter >= WIFI_MAX_CONNECT_TIME_SEC) {
        LOG("ERROR: WiFi connection counter expired");
        if (i == NB_WIFI_SSID-1){
          esp_restart();
        } else {
          break;
        }
      }
    }
  }

  
  sprintf(buf, "WiFi connected, IP address: ");
  LOG(buf, false);
  Serial.print(WiFi.localIP());
  LOG("");
  waitForSync();
}

void init_dfplayer(void) {
  Serial.println();
  Serial.println(F("DFRobot DFPlayer Mini Demo"));
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  if (!myDFPlayer.begin(SerialDF)) {  //Use softwareSerial to communicate with mp3.

    Serial.println(myDFPlayer.readType(), HEX);
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while (true);
  }
  Serial.println(F("DFPlayer Mini online."));
}

void setup() {
  SerialDF.begin(9600, SERIAL_8N1, 16, 17);
  Serial.begin(115200);
  Serial.println("Start");

  pinMode(BUTTON_PIN, INPUT);
  
  myDisplay.begin();
  myDisplay.setIntensity(15);
  myDisplay.displayClear();
  myDisplay.setTextAlignment(PA_CENTER);
  if (UPSIDE_DOWN) {
    myDisplay.setZoneEffect(0, true, PA_FLIP_UD);
    myDisplay.setZoneEffect(0, true, PA_FLIP_LR);
  }
  myDisplay.print("hello");

  setup_wifi();

  timezone.setLocation();

  init_dfplayer();
  server.begin();

  WiFi.localIP().toString().toCharArray(txtbuf, TXT_BUF_SIZE);
  myDisplay.displayText(txtbuf, PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);
}

void webpage_display(WiFiClient client, bool last_response) {
  // Display the HTML web page
  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<link rel=\"icon\" href=\"data:,\">");
  // CSS to style the on/off buttons
  // Feel free to change the background-color and font-size attributes to fit your preferences
  client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
  client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
  client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
  client.println(".button2 {background-color: #555555;}</style></head>");

  // Web Page Heading
  client.println("<body><h1>Annas BeautifulAlarm</h1>");

  // Display current state, and ON/OFF buttons for GPIO 26
  client.println("<p>Display " + displayState + "</p>");
  // If the output26State is off, it displays the ON button
  if (displayState == "off") {
    client.println("<p><a href=\"/display/on\"><button class=\"button\">ON</button></a></p>");
  } else {
    client.println("<p> <a href=\"/display/off\"><button class=\"button button2\">OFF</button></a></p>");
  }

  if (!last_response) {
    client.println("<h1>Wrong input format: please use HH:MM</h1>");
  }

  for (int i = 0; i < NB_ALARMS; i++) {
    client.println("<form>");
    
    client.println(" <h1>Alarm Time " + String(i+1) + "</h1>");
    client.println("<p>Currently set to: " + alarm_arr[i].str + " " + alarmRepeatStr[alarm_arr[i].repeat] + "</p>");
    client.println("<form action='/get'> New Alarm: <input type='text' name='date" + String(i) + "' id='date" + String(i) + "' size=2 autofocus> hh:mm");
    client.println("<br><br>");
    client.println("<input type='radio' id='mc' name='repeat' value='weekday'>");
    client.println("<label for='mc'> Mo - Fr</label> ");
    client.println("<input type='radio' id='vi' name='repeat' value='weekend'>");
    client.println("<label for='vi'> Sa - So</label>");
    client.println("<input type='radio' id='ae' name='repeat' value='on_once'>");
    client.println("<label for='ae'> once</label> ");
    client.println("<br><br>");
    client.println("<input type='submit' value='Submit'>");
    client.println("</form><br>");  
  }

  client.println("</body></html>");

  // The HTTP response ends with another blank line
  client.println();
}

bool webserver_handle_response(void) {
  if (header.indexOf("GET /display/on") >= 0) {
    Serial.println("Display on");
    displayState = "on";
    display_on = true;
  } else if (header.indexOf("GET /display/off") >= 0) {
    Serial.println("Display off");
    displayState = "off";
    display_on = false;
  } else if (header.indexOf("GET /?date") >= 0) {
    int i = header.substring(10, 11).toInt();
    if (i<NB_ALARMS && i>=0){
      String colon = header.substring(14, 17);
      if (colon.indexOf("%3A") >= 0) {
        int hh_int = header.substring(12, 14).toInt();
        int mm_int = header.substring(17, 19).toInt();
        alarm_arr[i].hh = hh_int;
        alarm_arr[i].mm = mm_int;
        if (hh_int <= 23 && hh_int >= 0 && mm_int <= 59 && mm_int >= 0) {
          // alarm set OK
          alarm_arr[i].enabled = true;         
          
          alarm_arr[i].str = "";
          if (hh_int < 10){
            alarm_arr[i].str = "0";
          }
          alarm_arr[i].str += String(hh_int) + ":";
          if (mm_int < 10){
            alarm_arr[i].str += "0";
          } 
          alarm_arr[i].str += String(mm_int);          
                  
        }
        int index = header.indexOf("&repeat=");
        if ( index >= 0){
          String s = header.substring(index+8, index+8+7);
          Serial.println(s);
          if (s == "weekday"){
            alarm_arr[i].repeat = WEEKDAY;
          } else if ( s == "weekend" ){
            alarm_arr[i].repeat = WEEKEND;
          } else {
            alarm_arr[i].repeat = ONCE;
          }
        } else {
          alarm_arr[i].repeat = ONCE;
        }
        
        myDisplay.displayClear();
        sprintf(txtbuf, "Alarm %i: %02d:%02d %s", i+1, hh_int, mm_int, alarmRepeatStr[alarm_arr[i].repeat]);
        Serial.println(txtbuf);
        myDisplay.displayText(txtbuf, PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);  
        
        return true;
      }
      
    }
    return false;
  }
  return true;
}

void webserver_loop() {
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            int last_response = webserver_handle_response();

            if (last_response) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();
            } else {
              client.println("HTTP/1.1 400 Bad Request");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();
            }

            webpage_display(client, last_response);
            break;

          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}

static bool alarm_playing = false;
static unsigned long alarm_start = 0;

void play_alarm_music(){
  myDisplay.displayText("ALARM", PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);
  myDFPlayer.volume(1);
  myDFPlayer.randomAll();
  alarm_playing = true;
  alarm_start = millis();
  Serial.println("Start");
}

void control_display_state(){
  if ( display_on ){
    myDisplay.displayText("Display off", PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);
    display_on = false;
    displayState = "off";
  } else {
    display_on = true;
    displayState = "on";
  }
}

static bool snooze_ongoing = false;
static unsigned long snooze_start = 0;
static int snoozecounter = 0;

void snooze(){
  if (snoozecounter >= MAX_SNOOZES){    
    stop_alarm();
    snoozecounter = 0;
  } else {
    Serial.println("SNOOZ");
    myDisplay.displayText("SNOOZ", PA_CENTER, 80, 1200, PA_SCROLL_DOWN, PA_SCROLL_DOWN);
    snooze_ongoing = true;
    myDFPlayer.pause();
    alarm_playing = false;
    snooze_start = millis();
    snoozecounter++;
  }
}

static bool alarm_stop_ongoing = false;
static bool after_alarm = false;

void stop_alarm(){
  alarm_stop_ongoing = false;
  alarm_playing = false;
  snooze_ongoing = false;
  myDisplay.displayText("ALARM OFF", PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);
  Serial.println("Stop alarm");
  myDFPlayer.pause(); 
  after_alarm = true;
}

void evaluate_button_press(unsigned long presstime){
  static unsigned long last_shortpress = 0; 

  if (presstime >= LONGPRESS_TIME_MS){
    return;
  } else if (presstime > 20 ){
    if (millis() - last_shortpress < 800){
      Serial.println("double shortpress");
      last_shortpress = 0;
    } else {
      Serial.println("shortpress");
      if (alarm_playing){
        snooze();
      } else {
        control_display_state();  
      }
      last_shortpress = millis();
    }    
  }
}

bool weekday_check(alarmRepeat repeat, int add_days){

  if (repeat == ONCE){
    return true;
  }
  
  uint8_t day = weekday() + add_days;

  if (day > SATURDAY){
    day = (day - SATURDAY);
  }

  switch(day){
    case SUNDAY:
    case SATURDAY:
      if (repeat == WEEKEND){
        return true;
      }
    break;
    case MONDAY:
    case TUESDAY:
    case WEDNESDAY:
    case THURSDAY:
    case FRIDAY:
      if (repeat == WEEKDAY){
        return true;
      }
    break;
  }

  return false;
}


static unsigned long counter = 0;
static bool button_pressed = false;
static unsigned long button_counter = 0;
static bool longpress_start = false;
static uint8_t alarm_stop_counter = 0;
static unsigned long alarm_stop_last_ms = 0;
static int active_alarm = 0;
static int en_alarms = 0;
static int last_alarm_min = 0;


void loop() {
  if (myDisplay.displayAnimate() && !alarm_stop_ongoing) {
    if (display_on) {
      int time_rounded = (int) (millis()/1000);
      if (snooze_ongoing && time_rounded % 2){
        if (active_alarm == 0){
          timezone.dateTime(" H:i[").toCharArray(txtbuf, TXT_BUF_SIZE);
        } else {
          timezone.dateTime(" H:i^").toCharArray(txtbuf, TXT_BUF_SIZE);
        }
      } else {
        if (en_alarms == 0){
          timezone.dateTime(" H:i ").toCharArray(txtbuf, TXT_BUF_SIZE);   
        } else if (en_alarms == 1){
          timezone.dateTime(" H:i[").toCharArray(txtbuf, TXT_BUF_SIZE);   
        } else if (en_alarms == 2) {
          timezone.dateTime(" H:i^").toCharArray(txtbuf, TXT_BUF_SIZE);   
        } else {
          timezone.dateTime(" H:i]").toCharArray(txtbuf, TXT_BUF_SIZE); 
        }
      }      
      myDisplay.displayText(txtbuf, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
    } else {
      myDisplay.displayClear();
    }
    webserver_loop();
    longpress_start = false;
  }
  en_alarms = 0;
  for (int i=0; i<NB_ALARMS; i++){
    if (alarm_arr[i].enabled && weekday_check(alarm_arr[i].repeat, 0)){
      if (timezone.hour()==alarm_arr[i].hh && timezone.minute()==alarm_arr[i].mm ){
        snoozecounter = 0;
        play_alarm_music();
        last_alarm_min = alarm_arr[i].mm;
        alarm_arr[i].enabled = false;
        active_alarm = i;
      }
      en_alarms += i+1;
    } else if (alarm_arr[i].enabled && weekday_check(alarm_arr[i].repeat, 1)){
      en_alarms += i+1;
    }    
  }
  if (counter + (5 * 1000) < millis()){
    // every 5 seconds
    counter = millis();

    if (alarm_playing) {
      if (alarm_start + (ALARM_MAX_PLAYING_TIME_SEC * 1000) > millis() ) {
        if (myDFPlayer.readVolume() <= 30){
          myDFPlayer.volumeUp();
        }
      } else {
        snooze();
      }
    }
      
  }
  if (digitalRead(BUTTON_PIN) && !button_pressed){
    button_counter = millis();
    button_pressed = true;
  } else if (!digitalRead(BUTTON_PIN) && button_pressed){
    button_pressed = false;
    evaluate_button_press(millis() - button_counter); 
    alarm_stop_ongoing = false;   
  } 
  if (digitalRead(BUTTON_PIN) && millis() - button_counter > LONGPRESS_TIME_MS && !longpress_start ){
    //longpress ongoing
    longpress_start = true;
    
    if (alarm_playing | snooze_ongoing) {
      alarm_stop_ongoing = true;
      alarm_stop_counter = 0;
      alarm_stop_last_ms = 0;
    } else if (myDisplay.displayAnimate()) {      
      char* ptr= txtbuf;
      snprintf(txtbuf, TXT_BUF_SIZE, "no alarm set");
  
      for (int i=0; i< NB_ALARMS; i++){
        if (alarm_arr[i].enabled){ 
          ptr += sprintf(ptr, "Alarm %d: %02d:%02d %s", i+1, alarm_arr[i].hh, alarm_arr[i].mm, alarmRepeatStr[alarm_arr[i].repeat]);
        }
      }
      myDisplay.displayText(txtbuf, PA_CENTER, 80, 0, PA_SCROLL_DIR, PA_SCROLL_DIR);
    }
  }

  static bool increment;

  if (digitalRead(BUTTON_PIN) && millis() - button_counter > LONGPRESS_TIME_MS && (alarm_stop_ongoing | snooze_ongoing) ){
    increment = false;
    
    if (alarm_stop_last_ms == 0){
      alarm_stop_last_ms = millis(); 
      increment = true;    
    } else {
      if (millis() - alarm_stop_last_ms > 500){
        alarm_stop_last_ms = millis();
        increment = true;
      }
    }

    if (increment){
      char * ptr = txtbuf;
      if (alarm_stop_counter < 5){
        alarm_stop_counter++;
      } else {
        alarm_stop_ongoing = false;
        button_counter = 0;
        stop_alarm();
      }
      snprintf(txtbuf, alarm_stop_counter, "OFF");

      myDisplay.displayText(txtbuf, PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
    }
  }

  if (snooze_ongoing && (millis() - snooze_start) > SNOOZE_TIME_MS && !alarm_playing){
    play_alarm_music();
  }

  if (after_alarm && last_alarm_min != minute()){
    after_alarm = false;
    for (int i=0; i< NB_ALARMS; i++){
      if (alarm_arr[i].repeat > 0){
        alarm_arr[i].enabled = true;
      }
    }
  }
  
}
