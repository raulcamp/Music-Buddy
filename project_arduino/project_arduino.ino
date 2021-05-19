#include <WiFi.h> //Connect to WiFi Network
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include<math.h>
#include<string.h>
#include "Button.h"
#include "HeartbeatSensor.h"
#include <DFRobot_MAX30102.h>

DFRobot_MAX30102 particleSensor;

TFT_eSPI tft = TFT_eSPI();
const int SCREEN_HEIGHT = 160;
const int SCREEN_WIDTH = 128;
// TODO: change pins depending on board config
const int BUTTON_PIN1 = 19; 
const int BUTTON_PIN2 = 5;
const int BUTTON_PIN3 = 3;

const int LOOP_PERIOD = 40;
const int MAX_GROUPS = 100;
int state;
int group_state;
int option_state;
int count;
int num_groups;
const int idle = 0;
const int song_menu = 1;
const int vis_menu = 2;
const int idle_menu = 3;
const int groups_menu = 4;
const int pulse_recommendation = 5;
const int recommended_song = 6;
int state_change;

#define IDLE 0
#define PRESSED 1

char network[] = "";
char password[] = "";
/* Having network issues since there are 50 MIT and MIT_GUEST networks?. Do the following:
    When the access points are printed out at the start, find a particularly strong one that you're targeting.
    Let's say it is an MIT one and it has the following entry:
   . 4: MIT, Ch:1 (-51dBm)  4:95:E6:AE:DB:41
   Do the following...set the variable channel below to be the channel shown (1 in this example)
   and then copy the MAC address into the byte array below like shown.  Note the values are rendered in hexadecimal
   That is specified by putting a leading 0x in front of the number. We need to specify six pairs of hex values so:
   a 4 turns into a 0x04 (put a leading 0 if only one printed)
   a 95 becomes a 0x95, etc...
   see starting values below that match the example above. Change for your use:
   Finally where you connect to the network, comment out
     WiFi.begin(network, password);
   and uncomment out:
     WiFi.begin(network, password, channel, bssid);
   This will allow you target a specific router rather than a random one!
*/
uint8_t channel = 1; //network channel on 2.4 GHz
byte bssid[] = {0x04, 0x95, 0xE6, 0xAE, 0xDB, 0x41}; //6 byte MAC address of AP you're targeting.

char host[] = "608dev-2.net";
char username[] = "test_user";
char song[] = "Song Name";
char artist[] = "Artist";

uint16_t rawReading;

//Some constants and some resources:
const int RESPONSE_TIMEOUT = 6000; //ms to wait for response from host
const uint16_t IN_BUFFER_SIZE = 1000;
const uint16_t OUT_BUFFER_SIZE = 1000; //size of buffer to hold HTTP response
char request[IN_BUFFER_SIZE];
char response[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP request

char recSongBuffer[OUT_BUFFER_SIZE];

char groups[MAX_GROUPS][80];
char invite[1000];
char shared[1000];
char liked[1000];

unsigned long timer;

uint8_t old_reading; //for remember button value and detecting edges

Button left(BUTTON_PIN1);
Button middle(BUTTON_PIN2);
Button right(BUTTON_PIN3);

HeartbeatSensor hbs;
int bpm;
bool usingPulse;

unsigned long primary_timer;

void setup() {

  int tryCounter = 0;
  usingPulse = true;
  while (!particleSensor.begin() && tryCounter < 5) {
    Serial.println("MAX30102 was not found");
    tryCounter++;
    delay(1000);
  }

  if (tryCounter >= 5) usingPulse = false;

  
  Serial.begin(115200); //for debugging if needed.
  delay(100);

  setupWifi();

  tft.init();
  tft.setRotation(2);
  tft.setTextSize(1.75);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK); //set color of font to red foreground, black background
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  timer = millis();
  state = idle;
  option_state = 0;
  group_state = 0;
  invite[0] = '\0';
  shared[0] = '\0';
  liked[0] = '\0';
  state_change = 0; // false

  particleSensor.sensorConfiguration(/*ledBrightness=*/60, /*sampleAverage=*/SAMPLEAVG_8, \
                                  /*ledMode=*/MODE_MULTILED, /*sampleRate=*/SAMPLERATE_400, \
                                  /*pulseWidth=*/PULSEWIDTH_411, /*adcRange=*/ADCRANGE_16384);

  hbs = HeartbeatSensor();

  primary_timer = millis();
  state_change = true;
}


