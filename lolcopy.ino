#include <SoftwareSerial.h>
#include <DFPlayerMini_Fast.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include "RTClib.h"  
SoftwareSerial sim800L(8,7); // RX, TX
SoftwareSerial mySerial(3,2); // RX, TX

#define RELAY_1 5 // for motor on 
#define RELAY_2 6  // for motor off
#define sql 9  // for notor on /off status
#define dql 10  // for motor sequrity

// OLED I2C address 
#define I2C_ADDRESS 0x3C
#define RST_PIN -1 
DFPlayerMini_Fast myDFPlayer;
SSD1306AsciiAvrI2c oled;
RTC_DS1307 rtc; 

unsigned long currentTime;
unsigned long loopTime1;
unsigned long loopTime2;

String top1,top2,top3,top4,at1,at2,alm,message,datedata,senderNumber,Motor_Stat,dateTimeInfo,Last_time;
boolean Display = false;
bool isPhoneNumberFound = false; 
char* phoneNumbers[] = {
    "+8801793496030",
    "+8801718677688",
    "+8801728754140",
    "+8801797723909",
    "+8801723349796"
};
String buff;
String dtmf_cmd;
boolean is_call = false;

 
void setup()
{ 
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
  oled.setFont(System5x7); 

  Serial.begin(9600);
  mySerial.begin(9600);
  rtc.begin(); 
  myDFPlayer.begin(mySerial);
  myDFPlayer.volume(29); 
  sim800L.begin(9600);
  Serialoled("Begin serial communication with (SIM800L)"); // Initialize serial communication with SIM800L
  delay(1000);
  sim800L.println("AT+CMGF=1"); // Set SMS mode to text
  delay(500);
  sim800L.println("AT+CNMI=2,1,0,0,0"); // Set SMS notification
  delay(500);
  sim800L.println("AT+CSCS=\"GSM\"");   // Set character set to GSM
  delay(500);
  sim800L.println("AT+CPMS=\"SM\",\"SM\",\"SM\"");  // Set preferred storage for SMS to SIM
  delay(7000);
  sim800L.println("AT"); //send AT
  delay(500);
  sim800L.println("AT+DDET=1"); //Enable DTMF
  delay(500);
  sim800L.println("AT+CMGF=1"); //Enable sms
  delay(500);
  sim800L.println("AT+CNMI=2,2,0,0,0"); //Enable sms
  delay(500);
  sim800L.println("AT+CMGD=1,4");
  delay(500);
  
  pinMode(RELAY_1, OUTPUT); // Relay for motor on
  pinMode(RELAY_2, OUTPUT); // Relay for motor off
  pinMode(sql, INPUT_PULLUP); // Switch for motor on/off status
  pinMode(dql, INPUT_PULLUP); // Switch for motor security
  digitalWrite(RELAY_1, 0); 
  digitalWrite(RELAY_2, 0);
  motoroled("READY TO PREPARE"); // Display initial message on OLED
  
  top1="AT";
  top2="OK";
  top3="||||";
  top4="92%";
  at1="AT";
  at2="OK";
  message="";
  datedata="";
  senderNumber="";
  alm="";
  Motor_Stat="";

  play_music(0,5000); // Play welcome tune
  Serialoled("Begin done ):");      
  motoroled("WELCOME TO SMEC ltd. "); // Display welcome message on OLED 
  
}

void loop()
{
  currentTime = millis();

  while(sim800L.available()){
    buff = sim800L.readString();
    //Serial.println(buff); 
    parseSMS(buff);
    verfynumber(buff);
     
    if(is_call == true)
    { 
      if(int index = buff.indexOf("+DTMF:") > -1 )
      {
        index = buff.indexOf(":");
        dtmf_cmd = buff.substring(index+1, buff.length());
        dtmf_cmd.trim();
        //Serial.println("dtmf_cmd: "+dtmf_cmd);
        doAction();
      }     
      if(buff.indexOf("NO CARRIER") > -1)
      {
        sim800L.println("ATH");
        is_call = false;
          top1="Ready";
          at1="AT";
          message="";
      }
    }
    if(buff.indexOf("RING") > -1){
      top1="RING";
      delay(2000);
     if (buff.indexOf(senderNumber)) {
        sim800L.println("ATA");
        at1="ATA";
        is_call = true;

        welcometune(); 
      } else {
        sim800L.println("ATH");
        is_call = false; 
        sim800L.println("AT+CMGD=1,4");

      }
    } 
  } 
  //-------------------------------------------------------
  while(Serial.available())  {
    sim800L.println(Serial.readString());
    //Serial.println(sim800L.println(Serial.readString()));
  }
  //-------------------------------------------------------
  
  //-------------------------------------------------------
  while(currentTime >=(loopTime1 + 3000) )  {
    int Aleart = digitalRead(sql);
    int Motor = digitalRead(dql);
    if(Motor == 0){
    //Serial.println("Motor On");
    Motor_Stat="M-ON";
    } else{
    //Serial.println("Motor off");
    Motor_Stat="";
    }
    if(Aleart == 0){
    //Serial.println("Alarm  deativated");
    alm="Normal";
    }else{
      //Serial.println("Alarm Activated");
    alm="Emergency";
    }
      Dynamicoled();
      Alarm();
      loopTime1 = currentTime;
  }
  //-------------------------------------------------------
}

