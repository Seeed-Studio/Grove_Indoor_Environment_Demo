#include <Servo.h>
#include <SPI.h>
#include <WiFi.h>
#include <TimerOne.h>
#include "rgb_lcd.h"
#include <stdlib.h>
#include <string.h>
#include <TH02_dev.h>


#define CMDSTR_MAX_LEN (128)
#define LOW_TEMP       (10)
#define NOM_TEMP       (25) 
#define HIGH_TEMP      (40)

#define SENSOR_COUNT   (6)
#define ACTUATOR_COUNT (4)
#define VAR1_COUNT     (SENSOR_COUNT+1)
#define VAR2_COUNT     (ACTUATOR_COUNT+1)
typedef void (*pActuatorHandler)(int);
typedef int (*pgetSensorValue)(void);

int getTempSensorValue();
int getSoundSensorValue();
int getHumiSensorValue();
int getMoistrueSensorValue();
int getLightSensorValue();
int getUVSensorValue();
int getPIRSensorValue();

void RelayHandle(int val);
void BuzzerHandle(int val);
void ServoHandle(int val);
void SleepHandle(int val);

rgb_lcd lcd;
boolean isBackLightOn = true;

char cmdstr[CMDSTR_MAX_LEN];

char ssid[48] = "EdisonAP";           // your network SSID (name) 
char psw[48] = "12345678";       // your network password
boolean isSSIDreconfiged = false;
int keyIndex = 0;                // your network key Index number (needed only for WEP)
int status = WL_IDLE_STATUS;
WiFiServer server(88);
int serverconnected = 0;

unsigned char BLColorRGB[]={0x00, 0x00, 0xFF};  //Backlight color

const int numRows = 2;
const int numCols = 16;

const int MenuCount = 5;
int MenuIndex = 0;

const int MoistrueSensorIndex = 0;
const int LightSensorIndex = 1;
const int UVSensorIndex = 2;
const int THSensorIndex = 3;
const int LocalIPIndex = 4;

const int pinSound = A0;
const int pinMoistrue = A1;
const int pinLight = A2;
const int pinUV = A3;
const int pinButton =0;
const int pinEncoder1 = 2;
const int pinEncoder2 = 3;
const int pinBuzzer = 4;
const int pinRelay = 5;
const int pinPIR = 7;
const int pinServo = 6;
Servo myservo;

const char* SerialVarList1[] = { "temp", "humi", "light", "uv", "pir", "ms","ssid"};
const char* SerialVarList2[] = { "relay", "buzzer", "servo", "sleep", "psw"};
enum ACTUATOR {RELAY=0,BUZZER,SERVO,SLEEP};
enum SENSOR {TEMPER=0,HUMI,LIGHT,UV,PIR,MS};

pgetSensorValue getSensorValueList[]={
  getTempSensorValue, 
  getHumiSensorValue, 
  getLightSensorValue, 
  getUVSensorValue, 
  getPIRSensorValue, 
  getMoistrueSensorValue
};

int SensorValue[SENSOR_COUNT];
                                      
pActuatorHandler ActuatorHandlerList[]={RelayHandle, BuzzerHandle, ServoHandle, SleepHandle};

int SensorConfig[][4] = {       // value, condition, Actuator, action
  {85,  '>', SLEEP,  1},        //Temperature
  {100, '>', SLEEP,  1},        //Humdity
  {200, '<', SERVO,  90},        //Light
  {1023,'>', SLEEP,  1},        //UV
  {2,   '=', BUZZER, 1},        //PIR
  {600, '<', RELAY,  1}         //Moistrue
};

void printSettings(void){
  Serial.println("The Sensors Configurations as follow:");
  for(int i=0; i<SENSOR_COUNT;i++){
    Serial.print(SerialVarList1[i]);
    Serial.write((char)SensorConfig[i][1]);
    Serial.print(SensorConfig[i][0]);
    Serial.write(',');
    Serial.print(SerialVarList2[SensorConfig[i][2]]);
    Serial.write('=');
    Serial.println(SensorConfig[i][3]);    
  }
  Serial.print('\n');
  Serial.print("SSID = ");
  Serial.print(ssid);
  Serial.print(", PSW = ");
  Serial.println(psw);
  
  Serial.println("The Sensors Value as follow:");
  for(int i=0; i<SENSOR_COUNT;i++){
    Serial.print(SerialVarList1[i]);
    Serial.print(" = ");
    Serial.println(getSensorValueList[i]());   
  }
}