void loop() {
  // button input
  int leftReading = left.update();
  int middleReading = middle.update();
  int rightReading = right.update();

  // pulse reading
  if (usingPulse) {
    bpm = hbs.update(particleSensor.getIR());
  }

  // audio
  rawReading = analogRead(A0);

  visualizeMusic(rawReading);

  handleDisplay(leftReading, middleReading, rightReading);
}

void handleDisplay(int leftReading, int middleReading, int rightReading) {
  fetchNotifications();
  
  tft.setCursor(0,0,1); // reset cursor at the top of the screen

  if (state_change) {
    tft.fillScreen(TFT_BLACK);
    state_change = false;
  }

  switch(state){

    case idle: //Menu State
        idleState(leftReading, middleReading, rightReading);
        break;
    case song_menu: //Here the user has option to fetch vis or display song name
        songMenuState(leftReading, middleReading, rightReading);
        break;
    case vis_menu: //Vis menu
        visMenuState(leftReading, middleReading, rightReading);
        break;  
    case groups_menu: // Groups menu
        groupsMenuState(leftReading, middleReading, rightReading);
        break;
    case pulse_recommendation:
        pulseRecommendationState(leftReading, middleReading, rightReading);
        break;
    case recommended_song:
        recommendedSongState(leftReading, middleReading, rightReading);
        break;
  }
}

void fetchNotifications() {
  if (millis() - timer > 5000) {
    request[0] = '\0'; //set 0th byte to null
    sprintf(request, "GET http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py?action=get_invites&username=%s\r\n",username);
    strcat(request, "Host: 608dev-2.net\r\n"); //add more to the end
    strcat(request, "\r\n"); //add blank line!
    do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
    Serial.println(response);
    if (response[0] != 'Z') {
      strcat(invite, response);
      state_change = 1;
    } else {
      request[0] = '\0'; //set 0th byte to null
      sprintf(request, "GET http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py?action=get_shared&username=%s\r\n",username);
      strcat(request, "Host: 608dev-2.net\r\n"); //add more to the end
      strcat(request, "\r\n"); //add blank line!
      do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
      if (response[0] != 'Z') {
        strcat(shared, response);
        state_change = 1;
      } else {
        request[0] = '\0'; //set 0th byte to null
        sprintf(request, "GET http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py?action=get_liked&username=%s\r\n",username);
        strcat(request, "Host: 608dev-2.net\r\n"); //add more to the end
        strcat(request, "\r\n"); //add blank line!
        do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
        if (response[0] != 'Z') {
          strcat(liked, response);
          state_change = 1;
        }
      } 
    }
    
    timer = millis();
  }
}

//////////////////////
/////// STATES ///////
//////////////////////

// IDLE

void idleState(int leftReading, int middleReading, int rightReading) {
  if (millis() - primary_timer > 200) {
    tft.fillScreen(TFT_BLACK);
    primary_timer = millis();

      //displays options...fetch current song
    tft.println("State: Idle. \n\nPress right button to display song info.");

  }

  state_change = false;

  if(leftReading){ //button 1 is pressed --> displays song on lcd 
  // send request and retrieve song
      state = song_menu;
      state_change = true;

      tft.println("Requesting Song...");
      // TODO actually request the song...
      delay(2000);
      tft.fillScreen(TFT_BLACK);
  } else if (middleReading) {
    state = pulse_recommendation;
    state_change = true;
  } else if(rightReading){ //button 3 is pressed --> Song List
      tft.fillScreen(TFT_BLACK);

      tft.println("Displaying song list...");
      delay(2000);
      tft.fillScreen(TFT_BLACK);

  }
}

// SONG MENU

