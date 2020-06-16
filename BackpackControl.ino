////Control Software for Backpack power unit
////Created by D O'Connor

//The trend for voltage on the cap uses all hist_ca values
//The display uses hist_cap/hist_cap_average_div
//The relay should use the live value

//Pin definitions
//D12,11,5,4,3,2 - Screen
//A0 - Main Bus Voltage
//A1 - Main Cap Voltage
//D6 - Relay for inverter  (LOW == ON)
//D7 - Relay for cap bank  (LOW == ON)

//Constants
#define hist_bus 150 
#define hist_cap 500 //assert %10==0
#define hist_cap_average_div 10
#define min_inverter_on_time 2000 //in ms

//Libraries
#include <LiquidCrystal.h>
#include <String.h>

//Vars - Display
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
unsigned long dispTime = millis();
int bus_display_val = 0;   //not used yet, might implement
int cap_display_val = 0;

//Vars - Input/Averaging/Trend
float vBus = 0.0f, vCap = 0.0f;
int prev_bus[hist_bus];
int prev_bus_pos = 0;
int prev_cap[hist_cap];
int prev_cap_pos = 0;
bool trend = false;

//Vars - Relays/Switching
int desired_joules = 80; 
bool bank_relay = true; //true == off
bool inverter_status = true; //true == off
unsigned long inverterTime = millis();
bool fake_button_press = true; //true == off; press comes from serial;


//Initialize HW and Vars
void setup() {
  lcd.begin(16, 2);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  digitalWrite(6, HIGH);
  digitalWrite(7, HIGH);
  for(int i=0;i<hist_bus;i++) prev_bus[i]=0;
  for(int i=0;i<hist_cap;i++) prev_cap[i]=0;
  Serial.begin(9600);
}


//Main
void loop() {
  //Fetch current Values from HW and correct for tollerances
  vBus = analogRead(A0)*2.45f;
  vCap = analogRead(A1)*2.65f;
  
  
  //Store bus value in history and increment iterator 
  prev_bus[prev_bus_pos]=int(vBus);
  prev_bus_pos++;
  if(prev_bus_pos==hist_bus)prev_bus_pos=0;
  
  //Store cap value in history and increment iterator 
  prev_cap[prev_cap_pos]=int(vCap);
  prev_cap_pos++;
  if(prev_cap_pos==hist_cap)prev_cap_pos=0;
  
  
  //Get average of each half of histvars older values vs newer ones
  int averageOld = 0,averageNew = 0;
  for(int i=0; i<(hist_cap/2); i++){
    averageNew+=prev_cap[(prev_cap_pos+hist_cap-1-i)%hist_cap];
    averageOld+=prev_cap[(prev_cap_pos+i)%hist_cap];
  }

  //Find which half was larger, if newer half, trend is up, otherwise down
  if(averageNew>averageOld)trend=true;
  else trend=false;


  //busAverage is an average of all prev_bus_vals 
  float busAverage = 0; 
  for(int i=0; i<hist_bus; i++)
    busAverage+=prev_bus[(prev_bus_pos+hist_bus-1-i)%hist_bus];
  busAverage/=hist_bus;

  //capAverage is a sum of most recent 10% of prev_cap_vals
  float capAverage = 0; 
  for(int i=0; i<(hist_cap/hist_cap_average_div); i++)
    capAverage+=prev_cap[(prev_cap_pos+hist_cap-1-i)%hist_cap];
  capAverage/=(hist_cap/hist_cap_average_div);
    
  //Change cap_display_val if it agrees with the current trend direction
  if((cap_display_val<vCap)&&trend) cap_display_val=round(capAverage);
  if((cap_display_val>vCap)&&!trend) cap_display_val=round(capAverage);

 
  //Display updates only each 0.1sec (measure in ms)
  if(millis()>(dispTime+100)){
    lcd.clear();
    
    //Display the voltages and trend of the voltage on cap bank
    lcd.setCursor(3-(String(busAverage,0).length()),0);
    lcd.print(String(vBus,0)+"vBus");
    lcd.setCursor(11-(String(cap_display_val).length()),0);
    String bankStr = (String(cap_display_val) + "vCap");
    lcd.print(bankStr);
    if(trend) lcd.print("+"); //Charging
    else lcd.print("-");      //Discharging

    //Current max energy we could have if vCap == vBus
    lcd.setCursor(3-String((0.008*busAverage*busAverage),0).length(),1);
    lcd.print(String((0.008*busAverage*busAverage),0) + "Jm");
    
    //Current energy in the cap bank
    lcd.setCursor(9-(String((0.008*capAverage*capAverage),0).length()),1);
    lcd.print(String((0.008*capAverage*capAverage),0) + "Jc");

    //Desired joules and direction we need to go
    lcd.setCursor(15-(String(desired_joules).length()),1);
    lcd.print(String(desired_joules));
    if((0.008*capAverage*capAverage)<desired_joules) 
      lcd.print("-"); //Not charged enough
    else if ((0.008*capAverage*capAverage)>(desired_joules*1.5))
      lcd.print("+"); //Significantly over
    else
      lcd.print("="); //Approximately equal or greater

    //Set time for framerate limit
    dispTime=millis();
  }


  //Perform any switching operations //Maybe dont use averages...
  //Apply hysteresis 5% or bound of 10V max
  if((0.008*vCap*vCap)<desired_joules){
    digitalWrite(7, LOW); //Means it is charging
    bank_relay = false;
  }

  if((0.008*vCap*vCap)>desired_joules){
    digitalWrite(7, HIGH); //Means it is not charging off
    Serial.println("Turning off cap relay");
    bank_relay = true;
  }



  //Check for fake button press, store into fake button
  if(Serial.read()=='i'){
    fake_button_press = false;
  }


  //Turn on the inverter since the button was pressed
  if(!fake_button_press){
    digitalWrite(6, LOW);
    inverterTime = millis();
    inverter_status = false; //false is on
    fake_button_press = true; //reset the button press
  }

  //Turn off the inverter after a set min time 
  //TODO: Add a condition to only turn off if inverter not needed
  if((inverterTime+min_inverter_on_time)<millis()&&!inverter_status){
    digitalWrite(6, HIGH);
    inverter_status = true;
  }


  //Debug: Printouts
  //Serial.print(String(vBus,1) + "vBus - " + String(vCap,1) + "vCap - ");
  //Serial.print(String(trend) + " Trend - " + String(cap_display_val) + "Dval - ");
  //Serial.print(String(averageOld) + " Old - " + String(averageNew) + " New\n");

  //Debug: Output of cycle frequency, loop cyc/sec
  digitalWrite(7, HIGH);
  digitalWrite(7, LOW);
  

  //loop delay, too fast otherwise, unnecessary
  delay(1);
}