void SerialRequestHandler(){
  if(cmdstrInput(cmdstr)){
     Serial.println(cmdstr);
     if(!parsecmd(cmdstr)){
       Serial.println("  ---- FAIL!");
       Serial.println("Please enter command: set temp>50,relay=1 etc.");
     }
     else {
       Serial.println("  ---- OK!");
     }
     printSettings();
  }
  int changedVal;
  for(int i=0;i<SENSOR_COUNT;i++){
    if(SensorConfig[i][2]==SERVO) changedVal = 0;
    else changedVal = !SensorConfig[i][0];
    switch (SensorConfig[i][1]){
      case '>':
        if(getSensorValueList[i]() > SensorConfig[i][0]) 
          ActuatorHandlerList[SensorConfig[i][2]](SensorConfig[i][3]);
        else ActuatorHandlerList[SensorConfig[i][2]](changedVal);
        break;
      case '<':
        if(getSensorValueList[i]() < SensorConfig[i][0]) 
          ActuatorHandlerList[SensorConfig[i][2]](SensorConfig[i][3]);
        else ActuatorHandlerList[SensorConfig[i][2]](changedVal);
        break;
      case '=':
        if(getSensorValueList[i]() == SensorConfig[i][0]) 
          ActuatorHandlerList[SensorConfig[i][2]](SensorConfig[i][3]);
        else ActuatorHandlerList[SensorConfig[i][2]](changedVal);
        break;
    }
  }
}

int parsecmd(char *cmd){
  char* cp = cmd;

/*  // use SSID and Passwrod, should not changed to lowcase
  while(*cp != '\0'){
    if(*cp >= 'A' && *cp <= 'Z') *cp += 0x20;     //tolower
    cp++;
  }
*/
  
  if(!(cmd[0]=='s' && cmd[1]=='e' && cmd[2]=='t' && cmd[3]==' ')) return 0;   //Illegal cmd
  
  char tmpcmd[CMDSTR_MAX_LEN];
  char *cp1=cmd+4;
  int len=0;
  while(*cp1 != '\0'){
    if(*cp1 != ' ') tmpcmd[len++] = *cp1;   //delete space
    cp1++;
  }
  tmpcmd[len]='\0';
  if(len<7) return 0;   // at least 7 characters,like T>0,R=1
  
  char *pvar1, *pvar2;
  pvar1 = strtok(tmpcmd,",");
  if(pvar1 == NULL) return 0;          //no setting
  pvar2 = strtok(NULL,",");
  if(pvar2 == NULL) return 0;          //no setting
  if(strtok(NULL,",")!=NULL) return 0; //more setting
  
  char op1='\0',op2='\0';
  char *pvarvalue1=NULL, *pvarvalue2=NULL;
  int i = 0;
  while(pvar1[i]!='\0'){
    if(pvar1[i]=='>' || pvar1[i]=='<' || pvar1[i]=='='){
      op1 = pvar1[i];
      pvar1[i] = '\0';
      pvarvalue1 = pvar1+i+1;
      break;
    }
    i++;
  }
  i=0;
  while(pvar2[i]!='\0'){
    if(pvar2[i]=='>' || pvar2[i]=='<' || pvar2[i]=='='){
      op2 = pvar2[i];
      pvar2[i] = '\0';
      pvarvalue2 = pvar2+i+1;
      break;
    }
    i++;
  }
  int varindex1,varindex2;
  for(i=0;i<VAR1_COUNT;i++)
    if(strcmp(pvar1,SerialVarList1[i])==0){
      varindex1 = i;
      break;
    }
    
  if(i==VAR1_COUNT) return 0;  // Illegal sensor name
  
  for(i=0;i<VAR2_COUNT;i++)
    if(strcmp(pvar2,SerialVarList2[i])==0){
      varindex2 = i;
      break;
    }
    
  if(i==VAR2_COUNT) return 0; // Illegal actuator name
  
  if(op1=='\0' || op2=='\0') return 0; // Illegal operator name
  
  if(varindex1 == VAR1_COUNT-1 && varindex2 == VAR2_COUNT-1){
    strcpy(ssid,pvarvalue1);
    strcpy(psw,pvarvalue2);
    isSSIDreconfiged = true;
  } 
  else if(varindex1 != VAR1_COUNT-1 && varindex2 != VAR2_COUNT-1) {
    int value1 = atoi(pvarvalue1), value2 = atoi(pvarvalue2);
    if(value1 < 0 || value1 > 1023) return 0;
    if(value2 < 0 || value2 > 1023) return 0;  
    SensorConfig[varindex1][0] = value1;
    SensorConfig[varindex1][1] = op1;
    SensorConfig[varindex1][2] = varindex2;
    SensorConfig[varindex1][3] = value2;
  }
    
  return 1;
}


//get Grove-Sound Sensor value
int getSoundSensorValue(){
    return analogRead(pinSound);
}
//get Temperature Senso value
int getTempSensorValue(){
  int temper =(int) TH02.ReadTemperature();
  SensorValue[TEMPER] = temper;
  return temper;
}