void songMenuState(int leftReading, int middleReading, int rightReading) {


  // Song Title/Artist display
  char output[80];
  output[0] = '\0';
  sprintf(output, "%s by %s.", song, artist);
  tft.println(output);

  // handle state changes
  if (state_change == 1) {
    tft.fillRect(0, 15, 127, 114, TFT_BLACK);
    state_change = 0;
  }

  displayBPM();
  
  if (invite[0] != '\0') {
    handleInvite(rightReading);
  } else if (shared[0] != '\0') {
    handleShared(rightReading); // TODO RAUL or TODO JAMES - should their be some kind of interaction with this notifcation? like a button press or something? at least to get out of it
  } else if (liked[0] != '\0') {
    handleLiked(rightReading); // TODO RAUL or TODO JAMES - should their be some kind of interaction with this notifcation? like a button press or something? at least to get out of it
    
  } else { // Song Menu
    
    tft.setCursor(1, 15);
    tft.println("\n\nPress left -> \n       Sync Viz");
    tft.setCursor(1, 48);
    tft.println("\n\nPress middle -> \n       Like Song");
    tft.setCursor(1, 81);
    tft.println("\n\nPress right -> \n       Share Song");
    tft.setCursor(1, 114, 1);
//    tft.println("\n\nStep Count: INSERT STEP COUNT MODULE HERE");
    tft.println("\n\nLong Press left -> \n   BPM-based Song Rec");
    tft.setCursor(1, 147, 1);
    
    if(leftReading == 1){ // sync visualization
      state = vis_menu;
      tft.fillScreen(TFT_BLACK);
      tft.println("Initializing \nVisualization...");
      delay(2000);
      tft.fillScreen(TFT_BLACK);
      count = 0;

    }
    else if(leftReading == 2) {
      state = pulse_recommendation;
      tft.fillScreen(TFT_BLACK);
      count = 0;
    }
    else if(middleReading){ // Like song
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0,0,1); // reset cursor at the top of the screen
      tft.println("Liking Song...");
      count = 0;
      delay(2000);
      tft.fillScreen(TFT_BLACK);
      initOptions();
      state = groups_menu;
    }
    else if(rightReading){ // Share song with current session
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0,0,1); // reset cursor at the top of the screen
      tft.println("Sharing Song...");
      count = 1;
      delay(2000);
      tft.fillScreen(TFT_BLACK);
      initOptions();
      state = groups_menu;
    }
  }
}

void handleInvite(int rightReading) {
  char output[80];
  sprintf(output, "\n\nNew invite: ->You have been invited to %s.", response);
  tft.setCursor(1, 15, 2);
  tft.println(output);
  tft.setCursor(1, 114, 1);

  if (rightReading) {
    char body[100];
    sprintf(body,"action=invited_join&username=%s&group_name=%s", username, invite);
    int body_len = strlen(body);
    sprintf(request,"POST http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py HTTP/1.1\r\n");
    strcat(request,"Host: 608dev-2.net\r\n");
    strcat(request,"Content-Type: application/x-www-form-urlencoded\r\n");
    sprintf(request+strlen(request),"Content-Length: %d\r\n", body_len); //append string formatted to end of request buffer
    strcat(request,"\r\n"); //new line from header to body
    strcat(request,body); //body
    strcat(request,"\r\n"); //new line
    do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
    invite[0] = '\0';
    state_change = 1;
  }
}

void handleShared(int rightReading) {
  char output[80];
  sprintf(output, "\n\nNew shared song: ->The song %s has been shared with you.", response);
  
  tft.setCursor(1, 15, 2);
  tft.println(output);
  tft.setCursor(1, 114, 1);
  
  if (rightReading || response[0] == 'Z') {
    shared[0] = '\0';
    state_change = 1;
  }
}

void handleLiked(int rightReading) {
  char output[80];  
  sprintf(output, "\n\nThe song %s is popular in your group! Do you want to listen to it?", response);
  
  tft.setCursor(1, 15, 2);
  tft.println(output);
  tft.setCursor(1, 114, 1);
  
  if (rightReading || response[0] == 'Z') {
    liked[0] = '\0';
    state_change = 1;
  }
}

// VISUALIZATION MENU

void visMenuState(int leftReading, int middleReading, int rightReading) {
  tft.println("State: Visualization Screen\n\nSong: Peaches\n\nLight strobes based \non current beat/pitch");
  //create object of dan's class and call run() to start visualization


  if(leftReading){ //next song
    tft.fillScreen(TFT_BLACK);
    tft.println("Fetching next song...");
    delay(2000); //delay 3 seconds
    tft.fillScreen(TFT_BLACK);

  }
  else if(rightReading){ // go back to idle
    state = idle;
    tft.fillScreen(TFT_BLACK);
    tft.println("Going back to idle state...");
    delay(2000);
    tft.fillScreen(TFT_BLACK);

  }
}

// GROUPS MENU

