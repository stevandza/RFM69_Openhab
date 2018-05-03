// Moja probna varijanta
// DS18B20 - pin4
// Senzor svetla TSL,  A4-SDA, A5 - SCL
// pin9-REL1,   pin7-REL2,   pin6 - REL3,    pin5-REL4
// pin A3 - BTN1
// pin 3  - Dimmer

// RFM69 DS18B20 node sketch
//
// This node talks to the MQTT-Gateway and will:
// - send sensor data periodically and on-demand
// - receive commands from the gateway to control actuators
// - receive commands from the gateway to change settings
//
// Configuration has been changed to adapt to Openhab example  November 2015, Computourist@gmail.com
//
// A DS18B20 is used for temperature & humidity measurements, other sensors and outputs can be added easily.
//
// Current defined devices are:
//
//	0	uptime:			read uptime node in minutes
//	1	node:			read/set transmission interval in seconds, 0 means no periodic transmission
//	2	RSSI:			read radio signal strength
//	3	Version:		read version node software
//	4	voltage:		read battery level
//	5	ACK:			read/set acknowledge message after a 'set' request
//	6	toggle:			read/set toggle function on button press
//	7	timer:			read/set activation timer after button press in seconds, 0 means no timer
//	9	retry:			read number of retransmissions needed in radiolink//	

//	16	actuator:		read/set LED or relay output
//  17  actuator:   read/set LED or relay output
//  18  actuator:   read/set LED or relay output
//  19  actuator:   read/set LED or relay output

//  33  dimmer1         :   dimovana rasveta - izlaz (zadana vrednost)
//  64  dimmer1 integer :   Ocitana stvarna vrednost salje se na openhab

//	40	Button:			tx only: message sent when button pressed
//  41  Button 1    tx only: message sent when button pressed 

//	48	temperature:		read temperature
//  50  lux:            read day level light


//	90	error:			tx only: error message if no wireless connection (generated by gateway)
//	92	error:			tx only: device not supported
//	99	wakeup:			tx only: first message sent on node startup
//
// 	The button is set to:
//	- generate a message on each state change of the output
//	- toggle the output ACT1 
//
//	A debug mode is included which outputs messages on the serial output
//
//	RFM69 Library by Felix Rusu - felix@lowpowerlab.com
//	Get the RFM69 library at: https://github.com/LowPowerLab/
//
//	version 1.7 by Computourist@gmail.com december 2014
//	version 2.0 increased payload size; implemented node uptime; standard device type convention; error handling .
//	version 2.1 removed device 8; changed handling of device 40; compatible with gateway V2.2	; march 2015
//	version 2.2 fixed bug in function TXradio that prevented retransmission of radio packets ; oct 2015
//	version 2.2_OH	special config to adapt for the Openhab example

#include <RFM69.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <SparkFunTSL2561.h>
SFE_TSL2561 light;           // Kreiramo objekat   

//
// CONFIGURATION PARAMETERS
//
#define NODEID 2 					// unique node ID within the closed network
#define GATEWAYID 1					// node ID of the Gateway is always 1
#define NETWORKID 100					// network ID of the network
#define ENCRYPTKEY "RFM69gateway1234" 			// 16-char encryption key; same as on Gateway!
#define DEBUG						// uncomment for debugging
#define VERSION "DHT V2.2_OH"				// this value can be queried as device 3

#define PORUKA1 "NOD2 NA DVORISTU"

// Wireless settings	Match frequency to the hardware version of the radio

//#define FREQUENCY RF69_433MHZ
#define FREQUENCY RF69_868MHZ
//#define FREQUENCY RF69_915MHZ

#define IS_RFM69HW 					// uncomment only for RFM69HW! 
#define ACK_TIME 50 			  // max # of ms to wait for an ack

// DS18B20      sensor setting
#define TEMP1  4          // DS18B20   data connection
 
#define ACT1 9						// Actuator pin (LED or relay)
#define ACT2 7            // Actuator pin (LED or relay)
#define ACT3 6            // Actuator pin (LED or relay)
#define ACT4 5            // Actuator pin (LED or relay)