//get Humidity Senso value
int getHumiSensorValue(){
  int humidity =(int)TH02.ReadHumidity();
  SensorValue[HUMI] = humidity;
  return humidity;
}

//get Grove-Moistrue Sensor value
int getMoistrueSensorValue(){
  return (SensorValue[MS] = analogRead(pinMoistrue));
}
//get Grove-Light Sensor value
int getLightSensorValue(){
  return (SensorValue[LIGHT] = analogRead(pinLight));
}
//get Grove-UV Sensor value
int getUVSensorValue(){
  return (SensorValue[UV] = analogRead(pinUV));
}

//Grove PIR Motion Sensor
int getPIRSensorValue(){
  return (SensorValue[PIR] = digitalRead(pinPIR));
}

  
//display Temperature&Humidity Senso value on LCD
void displayTHSensorValue(){
  lcd.clear();
  lcd.print(" Temp&Humidity  ");
  lcd.setCursor(0,1);
  lcd.print("<-");
  char number[16];
  sprintf(number,"%d",getTempSensorValue());
  lcd.setCursor(3,1);
  lcd.print(number);
  lcd.print("C");
  sprintf(number,"%d",getHumiSensorValue());
  lcd.setCursor(9,1);
  lcd.print(number);
  lcd.print("%");
  lcd.setCursor(numCols-2,1);
  lcd.print("->");
}

//display Grove-Moistrue Sensor value on LCD
void displayMoistrueSensorValue(){
  lcd.clear();
  lcd.print("Moistrue Sensor ");
  char number[16];
  int len = sprintf(number,"%d",getMoistrueSensorValue());
  lcd.setCursor(0,1);
  lcd.print("<-");
  lcd.setCursor((numCols-len)/2,1);
  lcd.print(number);
  lcd.setCursor(numCols-2,1);
  lcd.print("->");
}

//display Grove-Light Sensor value on LCD
void displayLightSensorValue(){
  lcd.clear();
  lcd.print("  Light Sensor  ");
  char number[16];
  int len = sprintf(number,"%d",getLightSensorValue());
  lcd.setCursor(0,1);
  lcd.print("<-");
  lcd.setCursor((numCols-len)/2,1);
  lcd.print(number);
  lcd.setCursor(numCols-2,1);
  lcd.print("->");
}

//display Grove-UV Sensor value on LCD
void displayUVSensorValue(){
  lcd.clear();
  lcd.print("   UV Sensor    ");
  char number[16];
  int len = sprintf(number,"%d",getUVSensorValue());
  lcd.setCursor(0,1);
  lcd.print("<-");
  lcd.setCursor((numCols-len)/2,1);
  lcd.print(number);
  lcd.setCursor(numCols-2,1);
  lcd.print("->");
}