void groupsMenuState(int leftReading, int middleReading, int rightReading) {
  if (leftReading) { // scroll next group
    option_state = (option_state + 1) % 5;
    nextOption(option_state);
  
  }
  else if (middleReading) { // confirm group
    if (count == 0) {
        char body[100];
        sprintf(body,"action=like&username=%s&group_name=%s&song=%s", username, groups[option_state], song);
        int body_len = strlen(body);
        sprintf(request,"POST http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py HTTP/1.1\r\n");
        strcat(request,"Host: 608dev-2.net\r\n");
        strcat(request,"Content-Type: application/x-www-form-urlencoded\r\n");
        sprintf(request+strlen(request),"Content-Length: %d\r\n", body_len); //append string formatted to end of request buffer
        strcat(request,"\r\n"); //new line from header to body
        strcat(request,body); //body
        strcat(request,"\r\n"); //new line
        do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
    } else {
        char body[100];
        sprintf(body,"action=share&username=%s&group_name=%s&song=%s", username, groups[option_state], song);
        int body_len = strlen(body);
        sprintf(request,"POST http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py HTTP/1.1\r\n");
        strcat(request,"Host: 608dev-2.net\r\n");
        strcat(request,"Content-Type: application/x-www-form-urlencoded\r\n");
        sprintf(request+strlen(request),"Content-Length: %d\r\n", body_len); //append string formatted to end of request buffer
        strcat(request,"\r\n"); //new line from header to body
        strcat(request,body); //body
        strcat(request,"\r\n"); //new line
        do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
    }
  } else if (rightReading) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0,0,1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    char out[100];
    if (count == 0) {
        sprintf(out, "Sent liked songs to groups");
    } else {
        sprintf(out, "Sent shared songs to groups");
    }
    tft.println(out);
    delay(2000);
    tft.fillScreen(TFT_BLACK);
    state = song_menu;
    option_state = 0;
  }
}

// HEARTRATE SONG RECOMMENDATION

void pulseRecommendationState(int leftReading, int middleReading, int rightReading) {
  state_change = false;
  tft.println("Fetching song!");
  getSongByBPM(bpm);
  state = recommended_song;
  state_change = true;
}

void displayBPM() {
  if (bpm > 45) {
    tft.printf("\nHeart BPM: %i\n", bpm);
  } else {
    tft.printf("\nSearching for Heartbeat...\n", bpm);
  }
}

// RECOMMENDED SONG

void recommendedSongState(int leftReading, int middleReading, int rightReading) {
  state_change = false;
  tft.println(recSongBuffer);
  tft.println("\n Press center button to go back to home.");

  if (middleReading) {
    state = idle;
    state_change = true;
  }
}

void getSongByBPM(int bpm) {
  request[0] = '\0';
  recSongBuffer[0] = '\0';
  // http://608dev-2.net/sandbox/sc/team65/michael/bpm.py?bpm=65
  sprintf(request, "GET http://608dev-2.net/sandbox/sc/team65/michael/bpm.py?bpm=%i HTTP/1.1\r\n",bpm);
  strcat(request, "Host: 608dev-2.net\r\n"); //add more to the end
  strcat(request, "\r\n"); //add blank line!
  do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
  strcpy(recSongBuffer, response);
}

// VISUALIZATION

void visualizeMusic(uint16_t raw_reading) {
  Serial.println("DUMMY FUNCTION");
  // TODO DAN
}

void syncMusic() {
  // TODO DAN - this is just a helper function to sync with the server like u had in your demo
}

