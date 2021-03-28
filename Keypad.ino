/*
 * Commands:
 * "*" Turn on alarm mode, result: ALARM_MODE_ON (if delayExit > 0 wait delayExit 
 * seconds before sending ALARM_MODE_ON, Press '*' again to cancel before time is out)
 * 
 * "CODE*" Turn off alarm and alarm mode, (CODE is 1-9 digits), result: CODE_FAIL,
 * TURN_ON_ALARM,ALARM_MODE_OFF, ALARM_OFF
 * 
 * "#CODE*P*NEW_CODE*" Store NEW_CODE (1-9 digits) in register P 
 * (P is 1-MAX_NUMBER_OF_CODES), result: CODE_CHANGED, OPERATION_FAILED, 
 * CODE_FAIL, TURN_ON_ALARM
 * 
 * If delayEntry > 0 and the MQTT message MQTT_ENTER_PAYLOAD is recieved in the 
 * MQTT_TOPIC_IN topic and we are armed, Send ALARM_MODE_OFF and ALARM_OFF and 
 * wait for delayEntry seconds. If a correct code is not entered during that time, 
 * send ALARM_MODE_ON, ALARM_ON.
 * 
 * If no code is stored in any register, the default CODE is 12345678
 * 
 * The Keypad communicates with a MQTT server, it subscribes to topic MQTT_TOPIC_IN 
 * and send data to topic MQTT_TOPIC_OUT
 */

#include <Wire.h>

#include <Key.h>
#include <Keypad.h>
#include <EEPROM.h>

#include <Ethernet.h>
#include <PubSubClient.h>

#define MQTT_ENTER_PAYLOAD "ENTER"
#define MQTT_ALARM_MODE_ON "ALARM_MODE_ON"
#define MQTT_ALARM_MODE_OFF "ALARM_MODE_OFF"
#define MQTT_ALARM_ON "ALARM_ON"
#define MQTT_ALARM_OFF "ALARM_OFF"

const int RED_PIN=A0;
const int GREEN_PIN=A1;

const int EEPROMPOS=0; // Where to start save EEPROM;
const int MAGIC0=47;   // Magic key in EEPROM start
const int MAGIC1=11;    // Magic key in EEPROM start+1
const int MAX_CODE_LENGTH=9; // Max number of digits in code
const int MIN_CODE_LENGTH=4; // Min number of digits in code
const int MAX_NUMBER_OF_CODES=4; // Max number of codes stored (do not change to more than 9)
const int MAX_NUMBER_OF_FAILED_ATTEMPTS=3; // Max number of failed attempts before turning on alarm

// Operation modes
const int ENTER_ALARM_CODE=0;
const int ENTER_CHANGE_CODE=1;
const int ENTER_CHANGE_REGISTER=2;
const int ENTER_NEW_CODE=3;

// Result codes

const int ALARM_MODE_ON=0; // Turn the Alarm mode on
const int ALARM_MODE_OFF=1; // Turn off alarm mode
const int ALARM_ON=2; // Turn the Alarm on (too many failed attempts)
const int ALARM_OFF=3; // Turn off the alarm (ALARM_OFF will also set ALARM_MODE_OFF)
const int CODE_FAIL=4; // Sub command number of failed attempts
const int CODE_CHANGED=5; // Sub command with the register changed
const int OPERATION_ERROR=6; // Sent when wrong code or wrong register in change code

const char *operation[7]={MQTT_ALARM_MODE_ON, MQTT_ALARM_MODE_OFF, MQTT_ALARM_ON, MQTT_ALARM_OFF, 
                          "CODE_FAIL", "CODE_CHANGED", "OPERATION_ERROR"};

const char * MQTT_SERVER="ip-for-your-mqtt-server";
const int    MQTT_PORT=1883;
const char * MQTT_USERNAME="username-for-your-mqtt-server";
const char * MQTT_PASSWORD="password-for-your-mqtt-server";
const char * MQTT_TOPIC_IN="keypad_in";
const char * MQTT_TOPIC_OUT="keypad_out";

const char * MQTT_CLIENT_ID="arduino-1";