void parseSMS(String sms) {
  // Check if the SMS contains specific commands or instructions
  if(sms.indexOf(senderNumber)){
    if (sms.indexOf("MOTOR_ON") != -1) {
    //Serial.println(sms.indexOf("MOTOR_ON") != -1 +"lol");
    MotorStatar("motor_on", RELAY_1, 7, 2000, 3000); 
    sendSMS("MOTOR ON  "+ fullTime());
  } else if (sms.indexOf("MOTOR_OFF") != -1) {
    MotorStatar("motor_on", RELAY_2, 10, 2000, 3000); 
    sendSMS("MOTOR OFF  "+ fullTime());
  } else if (sms.indexOf("RTC") != -1) {
     rtcsetup(sms);
    sendSMS("RTC SET UP SUCCESSFUL "+ fullTime());
  } else if (sms.indexOf("STTS") != -1) {
     
     if(Motor_Stat="M-ON"){
        sendSMS("NAW MOTOR ON  "+ Last_time);
     }else if (Motor_Stat=""){
        sendSMS("NAW MOTOR OFF  "+ Last_time);
     }

  }  else if (sms.indexOf("TIME") != -1) { 
     
    sendSMS("Time & Date"+ fullTime());

  }  else if (sms.indexOf("ETC") != -1) { 
     
    sendSMS("Electricity Available");


  } else if (sms.indexOf("list") != -1) { 
    String text = "";
      for (int i = 0; i < sizeof(phoneNumbers) / sizeof(phoneNumbers[0]); i++) {
        
      if(phoneNumbers[i])
        //text = text + phoneNumbers[1]+"\r\n";
        text += phoneNumbers[i];
          text += "\n"; // Add a newline for readability
          // Serial.print("Phone number ");
          // Serial.print(i + 1);
          // Serial.print(": ");
          Serialoled(phoneNumbers[i]);
      } 
          //Serial.println(text);

      sendSMS(text);

  }

  }
  
  
}
void rtcsetup( String sms){
    int startIndex = sms.indexOf("\"", sms.indexOf("\"", sms.indexOf(",") + 1) + 1) + 1;
    dateTimeInfo = sms.substring(startIndex); 
    String data = dateTimeInfo.substring(2, 23);
    //Serial.println(data); 
    String dateStr = data.substring(0, 9);
    //Serial.println(dateStr);
    int year = dateStr.substring(0, 2).toInt()+2000;
    int month = dateStr.substring(3, 5).toInt();
    int day = dateStr.substring(6).toInt();
 
    String timeStr = data.substring(9, 17);
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(3, 5).toInt();
    int second = timeStr.substring(6).toInt();
 
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
}


void doAction(){
  if(dtmf_cmd == "1"){ 
    if(Motor_Stat == ""){
      MotorStatar("motor_on",RELAY_1, 7, 2000, 3000);
      sendSMS("MOTOR ON  "+ fullTime());
      message="MOTOR ON";
      Motor_Stat="M-ON";
    }else if(Motor_Stat == "M-ON"){
      play_music(7,3000);
    }
     
  }
  else if(dtmf_cmd == "2"){ 
     if(Motor_Stat == "M-ON"){
        MotorStatar("motor_on",RELAY_2, 10, 2000, 3000); 
        message="MOTOR OFF"; 
        sendSMS("MOTOR OFF  "+ fullTime());
        Motor_Stat="";
    } else{
      play_music(10,3000);

    }

  }
  else if(dtmf_cmd == "3"){
    if(Motor_Stat == ""){
      play_music(11,3000);
    }else if(Motor_Stat == "M-ON"){
      play_music(14,3000);
    }

  }  
  else if(dtmf_cmd == "0"){
        sim800L.println("ATH");
        is_call = false;

  }  
}
void welcometune(){ 
play_music(3,3000);
play_music(4,3000); 
}