#define DIM1 3            // Izlaz za dimmer 1

#define BTN  8						// Button pin
#define BTN1 A3           // Button pin 1


#define SERIAL_BAUD 115200
#define HOLDOFF 2000					// blocking period between button messages



//
//	STARTUP DEFAULTS
//
long 	TXinterval = 60;				  // periodic transmission interval in seconds
long	TIMinterval = 20;				  // timer interval in seconds
bool	ackButton = false;				// flag for message on button press
bool	toggleOnButton = true;		// toggle output on button press

//
//	VARIABLES
//
long	lastPeriod = -1;				// timestamp last transmission
long 	lastBtnPress = -1;				// timestamp last buttonpress
long  lastBtnPress1 = -1;        // timestamp last buttonpress1
long	lastMinute = -1;				// timestamp last minute
long	upTime = 0;					// uptime in minutes
float temp;					// humidity, temperature

int	 ACT1State;					 // status ACT1 output
int  ACT2State;          // status ACT2 output
int  ACT3State;          // status ACT3 output
int  ACT4State;          // status ACT4 output

int DIM1State;           // status Dimera1

int	signalStrength;					// radio signal strength
bool	setAck = true;					// send ACK message on 'SET' request
bool	send0, send1, send2, send3, send4;  // sistem
bool	send5, send6, send7, send9;         // sistem
bool	send16,send17, send18, send19; 		// message triggers - izlazni releji
bool  send33;                           // message triggers - Dimer1
bool  send40, send41;                   // message triggers - ulazi PIR, Tasteri itd
bool  send48, send50;           // message triggers - temperatura, osvetljaj
bool  send64;
bool  send92;                           // message triggers - poruke greske
bool	promiscuousMode = false; 			// only listen to nodes within the closed network
bool	curState = true;				// current button state
bool  curState1 = true;        // current button1 state
bool	lastState = true;				// last button state
bool  lastState1 = true;       // last button1 state
bool 	wakeUp = true;					// wakeup flag
bool	timerOnButton = false;				// timer output on button press
bool	msgBlock = false;				// flag to hold button messages to prevent overload
bool  msgBlock1 = false;       // flag to hold button messages to prevent overload
bool	retx = true; 					// flag to signal retransmission
int	numtx;							// number of retransmissions


double lux;       // Resulting lux value
boolean good;     // True if neither sensor is saturated
unsigned int data0, data1;
unsigned int ms;

typedef struct {					// Radio packet format
int	nodeID;						// node identifier
int	devID;						// device identifier 
int	cmd;						// read or write
long	intVal;						// integer payload
float	fltVal;						// floating payload
char	payLoad[32];					// string payload
} Message;

Message mes;

// Inicijalizacija DS18B20 temperaturne sonde
OneWire oneWire(TEMP1);
DallasTemperature DS18B20(&oneWire);
DeviceAddress insideThermometer;


RFM69 radio;