const byte KEYPAD_ROWS = 4; //four rows
const byte KEYPAD_COLS = 3; //four columns
//define the symbols on the buttons of the keypads
char keyMap[KEYPAD_ROWS][KEYPAD_COLS] = {
{'1','2','3'},
{'4','5','6'},
{'7','8','9'},
{'*','0','#'}
};
char codes[MAX_NUMBER_OF_CODES][MAX_CODE_LENGTH+1]; // codes are cached in this variable
char str[MAX_CODE_LENGTH+1];
bool alreadyPressed=false;
// Position 1-7 on the keypad goes to arduino digital pin 9-3
byte rowPins[KEYPAD_ROWS] = {8, 3, 4, 6}; //connect to the row pinouts of the keypad
byte colPins[KEYPAD_COLS] = {7, 9, 5};    //connect to the column pinouts of the keypad
//initialize an instance of class NewKeypad
Keypad customKeypad = Keypad(makeKeymap(keyMap), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS); 
int operation_mode; /* ENTER_ALARM_CODE, ENTER_CHANGE_CODE, ENTER_CHANGE_REGISTER, ENTER_NEW_CODE */
int entered_register=0; /* 1,...,MAX_NUMBER_OF_CODES are valid values */
int failed_attempts=0; /* Number of failed code attempts */

byte mac[] = {0x2A, 0x27, 0x55, 0x08, 0x1E, 0x7A}; // I just use a random number here

int light_mode=0; // 0=normal (green), 1=network fail(green blink), 2=armed(red), 4=alarm(red blink)
                  // Also some combination: 3=armed+network fail, 5=alarm+network fail

int     green_count=0;
boolean green_status;

int     red_count=0;
boolean red_status;

int     delayEntry=60; // If the MQTT message ENTER is recieved in the keypad_in topic, Turn off ALARM_MODE_OFF
                       // (+ ALARM_OFF ?) for delayEntry seconds if not the right code is entered 
                       // turn on alarm ALARM_ON
int     delayExit=60;  // If '*' is pressed at the keypad, wait for delayExit seconds before the 
                       // ALARM_MODE_ON is sent

int     networkWatchdog=60; // Check network connection each networkWatchdog seconds, 0 = skip check

unsigned long currentDelayEntry = 0L;
unsigned long currentDelayExit = 0L;                   
unsigned long currentNetworkWatchdog = 0L;

EthernetClient ethClient;
PubSubClient mqttClient(ethClient);

void reset_data(char * msg) {
  operation_mode=ENTER_ALARM_CODE;
  failed_attempts=0;
  entered_register=0;
  updateDiode(GREEN_PIN, HIGH);
  Serial.println(msg);
}

void callback(char* topic, byte* payload, unsigned int length) {
  char payloadstr[20];
  int i=0; // Only retrieve at maximum 19 chars from payload
  for (; i < 19 && i < length; i++) {
    payloadstr[i]=(char) payload[i];
  }
  payloadstr[i]='\0';
  Serial.print("Message arrived at ");
  Serial.println(topic);

  if (strcmp(MQTT_ENTER_PAYLOAD,payloadstr) == 0 && delayEntry > 0) {
    if ((light_mode / 2) == 1) {
      Serial.println("Turn off alarms, wait for password");
      sendCommand(ALARM_MODE_OFF, 9);
      sendCommand(ALARM_OFF, 9);
      currentDelayEntry=millis() / 1000L;    
    } else {
      Serial.println("Got ENTER, not armed - ignoring");
    }
  } else if (strcmp(MQTT_ALARM_OFF,payloadstr) == 0 || strcmp(MQTT_ALARM_MODE_OFF,payloadstr) == 0) {
    light_mode=(light_mode % 2);
    reset_data(payloadstr);
    updateDiode(RED_PIN, LOW);
    if (strcmp(MQTT_ALARM_MODE_OFF,payloadstr) == 0) {
      sendCommand(ALARM_MODE_OFF, 9);
    } else {
      sendCommand(ALARM_OFF, 9);
      sendCommand(ALARM_MODE_OFF, 9);
    }
  } else if (strcmp(MQTT_ALARM_MODE_ON,payloadstr) == 0) {
    if ((light_mode / 4) != 1) { // Skip arming if already in alarm mode
      light_mode=2+(light_mode % 2);
    }
    reset_data(payloadstr);
    currentDelayExit=0L;
    updateDiode(RED_PIN, HIGH);
    sendCommand(ALARM_MODE_ON, 9);
  } else if (strcmp(MQTT_ALARM_ON,payloadstr) == 0) {  
    light_mode=4+(light_mode % 2);
    reset_data(payloadstr);
    sendCommand(ALARM_MODE_ON, 9);
    sendCommand(ALARM_ON, 9);
  } else {
    Serial.print("Skip payload ");
    Serial.println(payloadstr);
  }
}