String fullTime() {
    DateTime today = rtc.now();
    int minute = today.minute();
    int hour = today.hour();
    int second = today.second(); 
    int hour12 = hour % 12;
    if (hour12 == 0) {  hour12 = 12;  }  
    String period = (hour < 12) ? "AM" : "PM";
    int day = today.day();
    int year = today.year();
    int month = today.month();
    int fulldate[] = {minute, hour12, second, day, year, month};
    String formattedTime = String(fulldate[1]) + ":" + String(fulldate[0]) + ":" + String(fulldate[2]) + " " + period + "-" + String(fulldate[3]) + "/" + String(fulldate[5]) + "/" + String(fulldate[4]);
    Last_time =formattedTime;
    return formattedTime;
}
 

// motor control function
void MotorStatar(String data,int relay_data, int music , int time, int musicplay){
  if(data == "motor_on"){
    digitalWrite(relay_data, 1);
    delay(time);
    digitalWrite(relay_data, 0);
    play_music(music,musicplay); 
    datedata=fullTime();
    }
}
void play_music(int data, int time){
    myDFPlayer.play(data);
    delay(time);
}
void sendSMS(String message) {
      sim800L.println("AT+CMGF=1"); // Set SMS mode to text
      delay(300);
      // sim800L.print("AT+CMGS=\"+phone_no\"\r"); // Replace with the phone number to send SMS
      sim800L.print("AT+CMGS=\"");
      sim800L.print(senderNumber);
      sim800L.print("\"\r");
      delay(300);
      sim800L.println(message);
      delay(300);
      sim800L.write(0x1A); // Send Ctrl+Z to end SMS
      delay(300);
}
void makeCall(String number) {
    Serialoled(number);
  sim800L.println("ATD" + number + ";"); // Replace number with the recipient's phone number
  delay(15000);
  sim800L.println("ATH");
}

/*
AT
Check Sim is ready or not
AT+CPIN?
 */
 void verfynumber(String buff){
        //Serial.println("___________sender number find--------");
       // Serial.println(buff);
        for (int i = 0; i < sizeof(phoneNumbers) / sizeof(phoneNumbers[0]); i++) {
          if (buff.indexOf(phoneNumbers[i]) > -1) {
            //Serial.println(phoneNumbers[i]); // incoming verified number
            senderNumber=String(phoneNumbers[i]);
              isPhoneNumberFound = true;
              break; // Exit loop early if a phone number is found
          }
      }


      // // Output verification result
      // if (isPhoneNumberFound) {
      //     Serial.println("At least one phone number found in buff.");
      // } else {
      //     Serial.println("No phone number found in buff.");
      // }
      //   Serial.println("___________sender number find--------");


 }
void Alarm(){
  if(alm == "Emergency"){
    Serialoled("make call");
   // sendSMS("Motor Alert !!!!");
     delay(1500);
     for (int i = 0; i < sizeof(phoneNumbers) / sizeof(phoneNumbers[0]); i++) {
        makeCall(phoneNumbers[i]);
        delay(15000); // Wait for 15 seconds before calling the next number
    } 
      delay(2000);
  }
}
 
 void Dynamicoled(){
   oled.clear();
   oled.setCursor(0,0);
   oled.print(top1); 
   oled.setCursor(35,0);
   oled.print(top2); 
   oled.setCursor(70,0);
   oled.print(top3); 
   oled.setCursor(105,0);
   oled.print(top4); 
   //------------ 3rd line for at commad ----------
   oled.setCursor(0,3);
   oled.print(at1); 
   oled.setCursor(40,3);
   oled.print(at2); 
   oled.setCursor(70,3);
   oled.print(alm); 
   // ----------- message opsation ---------------
   oled.setCursor(0,4);
   oled.print(message); 
   // ----------- date opsation ---------------
   oled.setCursor(0,5);
   oled.print(datedata); 
   // ----------- number opsation ---------------
   oled.setCursor(0,6);
   oled.print(senderNumber); 
   delay(1000);  
 }
 
 void motoroled(String data){
   oled.clear();
   oled.setCursor(0,5);
   oled.print(data );  
   delay(700);
   //oled.clear();
   Display = false;

 }
 void Serialoled(String data){
   oled.clear();
   oled.setCursor(0,7);
   oled.print(data );  
   delay(300);
   //oled.clear(); 

 }
 void collarid (String id){
  String line = id;
  int start_index = line.indexOf('"'); 
  int end_index = line.indexOf('"', start_index + 1); 
  String caller_id = line.substring(start_index + 1, end_index);
  senderNumber= caller_id;
  //Serial.print("Caller ID: ");
 // Serial.println(caller_id);
  
 }