//display local ip
void displayLocalIP(){
  IPAddress ip = WiFi.localIP();
  char local_ip[16];
  sprintf(local_ip,"%d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
  local_ip[15]= '\0';
  lcd.clear();
  lcd.print("    Local IP    ");
  lcd.setCursor(0,1);
  lcd.print(local_ip);
}
    

//is button pressed
int isButtonPressed(){
  int state = 0;
  if(digitalRead(pinButton)){
    while(digitalRead(pinButton));
    state = 1;
  }
  return state;
}

void RelayHandle(int val){
  if(val==1) {
    digitalWrite(pinRelay,HIGH);
  }
  else if(val==0){
    digitalWrite(pinRelay,LOW);
  }
}
void BuzzerHandle(int val){
  if(!(val==0 || val==1)) return;
  if(val==1)
    digitalWrite(pinBuzzer,HIGH);
  else if(val==0)
    digitalWrite(pinBuzzer,LOW);
}
void ServoHandle(int val){
  static unsigned long curt = millis();
  if(millis()-curt > 1000){
    myservo.write(val);
    curt = millis();
  }
}

void SleepHandle(int val){
  return;
}  
  
  
void setup() 
{
  Serial.begin(9600);
    // set up the LCD's number of columns and rows:
    lcd.begin(16, 2);    
    
    WiFi_Init();
      
    pinMode(pinButton,INPUT);
    pinMode(pinRelay,OUTPUT);
    pinMode(pinBuzzer,OUTPUT);
    pinMode(pinEncoder1,INPUT);
    pinMode(pinEncoder2,INPUT);
    myservo.attach(pinServo);
    myservo.write(0);
    
    printSettings();
    
    Timer1.initialize(20000); // set a timer of length 2000 microseconds
    Timer1.attachInterrupt(timerIsr); // attach the service routine here
    attachInterrupt(pinEncoder2,EncoderISR,FALLING);    
}
void EncoderISR(){
    while(!digitalRead(pinEncoder2));
    if(digitalRead(pinEncoder1)==HIGH){
      MenuIndex++;
      if(MenuIndex >= MenuCount) MenuIndex = 0;
    } else {
      MenuIndex--;
      if(MenuIndex < 0) MenuIndex = MenuCount - 1;
    } 
}

//timer interrupt handler
void timerIsr(){
  RequestHandler();
}

// display menu on the LCD screen
void displayMenu(){
  static unsigned long curtime = millis();
  if(millis()-curtime > 1000){
    //noInterrupts();
    switch(MenuIndex){
      case(THSensorIndex):
        displayTHSensorValue();
        break;     
      case(MoistrueSensorIndex):
        displayMoistrueSensorValue();
        break;
      case(UVSensorIndex):
        displayUVSensorValue();
        break;
      case(LightSensorIndex):
        displayLightSensorValue();
        break;
      case(LocalIPIndex):
        displayLocalIP();
        break;
     }
     curtime = millis();
    //interrupts();
  }
}

void TempColorHandle(){
  int temp = getTempSensorValue();
  if(!isBackLightOn){
    lcd.setRGB(0,0,0);
    return;
  }
  if(temp<LOW_TEMP) {
    BLColorRGB[0]=0x00; 
    BLColorRGB[1]=0x00; 
    BLColorRGB[2]=0xFF;
  }
  else if(temp<NOM_TEMP){
    BLColorRGB[0]=0x00;
    BLColorRGB[1]=map(temp,LOW_TEMP,NOM_TEMP-1,0,255);  
    BLColorRGB[2]=map(temp,LOW_TEMP,NOM_TEMP-1,255,0);
  }
  else if(temp<HIGH_TEMP){
    BLColorRGB[0]=map(temp,NOM_TEMP,HIGH_TEMP-1,0,255); 
    BLColorRGB[1]=map(temp,NOM_TEMP,HIGH_TEMP-1,255,0);
    BLColorRGB[2]=0x00;
  }
  else {
    BLColorRGB[0]=0xFF; 
    BLColorRGB[1]=0x00;
    BLColorRGB[2]=0x00;
  }
  lcd.setRGB(BLColorRGB[0],BLColorRGB[1],BLColorRGB[2]);
}  
    
void loop() 
{
  TempColorHandle();
  
  displayMenu();
  
  SerialRequestHandler();
   
  //if button pressed, backlight transforms
  if(isButtonPressed()) isBackLightOn = !isBackLightOn;
  
  //if reconfig WiFi SSID,reconnect.
  if(isSSIDreconfiged==true) {
    status = WL_IDLE_STATUS;
    isSSIDreconfiged = false;
  }
    
  
}

int cmdstrInput(char *str){
  static int index = 0;
  if(Serial.available()){
    char inChar = Serial.read();
    cmdstr[index++]=inChar;
    if(inChar == '\n' || index >= CMDSTR_MAX_LEN){
      cmdstr[index-1]='\0';
      index = 0;
      return 1;
    }
  }
  return 0;
}

//initialize wifi connection 
void WiFi_Init(){
  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present"); 
    // don't continue:
    while(true);
  } 

  String fv = WiFi.firmwareVersion();
  if( fv != "1.1.0" )
    Serial.println("Please upgrade the firmware");
}

// print the WiFi connection status using serialport
void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

// handl wifi request
void RequestHandler(){
  static unsigned long cur = millis();
  if(status != WL_CONNECTED && *ssid != '\0' && *psw !='\0'){
    if((millis()-cur) > 5000){
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:    
      status = WiFi.begin(ssid,psw);
      cur = millis();
    }
  } else if(status==WL_CONNECTED) {
    if(!serverconnected){
      server.begin();
      // you're connected now, so print out the status:
      printWifiStatus();
      serverconnected = 1;
    } else {
      sendpage();
    }
  }
}

// send html to client
void sendpage(){
   // listen for incoming clients
  WiFiClient client = server.available();
  if (client) {
    Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Refresh: 2");         // refresh the page automatically every 2 sec
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("<title>Grove Indoor Environment Kit for Edison</title>"); 
          client.println("<font face=\"Microsoft YaHei\" color=\"#0071c5\"/>");
          client.println("<h1 align=\"center\">Grove Indoor Environment Kit for Edison</h1>");
          client.print("<br />");
          for(int i=0; i < SENSOR_COUNT; i++){
            client.print("<h2 align=\"center\"><big>");
            client.print(SerialVarList1[i]);
            client.print(" = ");
            client.print(SensorValue[i]); 
            client.println("</big></h2>"); 
            //client.print("<br />");
          }      
          client.println("</html>");
           break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    
    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }
}