boolean connectMQTT() {
  if (!mqttClient.connected())
  {
    boolean r=mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    if (r) {
      delay(10);
      boolean r2=mqttClient.subscribe(MQTT_TOPIC_IN);
      if (!r2) {
        Serial.print("Failed sub on ");
        Serial.println(MQTT_TOPIC_IN);
      }
      return r2;
    } else {
        Serial.println("Failed connect to mqtt server");
        return false;
    }
  }
  return true;
}

void updateDiode(int pin, int high_or_low) {
  if (pin == GREEN_PIN) {
    digitalWrite(GREEN_PIN, high_or_low);
    green_status=high_or_low == HIGH;
  } else {
    digitalWrite(RED_PIN, high_or_low);
    red_status=high_or_low == HIGH;    
  }
}

/* register = 1...MAX_NUMBER_OF_CODES */
void writeEEPROMString(int reg) {
  EEPROM.write(EEPROMPOS,MAGIC0);
  EEPROM.write(EEPROMPOS+1,MAGIC1);
  for (int i=0; i<strlen(codes[reg-1]); i++) {
    EEPROM.write(EEPROMPOS+i+2+(reg-1)*(MAX_CODE_LENGTH+1),codes[reg-1][i]);
  }
  EEPROM.write(EEPROMPOS+strlen(codes[reg-1])+2+(reg-1)*(MAX_CODE_LENGTH+1),'\0');
}

/* register = 1...MAX_NUMBER_OF_CODES */
void readEEPROMString(int reg) {
  int magic0=EEPROM.read(EEPROMPOS);
  int magic1=EEPROM.read(EEPROMPOS+1);
  if (magic0 != MAGIC0 || magic1 != MAGIC1)
  {
    Serial.println("No saved EEPROM data");
    return "";
  }
  int pos=0;
  char ch;
  while (pos < MAX_CODE_LENGTH && (ch = EEPROM.read(EEPROMPOS+pos+2+(reg-1)*(MAX_CODE_LENGTH+1))) != '\0') {
    codes[reg-1][pos]=ch;
    pos=pos+1;
  }
  codes[reg-1][pos]='\0';
}

// Returns the register for the correct code or 0 if not correct
int isCodeCorrect(char *enteredCode) {
  for (int i=0; i<MAX_NUMBER_OF_CODES; i++) {
    if (strcmp(codes[i], enteredCode) == 0) {
      return i+1;
    }
  }
  return 0;
}

void sendCommand(int cmd, int subcmd) {

  switch (cmd) {
    case ALARM_MODE_ON  : light_mode = 2 + (light_mode % 2);
                          break;
    case ALARM_MODE_OFF :
    case ALARM_OFF      : light_mode = (light_mode % 2);
                          break;
    case ALARM_ON       : light_mode = 4 + (light_mode % 2);
  }

  if (connectMQTT()) {
    // Go from unconnected to connectecd
    if ((light_mode % 2) == 1) {
      light_mode=light_mode-1;
      updateDiode(GREEN_PIN, HIGH);
    }
  } else {
    // Go from connected to unconnected
    if ((light_mode % 2) == 0) {
      light_mode=light_mode+1;
    }
  }
  
  Serial.print(operation[cmd]);
  Serial.print(" ");
  Serial.println(subcmd);
  boolean rc = mqttClient.publish(MQTT_TOPIC_OUT, operation[cmd]);
  Serial.print("MQTT sent rc=");
  Serial.println(rc);
  Serial.print("light_mode=");
  Serial.println(light_mode);
}

void fix_led_lights() {
  // green
  if (light_mode == 0)
  {
    if (!green_status) {
      updateDiode(GREEN_PIN, HIGH);
    }
  }

  // green flashing, can be combined with other red light
  if ((light_mode % 2) == 1) {
    if (!green_status) {
      updateDiode(GREEN_PIN, HIGH);
    } else {
      updateDiode(GREEN_PIN, LOW);
    }
    
  }

  // red
  if ((light_mode / 2) == 1) {
    if (!red_status) {
      updateDiode(RED_PIN, HIGH);
    }
  }

  // red flashing
  if ((light_mode / 4) == 1 || currentDelayExit > 0 || currentDelayEntry > 0) {
    if (!red_status) {
      updateDiode(RED_PIN, HIGH);
    } else {
      updateDiode(RED_PIN, LOW);
    }
  }

  if (currentDelayExit > 0 || currentDelayEntry > 0) {
    if (red_status) {
      delay(85);
    } else {
      delay(15);
    }
  } else if ((light_mode % 2) == 1 || (light_mode / 4) == 1) {
    delay(50);
  }
}