//
//=====================		SETUP	========================================
//
void setup() {
pinMode(BTN, INPUT_PULLUP);
pinMode(BTN1, INPUT_PULLUP);


#ifdef DEBUG
	Serial.begin(SERIAL_BAUD);
#endif
pinMode(ACT1, OUTPUT);					// set actuator 1
ACT1State = 0;
digitalWrite(ACT1, ACT1State);

pinMode(ACT2, OUTPUT);          // set actuator 2
ACT2State = 0;
digitalWrite(ACT2, ACT2State);

pinMode(ACT3, OUTPUT);          // set actuator 3
ACT3State = 0;
digitalWrite(ACT3, ACT3State);

pinMode(ACT4, OUTPUT);          // set actuator 4
ACT4State = 0;
digitalWrite(ACT4, ACT4State);

pinMode(DIM1, OUTPUT);         // set dimmera1
DIM1State = 0;
analogWrite(DIM1, DIM1State);

light.begin();  // Senzor svetla
light.setTiming(0,2,ms);
light.setPowerUp();

dht.begin();						// initialise temp/hum sensor

radio.initialize(FREQUENCY,NODEID,NETWORKID);		// initialise radio 
#ifdef IS_RFM69HW
radio.setHighPower(); 					// only for RFM69HW!
#endif
radio.encrypt(ENCRYPTKEY);				// set radio encryption	
radio.promiscuous(promiscuousMode);			// only listen to closed network
wakeUp = true;						// send wakeup message

#ifdef DEBUG
	Serial.print("Node Software Version ");
	Serial.println(VERSION);
	Serial.print("\nTransmitting at ");
	Serial.print(FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
	Serial.println(" Mhz...");
#endif
}	// end setup

//
//
//====================		MAIN	========================================
//
void loop() {
// RECEIVE radio input
//
if (receiveData()) parseCmd();				// receive and parse any radio input

// DETECT INPUT CHANGE
//
curState  = digitalRead(BTN);
msgBlock = ((millis() - lastBtnPress) < HOLDOFF);		// hold-off time for additional button messages
if (!msgBlock &&  (curState != lastState)) {			  // input changed ?
	delay(5);
	lastBtnPress = millis();						            	// take timestamp
	if (curState == LOW) {
	if (toggleOnButton) {								              // button in toggle state ?
	ACT1State = !ACT1State; 						            	// toggle output
	digitalWrite(ACT1, ACT1State);
	send16 = true;										                // set output state message flag
	} else
	if (TIMinterval > 0 && !timerOnButton) {		    	// button in timer state ?
	timerOnButton = true;								              // start timer interval
	ACT1State = HIGH;									                // switch on ACT1
	digitalWrite(ACT1, ACT1State);
	}}
lastState = curState;
}

curState1 = digitalRead(BTN1);
msgBlock1 = ((millis() - lastBtnPress1) < HOLDOFF);    // hold-off time for additional button messages
if (!msgBlock1 &&  (curState1 != lastState1)) {        // input changed ?
  delay(5);
  lastBtnPress1 = millis();                          // take timestamp                        
  send41 = true;                                    // set output state message flag  
  }
lastState1 = curState1;


// TIMER CHECK
//

if (TIMinterval > 0 && timerOnButton)			// =0 means no timer
{	
	if ( millis() - lastBtnPress > TIMinterval*1000) {	// timer expired ?
	timerOnButton = false;				// then end timer interval 
	ACT1State = LOW;				// and switch off Actuator
	digitalWrite(ACT1, ACT1State);
	}
}

// UPTIME 
//

if (lastMinute != (millis()/60000)) {			// another minute passed ?
	lastMinute = millis()/60000;
	upTime++;
	}

// PERIODIC TRANSMISSION
//

if (TXinterval > 0)
{
int currPeriod = millis()/(TXinterval*1000);
if (currPeriod != lastPeriod) {				// interval elapsed ?
	lastPeriod = currPeriod;
	
// list of sensordata to be sent periodically..
// remove comment to include parameter in transmission
 
//	send1 = true;					// send transmission interval
//	send2 = true; 					// signal strength
//	send4 = true;					// voltage level
//	send9 = true;					// number of retransmissions

	send16 = true;					// actuator state
  send17 = true;          // actuator state
  send18 = true;          // actuator state
  send19 = true;         // actuator state

  send64 = true;         // Dimmer1  state - ulaz je na 33 a izlaz (ocitano stanje je na 64)

  send40 = true;         // Button   state
  send41 = true;         // Button1  state
  
	send48 = true;					// send temperature
  send50 = true;          // send Lux - nivo svetla na dvoristu
	}
}

// SEND RADIO PACKETS
//

sendMsg();						// send any radio messages 

}		// end loop

//
//
//=====================		FUNCTIONS	==========================================

//
//========		RECEIVEDATA : receive data from gateway over radio
//

bool receiveData() {
bool validPacket = false;
if (radio.receiveDone())				// check for received packets
{
if (radio.DATALEN != sizeof(mes))			// wrong message size means trouble
#ifdef DEBUG
	Serial.println("invalid message structure..")
#endif
;
else
{
	mes = *(Message*)radio.DATA;
	validPacket = true;				// YES, we have a packet !
	signalStrength = radio.RSSI;
#ifdef DEBUG
	Serial.print(mes.devID);
	Serial.print(", ");
	Serial.print(mes.cmd);
	Serial.print(", ");
	Serial.print(mes.intVal);
	Serial.print(", ");
	Serial.print(mes.fltVal);
	Serial.print(", RSSI= ");
	Serial.println(radio.RSSI);
	Serial.print("Node: ");
	Serial.println(mes.nodeID);
#endif	
}
}
if (radio.ACKRequested()) radio.sendACK();		// respond to any ACK request
return validPacket;					// return code indicates packet received
}		// end recieveData

//
//
//==============		PARSECMD: analyse messages and execute commands received from gateway
//

void parseCmd() {					// parse messages received from the gateway
send0 = false;						// initialise all send triggers
send1 = false;
send2 = false;
send3 = false; 
send4 = false;
send5 = false;
send6 = false;
send7 = false;
send9 = false;

send16 = false;
send17 = false;
send18 = false;
send19 = false;

send64 = false;

send40 = false;
send41 = false;

send48 = false;

send50 = false;
send92 = false;

switch (mes.devID)					// devID indicates device (sensor) type
{
case (0):						// uptime
if (mes.cmd == 1) send0 = true;
break;
case (1):						// polling interval in seconds
if (mes.cmd == 0) {					// cmd == 0 means write a value
	TXinterval = mes.intVal;			// change interval to radio packet value
	if (TXinterval <10 && TXinterval !=0) TXinterval = 10;	// minimum interval is 10 seconds
	if (setAck) send1 = true;			// send message if required
#ifdef DEBUG
	Serial.print("Setting interval to ");
	Serial.print(TXinterval);
	Serial.println(" seconds");
#endif
}
else send1 = true;					// cmd == 1 is a read request, so send polling interval 
break;
case (2): 						// signal strength
if (mes.cmd == 1) send2 = true;
break;
case (3): 						// software version
if (mes.cmd == 1) send3 = true;
break;
case (4): 						// battery level
if (mes.cmd == 1) send4 = true;
break;
case (5): 						// set ack status
if (mes.cmd == 0) {
	if (mes.intVal == 0) setAck = false;
	if (mes.intVal == 1) setAck = true;
	if (setAck) send5 = true;			// acknowledge message ?
}
else send5 = true;					// read request means schedule a message
break;
case (6): 						// set toggle
if (mes.cmd == 0) {
	if (mes.intVal == 0) toggleOnButton = false;
	if (mes.intVal == 1) toggleOnButton = true;
	if (setAck) send6 = true;			// acknowledge message ?
}
else send6 = true;
break;
case (7):						// timer interval in seconds
if (mes.cmd == 0) {					// cmd == 0 means write a value
	TIMinterval = mes.intVal;			// change interval 
	if (TIMinterval <5 && TIMinterval !=0) TIMinterval = 5;
	if (setAck) send7 = true;			// acknowledge message ?
}							// cmd == 1 means read a value
else send7 = true;					// send timing interval 
break;
case (9):						// retransmissions
if (mes.cmd == 1) send9 = true;
break;

case (16):						// Actuator 1
if (mes.cmd == 0) {					// cmd == 0 means write
	if(mes.intVal == 0 || mes.intVal == 1) {
	ACT1State = mes.intVal;
	digitalWrite(ACT1, ACT1State);
	if (setAck) send16 = true;			// acknowledge message ?
#ifdef DEBUG	
	Serial.print("Set LED to ");
	Serial.println(ACT1State);
#endif
}}
else send16 = true;					// cmd == 1 means read
break;

case (17):            // Actuator 2
if (mes.cmd == 0) {         // cmd == 0 means write
  if(mes.intVal == 0 || mes.intVal == 1) {
  ACT2State = mes.intVal;
  digitalWrite(ACT2, ACT2State);
  if (setAck) send17 = true;      // acknowledge message ?
#ifdef DEBUG  
  Serial.print("Set LED to ");
  Serial.println(ACT2State);
#endif
}}
else send17 = true;         // cmd == 1 means read
break;

case (18):            // Actuator 3
if (mes.cmd == 0) {         // cmd == 0 means write
  if(mes.intVal == 0 || mes.intVal == 1) {
  ACT3State = mes.intVal;
  digitalWrite(ACT3, ACT3State);
  if (setAck) send18 = true;      // acknowledge message ?
#ifdef DEBUG  
  Serial.print("Set LED to ");
  Serial.println(ACT3State);
#endif
}}
else send18 = true;         // cmd == 1 means read
break;

case (19):            // Actuator 4
if (mes.cmd == 0) {         // cmd == 0 means write
  if(mes.intVal == 0 || mes.intVal == 1) {
  ACT4State = mes.intVal;
  digitalWrite(ACT4, ACT4State);
  if (setAck) send19 = true;      // acknowledge message ?
#ifdef DEBUG  
  Serial.print("Set LED to ");
  Serial.println(ACT4State);
#endif
}}
else send19 = true;         // cmd == 1 means read
break;

case (33):            // Dimmer1
if (mes.cmd == 0) {         // cmd == 0 means write  
  DIM1State = mes.intVal;
  analogWrite(DIM1, DIM1State);
  if (setAck) send33 = true;      // acknowledge message ?
  if (setAck) send64 = true;      // acknowledge message ?
#ifdef DEBUG  
  Serial.print("Set DIM1 to ");
  Serial.println(DIM1State);
#endif
}
else send33 = true;         // cmd == 1 means read
break;


case (40):						// binary input
if (mes.cmd == 1) send40 = true;
break;

case (41):            // binary input
if (mes.cmd == 1) send41 = true;
break;

case (48):						// temperature
if (mes.cmd == 1) send48 = true;
break;

case (50):            // Nivo svetla na dvoristu
if (mes.cmd == 1) send50 = true;
break;

default: send92 = true;					// no valid device parsed
}
}	// end parseCmd

//
//
//======================		SENDMSG: sends messages that are flagged for transmission
//

void sendMsg() {					// prepares values to be transmitted
bool tx = false; 					// transmission flag
mes.nodeID=NODEID;
mes.intVal = 0;
mes.fltVal = 0;
mes.cmd = 0;						// '0' means no action needed in gateway
int i;
for ( i = 0; i < sizeof(VERSION); i++){
mes.payLoad[i] = VERSION[i];	}
mes.payLoad[i] = '\0';					// software version in payload string

if (wakeUp) {						// send wakeUp call 
	mes.devID = 99;	
	wakeUp = false;					// reset transmission flag for this message
	txRadio();					// transmit
}
if (send0) {
	mes.devID = 0;
	mes.intVal = upTime;				// minutes uptime
	send0 = false;
	txRadio();
}
if (send1) {						// transmission interval
	mes.devID = 1;
	mes.intVal = TXinterval;			// seconds (integer)
	send1 = false;
	txRadio();
}
if (send2) {
	mes.devID = 2;
	mes.intVal = signalStrength;			// signal strength (integer)
	send2 = false;
	txRadio();
}
if (send3) {						// node software version (string)
	mes.devID = 3;					// already stored in payload string
	send3 = false;
	txRadio();
}
if (send4) {						// measure voltage..
	mes.devID = 4;	
	long result;					// Read 1.1V reference against AVcc
	ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
	delay(2);					// Wait for Vref to settle
	ADCSRA |= _BV(ADSC);				// Convert
	while (bit_is_set(ADCSRA,ADSC));
	result = ADCL;
	result |= ADCH<<8;
	result = 1126400L / result; 			// Back-calculate in mV
	mes.fltVal = float(result/1000.0);		// Voltage in Volt (float)
	txRadio();
	send4 = false;
}
if (send5) {						// Acknowledge on 'SET'
	mes.devID = 5;
	if (setAck) mes.intVal = 1; else mes.intVal = 0;// state (integer)
	send5 = false;
	txRadio();
}
if (send6) {						// Toggle on Buttonpress 
	mes.devID = 6;
	if (toggleOnButton) mes.intVal = 1; 		// read state of toggle flag
	else mes.intVal = 0;				// state (integer)
	send6 = false;
	txRadio();
}
if (send7) {						// timer interval
	mes.devID = 7;
	mes.intVal = TIMinterval;			// seconds (integer)
	send7 = false;
	txRadio();
}
if (send9) {						// number of retransmissions
	mes.devID = 9;
	mes.intVal = numtx;			// number (integer)
	send9 = false;
	txRadio();
}
if (send16) {						// state of Actuator 1
	mes.devID = 16;
	mes.intVal = ACT1State;				// state (integer)
	send16 = false;
	txRadio();
}
if (send17) {            // state of Actuator 2
  mes.devID = 17;
  mes.intVal = ACT2State;       // state (integer)
  send17 = false;
  txRadio();
}
if (send18) {            // state of Actuator 3
  mes.devID = 18;
  mes.intVal = ACT3State;       // state (integer)
  send18 = false;
  txRadio();
}
if (send19) {            // state of Actuator 4
  mes.devID = 19;
  mes.intVal = ACT4State;       // state (integer)
  send19 = false;
  txRadio();
}
if (send64) {            // state of Dimer1
  mes.devID = 64;
  mes.intVal =DIM1State;       // state (integer)
  send64 = false;
  txRadio();
}


if (send40) {						// Binary input read
	mes.devID = 40;
	if (curState == LOW) mes.intVal = 1;					// state (integer)
	send40 = false;
	txRadio();
}
if (send41) {            // Binary input read
  mes.devID = 41;
  if (curState1 == LOW) { mes.intVal = 1; }  // state (integer)
       else { mes.intVal = 0; }           // JA DODAO !!!
  send41 = false;
  txRadio();
}

if (send48) {						// Temperature
	mes.devID = 48;
     do {
          DS18B20.requestTemperatures(); 
          temp = DS18B20.getTempCByIndex(0);
          delay(10);
       }  while (temp == 85.0 || temp == (-127.0));
 
	mes.fltVal = temp;				     // Degrees Celcius (float)
	send48 = false;
	txRadio();
}


if (send50) {            // Lux - svetlo na dvoristu
  mes.devID = 50;
 if (light.getData(data0,data1))
  {      
    good = light.getLux(0, ms,data0,data1,lux);
  
  #ifdef DEBUG 
    if (good) Serial.println("(TSL2561 - GOOD)"); else Serial.print("(BAD)");
  #endif DEBUG  
   
  }
  else
  {
    byte error = light.getError();

    Serial.print("I2C error: ");
    Serial.print(error,DEC);
    Serial.print(", ");
 
  switch(error)
  {
    case 0:
      Serial.println("success");
      break;
    case 1:
      Serial.println("data too long for transmit buffer");
      break;
    case 2:
      Serial.println("received NACK on address (disconnected?)");
      break;
    case 3:
      Serial.println("received NACK on data");
      break;
    case 4:
      Serial.println("other error");
      break;
    default:
      Serial.println("unknown error");
  }
 
  } 
  
  mes.fltVal = lux/1.0;        // Lux (float)
  send50 = false;
  txRadio();
}

if (send92) {						// error message invalid device
	mes.intVal = mes.devID;
	mes.devID = 92;
  send92 = false;
	txRadio();
}

}
//
//
//=======================		TXRADIO
//

void txRadio()						// Transmits the 'mes'-struct to the gateway
{
retx = true;
int i = 0;

while (retx && i<6) {
if (radio.sendWithRetry(GATEWAYID, (const void*)(&mes), sizeof(mes),5)) {
	retx = false;
#ifdef DEBUG
	Serial.print(" message ");
	Serial.print(mes.devID);
	Serial.println(" sent...");
#endif
} else delay(500);
i++;
}
numtx = i;							// store number of retransmissions needed
#ifdef DEBUG
	if (retx) Serial.println("No connection...")
#endif
;}	// end txRadio