void initOptions() {
  request[0] = '\0'; //set 0th byte to null
  sprintf(request, "GET http://608dev-2.net/sandbox/sc/team65/raul/final_project_server_code.py?action=get_groups&username=%s\r\n",username);
  strcat(request, "Host: 608dev-2.net\r\n"); //add more to the end
  strcat(request, "\r\n"); //add blank line!
  do_http_request("608dev-2.net", request, response, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, false);
  StaticJsonDocument<200> group_doc;
  deserializeJson(group_doc, response);
  for(int i = 0; i < 5; i++) {
    groups[i][0] = '\0';
    strcat(groups[i], group_doc["groups"][i]);
  }

  tft.setCursor(5, 0, 2);
  tft.fillRect(0, 0, 100, 15, TFT_RED);
  tft.setTextColor(TFT_BLACK, TFT_BLACK);
  tft.println(groups[0]);
    
  for(int i = 1; i < 5; i++) {
    tft.setCursor(5, 15*i, 2);
    tft.fillRect(0, 15*i, 100, 15, TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println(groups[i]);
  }
}

void nextOption(int option) {
  switch (option) {
    case 0:
      tft.setCursor(5, 60, 2);
      tft.fillRect(0, 60, 100, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println(groups[4]);

      tft.setCursor(5, 0, 2);
      tft.fillRect(0, 0, 100, 15, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.println(groups[0]);
      break;
    case 1:
      tft.setCursor(5, 0, 2);
      tft.fillRect(0, 0, 100, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println(groups[0]);

      tft.setCursor(5, 15, 2);
      tft.fillRect(0, 15, 100, 15, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.println(groups[1]);
      break;
    case 2:
      tft.setCursor(5, 15, 2);
      tft.fillRect(0, 15, 100, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println(groups[1]);

      tft.setCursor(5, 30, 2);
      tft.fillRect(0, 30, 100, 15, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.println(groups[2]);
      break;
    case 3:
      tft.setCursor(5, 30, 2);
      tft.fillRect(0, 30, 100, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println(groups[2]);

      tft.setCursor(5, 45, 2);
      tft.fillRect(0, 45, 100, 15, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.println(groups[3]);
      break;
    case 4:
      tft.setCursor(5, 45, 2);
      tft.fillRect(0, 45, 100, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println(groups[3]);

      tft.setCursor(5, 60, 2);
      tft.fillRect(0, 60, 100, 15, TFT_RED);
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.println(groups[4]);
      break;
  }
}

// NETWORK STUFF

void setupWifi() {
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      Serial.printf("%d: %s, Ch:%d (%ddBm) %s ", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "");
      uint8_t* cc = WiFi.BSSID(i);
      for (int k = 0; k < 6; k++) {
        Serial.print(*cc, HEX);
        if (k != 5) Serial.print(":");
        cc++;
      }
      Serial.println("");
    }
  }
  delay(100); //wait a bit (100 ms)

  //if using regular connection use line below:
  WiFi.begin(network, password);
  //if using channel/mac specification for crowded bands use the following:
  //WiFi.begin(network, password, channel, bssid);


  uint8_t count = 0; //count used for Wifi check times
  Serial.print("Attempting to connect to ");
  Serial.println(network);
  while (WiFi.status() != WL_CONNECTED && count < 6) { //can change this to more attempts
    delay(500);
    Serial.print(".");
    count++;
  }
  delay(2000);  //acceptable since it is in the setup function.
  if (WiFi.isConnected()) { //if we connected then print our IP, Mac, and SSID we're on
    Serial.println("CONNECTED!");
    Serial.printf("%d:%d:%d:%d (%s) (%s)\n", WiFi.localIP()[3], WiFi.localIP()[2],
                  WiFi.localIP()[1], WiFi.localIP()[0],
                  WiFi.macAddress().c_str() , WiFi.SSID().c_str());
    delay(500);
  } else { //if we failed to connect just Try again.
    Serial.println("Failed to Connect :/  Going to restart");
    Serial.println(WiFi.status());
    ESP.restart(); // restart the ESP (proper way)
  }
}

/*----------------------------------
  char_append Function:
  Arguments:
     char* buff: pointer to character array which we will append a
     char c:
     uint16_t buff_size: size of buffer buff

  Return value:
     boolean: True if character appended, False if not appended (indicating buffer full)
*/
uint8_t char_append(char* buff, char c, uint16_t buff_size) {
  int len = strlen(buff);
  if (len > buff_size) return false;
  buff[len] = c;
  buff[len + 1] = '\0';
  return true;
}

/*----------------------------------
   do_http_request Function:
   Arguments:
      char* host: null-terminated char-array containing host to connect to
      char* request: null-terminated char-arry containing properly formatted HTTP request
      char* response: char-array used as output for function to contain response
      uint16_t response_size: size of response buffer (in bytes)
      uint16_t response_timeout: duration we'll wait (in ms) for a response from server
      uint8_t serial: used for printing debug information to terminal (true prints, false doesn't)
   Return value:
      void (none)
*/
void do_http_request(char* host, char* request, char* response, uint16_t response_size, uint16_t response_timeout, uint8_t serial) {
  WiFiClient client; //instantiate a client object
  if (client.connect(host, 80)) { //try to connect to host on port 80
    if (serial) Serial.print(request);//Can do one-line if statements in C without curly braces
    client.print(request);
    memset(response, 0, response_size); //Null out (0 is the value of the null terminator '\0') entire buffer
    uint32_t count = millis();
    while (client.connected()) { //while we remain connected read out data coming back
      client.readBytesUntil('\n', response, response_size);
      if (serial) Serial.println(response);
      if (strcmp(response, "\r") == 0) { //found a blank line!
        break;
      }
//      memset(response, 0, response_size);
      if (millis() - count > response_timeout) break;
    }
//    memset(response, 0, response_size);
    count = millis();
    while (client.available()) { //read out remaining text (body of response)
      char_append(response, client.read(), OUT_BUFFER_SIZE);
    }
    if (serial) Serial.println(response);
    client.stop();
    if (serial) Serial.println("-----------");
  } else {
    if (serial) Serial.println("connection failed :/");
    if (serial) Serial.println("wait 0.5 sec...");
    client.stop();
  }
}