void checkDelayedActions() {

  unsigned long now_s = millis() / 1000L;
  
  if (currentDelayExit > 0) {
    if (now_s - currentDelayExit < 0) { // Wrap around, set currentDelayExit again...
      currentDelayExit = now_s;
    }
    
    if ((currentDelayExit + (long)delayExit) < now_s) {
      Serial.println("Activating alarm mode after delay");
      sendCommand(ALARM_MODE_ON, 0);
      currentDelayExit=0L;
    }
  }

  if (currentDelayEntry > 0) {

    if (now_s - currentDelayEntry < 0) { // Wrap around, set currentDelayEntry again...
      currentDelayEntry = now_s;
    }
    
    if ((currentDelayEntry + (long)delayEntry) < now_s) {
      Serial.println("Wrong code entered, delayed entry expired => alarm");
      sendCommand(ALARM_MODE_ON, 0);
      sendCommand(ALARM_ON, 0);
      currentDelayEntry=0L;
    }
  }

  if (networkWatchdog > 0) {

    if (now_s - currentNetworkWatchdog < 0) { // Wrap around, set currentNetworkWatchdog again...
      currentNetworkWatchdog = now_s;
    }
    
    if ((currentNetworkWatchdog + (long) networkWatchdog) < now_s) {
      Serial.println("Network watchdog");
      if (!connectMQTT()) {
        ip_and_mqtt_setup();
        if (connectMQTT()) {
          if (light_mode % 2 == 1) {
            light_mode = light_mode - 1;
          }
        } else {
          if (light_mode % 2 == 0) {
            light_mode = light_mode + 1;
          }
        }
      } else {
        if (light_mode % 2 == 1) {
            light_mode = light_mode - 1;
        }
      }
      currentNetworkWatchdog=millis() / 1000L; // Might have passed some time, so don't use now_s
    }
  }
}

void mark_error() {
  updateDiode(RED_PIN, HIGH);
  delay(800);
  updateDiode(RED_PIN, LOW);
}

void state_enter_alarm_code(char *str) {
  // If str is empty, turn on alarm
  if (strlen(str) == 0) {
    if (delayExit > 0 && (delayEntry == 0 || currentDelayEntry == 0)) { // Possible delay exit but not in delay entry
      if (currentDelayExit == 0) {
        if (light_mode / 2 == 1) { // We are already armed, be consistent and send again
          sendCommand(ALARM_MODE_ON, 0);
        } else {
          currentDelayExit=millis() / 1000L;
          Serial.print("Enter delay exit");
        }
      } else {
        currentDelayExit = 0L;
        Serial.println("Removing delay exit");
        updateDiode(RED_PIN, LOW);
      }
    } else if (delayEntry == 0 || currentDelayEntry == 0) { // Ignore if we are in a delay entry
     sendCommand(ALARM_MODE_ON, 0);
    }
  } else {
    int res = isCodeCorrect(str);
    if (res > 0) {
      sendCommand(ALARM_MODE_OFF, res);
      sendCommand(ALARM_OFF, res);
      failed_attempts=0;
      // Turn off red led light
      updateDiode(RED_PIN, LOW);
      currentDelayEntry=0L; // Always turn off delay entry mode    
    } else {
      failed_attempts++;
      if (failed_attempts >= MAX_NUMBER_OF_FAILED_ATTEMPTS) {
        sendCommand(ALARM_ON, 0);
        currentDelayEntry=0L;
      } else {
        sendCommand(CODE_FAIL, failed_attempts);
        mark_error();
      }
    }
  }
}

void state_enter_change_code(char *str) {
  if (strlen(str) == 0) {
    sendCommand(OPERATION_ERROR, 0);
    mark_error();
    operation_mode=ENTER_ALARM_CODE;
  } else {
    int res = isCodeCorrect(str);
    if (res > 0) {
      operation_mode=ENTER_CHANGE_REGISTER;
      entered_register = 0;
      failed_attempts=0;
    } else {
      failed_attempts++;
      if (failed_attempts >= MAX_NUMBER_OF_FAILED_ATTEMPTS) {
        sendCommand(ALARM_ON, 0);
      } else {
        sendCommand(CODE_FAIL, failed_attempts);
        mark_error();
      }
      operation_mode=ENTER_ALARM_CODE;
    }
  }
}

void state_enter_change_register(char *str) {
  if (strlen(str) == 0) {
    sendCommand(OPERATION_ERROR, 1);
    mark_error();
    operation_mode=ENTER_ALARM_CODE;
  } else {
    int reg = str[0]-'0';
    if (strlen(str) > 1 || reg < 1 || reg > MAX_NUMBER_OF_CODES) {
      sendCommand(OPERATION_ERROR, 2);
      mark_error();
      operation_mode=ENTER_ALARM_CODE;
    } else {
      entered_register = reg;
      operation_mode=ENTER_NEW_CODE;
    }
  }
}

void state_enter_new_code(char *str) {
  if (strlen(str) == 0) { // It would be possible to clear a code this way, not implemented now
    sendCommand(OPERATION_ERROR, 3);
    mark_error();
  } else if (strlen(str) < MIN_CODE_LENGTH) {
    sendCommand(OPERATION_ERROR, 4);
    mark_error();
  } else {
    strcpy(codes[entered_register-1],str);
    writeEEPROMString(entered_register);
    sendCommand(CODE_CHANGED, entered_register);
  }
  operation_mode=ENTER_ALARM_CODE;
}

void ip_and_mqtt_setup() {
  //Fixed ip, gw and subnetmask (saves 114 bytes in Global variables memory)
  //IPAddress myIP(192, 168, 0, 5);
  //IPAddress gateway(192, 168, 0, 1);
  //IPAddress subnet(255, 255, 255, 0);
  //Ethernet.begin(mac, myIP, gateway, subnet);
  
  Ethernet.begin(mac); // Reserves digital pin 10,11,12,13 

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);

  int retrycount=1;
  boolean is_connected=false;
  while (retrycount <= 10 && !is_connected)
  {
    delay(retrycount*1000L);
    if (connectMQTT()) {
      Serial.println("Connected to MQTT server");
      if (light_mode % 2 == 1) {
        light_mode = light_mode - 1;
      }
      is_connected=true;
    } else {
      if (light_mode % 2 == 0) {
        light_mode = light_mode + 1;
      }
    }
    retrycount++;
  }
}

void setup() {
  Serial.begin(9600);


  readEEPROMString(1);
  if (strlen(codes[0]) == 0) /* You must have code 1 set otherwise set default code in 1 and empty others */
  {
    strcpy(codes[0],"12345678");
    writeEEPROMString(1);
    for (int i=2; i<=MAX_NUMBER_OF_CODES; i++) {
      codes[i-1][0]='\0';
      writeEEPROMString(i);
    }
    Serial.println("Setting default code in reg 1");
  }
  
  Serial.println("Reading codes from EEPROM");
  for (int i=1; i<MAX_NUMBER_OF_CODES; i++) {
    readEEPROMString(i);
  }
    
  alreadyPressed=false;
  
  str[0]='\0'; // Always clear string for now
  operation_mode=ENTER_ALARM_CODE;
  failed_attempts=0; 

  ip_and_mqtt_setup();

  pinMode(RED_PIN, OUTPUT);
  updateDiode(RED_PIN, LOW);
  pinMode(GREEN_PIN, OUTPUT);
  updateDiode(GREEN_PIN, LOW);

  currentDelayEntry = 0L;
  currentDelayExit = 0L;

  currentNetworkWatchdog = millis() / 1000L;
}

void loop() {

  fix_led_lights();

  mqttClient.loop();

  checkDelayedActions();
  
  char customKey = customKeypad.getKey();
  KeyState state = customKeypad.getState();
  if (state == PRESSED && !alreadyPressed) {
    alreadyPressed=true;
    if (customKey == '#') {
      if ((delayEntry == 0 || currentDelayEntry == 0) && (delayExit == 0 || currentDelayExit == 0)) { // don't change code in delay Entry/Exit
        operation_mode=ENTER_CHANGE_CODE;
      }
      str[0]='\0';
    } else if (customKey == '*') {
      if (operation_mode == ENTER_ALARM_CODE) {
        state_enter_alarm_code(str);
      } else if (operation_mode == ENTER_CHANGE_CODE) {
        state_enter_change_code(str);
      } else if (operation_mode == ENTER_CHANGE_REGISTER) {
        state_enter_change_register(str);
      } else if (operation_mode == ENTER_NEW_CODE) {
        state_enter_new_code(str);
      } else {
        sendCommand(OPERATION_ERROR, 5);
        operation_mode=ENTER_ALARM_CODE;
      }
      str[0]='\0';
    } else {
      if (strlen(str) < MAX_CODE_LENGTH) {
        int len=strlen(str);
        str[len]=customKey;
        str[len+1]='\0';
      } else {
        Serial.println("Max chars reached, ignoring");
      }
    }
    Serial.println(str);
  } else if (state != PRESSED) {
    alreadyPressed=false;
  }
}
