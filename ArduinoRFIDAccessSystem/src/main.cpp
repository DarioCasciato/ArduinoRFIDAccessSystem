#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <MFRC522.h>
#include <string.h>
#include "../lib/Arduino_SK6812/SK6812.h"



/*Pin definition*/
#define RST_PIN 9
#define SS_PIN 10

#define SIGNALIZER_BUZZER 14
#define SIGNALIZER_LED 15

/*Define size of Whitelist (depends on RAM size of Controller)*/
#define WHITELIST_SIZE 10

#define ADDRESS_WHITELIST 0x020
#define ADDRESS_MASTER 0x010



/*struct for edge trigger*/
typedef struct
{
  bool act;
  bool edge;
  bool edge_pos;
  bool edge_neg;
  bool old;
} edge_t;

typedef struct
{
  unsigned char loopcounter;
  unsigned char pulse;
} timer_t;



/*Objects*/
edge_t RfidPresent = {0};
timer_t time = {0};
SK6812 LED(1);                    // Numbers of LEDs in LED chain
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

/*Colors*/
RGBW color_red = {100, 0, 0, 0}; // Values from 0-255
RGBW color_green = {0, 100, 0, 0};
RGBW color_off = {0, 0, 0, 0};



// Signalisation Functions
void SignalPositive();
void SignalPositiveSound();
void SignalResetWhitelist();
void SignalPermDenied();
void SignalReject();
void SignalClose();
void SignalFullReset();

// Program Logic Functions
bool tagPresent();
bool checkMaster();
unsigned long getUID();

// Whitelist Functions
void whitelistRemove(unsigned long UID);
void whitelistAdd(unsigned long UID);
void whitelistReset();
bool isWhitelistMember(unsigned long UID);

//Master functions
void masterSet(unsigned long UID);
void masterReset();



/*RFID reading variables*/
int blockNum = 2;
byte blockData[16] = {"MasterMediumCard"};

MFRC522::StatusCode status;
MFRC522::MIFARE_Key key;

/*Flags*/
bool isMaster = 0;
bool repeatFlagPresent = 0;
unsigned long wasPresent = 0;
bool wasPresentMaster = 0;

/*UID*/
unsigned long TagUID;
unsigned long whitelist[WHITELIST_SIZE] = {0};
unsigned long registeredMaster;

byte bufferLen = 18;
byte readBlockData[18];

enum states_t {noMaster, idle};
states_t state = noMaster;


void setup()
{
  /*Initialisation*/
  Serial.begin(9600);
  SPI.begin();        // Initiate  SPI bus
  mfrc522.PCD_Init(); // Initiate MFRC522

  /*Pin Initialisation*/
  pinMode(SIGNALIZER_BUZZER, OUTPUT);
  pinMode(SIGNALIZER_LED, OUTPUT);
  LED.set_output(SIGNALIZER_LED); // Digital Pin

  /*Signalisation setup*/
  delay(100);
  LED.set_rgbw(0, color_off);
  LED.sync();
  noTone(SIGNALIZER_BUZZER);

  

  //Get Whitelist
  EEPROM.get(ADDRESS_WHITELIST, whitelist);

  for(uint8_t loop = 0; loop < WHITELIST_SIZE; loop++)
  {
    if(whitelist[loop] == 0xFFFFFFFF) whitelist[loop] = NULL;
  }

  //Get Master
  EEPROM.get(ADDRESS_MASTER, registeredMaster);
  if(registeredMaster == 0xFFFFFFFF) registeredMaster = 0;

  masterReset();

}

void loop()
{
  //----------Loop Header

  // edge trigger setup
  RfidPresent.act = tagPresent();
  RfidPresent.edge = RfidPresent.act ^ RfidPresent.old;
  RfidPresent.edge_pos = RfidPresent.edge & RfidPresent.act;
  RfidPresent.edge_neg = RfidPresent.edge & RfidPresent.old;


  // Read Tag Values
  isMaster = checkMaster();
  if(RfidPresent.edge_pos) TagUID = getUID();
  
  if(isMaster) wasPresentMaster = 1;
  if(RfidPresent.edge_neg) wasPresent = TagUID;

  for (byte i = 0; i < 6; i++)
    key.keyByte[i] = 0xFF;

  //----------Loop Main
	
  // MAIN CODE HERE
  switch (state)
  {
  case noMaster:
    if(RfidPresent.act)
    {
      if(isMaster)
      {
        LED.set_rgbw(0, color_green);
        LED.sync();
      }
    }

    if(RfidPresent.edge_neg && wasPresentMaster)
    {
      masterSet(wasPresent);
      SignalFullReset();
      state = idle;
    }
    break;
  
  case idle:
    if(RfidPresent.edge_pos)
    {
      if(isMaster)
      {
        if(TagUID != registeredMaster) SignalReject();
      }

      else if(!isWhitelistMember(TagUID)) SignalPermDenied();
    }
    break;

  default:
    break;
  }

  //----------Loop Footer

  wasPresent = 0;
  RfidPresent.old = RfidPresent.act;
  if(RfidPresent.edge_neg) 
  {
    wasPresentMaster = 0;
    TagUID = 0;
  }

  //----------Timer Setup

  time.pulse = 0;

  if (time.loopcounter++ >= 100)
  {
    time.loopcounter = 0;
    time.pulse = 1;
  }

  delay(10);
}

/*----------Signalisation Functions*/

void SignalPositive()
{
  tone(SIGNALIZER_BUZZER, 3000);
  LED.set_rgbw(0, color_green);
  LED.sync();
  delay(150);
  LED.set_rgbw(0, color_off);
  LED.sync();
  noTone(SIGNALIZER_BUZZER);
}

void SignalPositiveSound()
{
  tone(SIGNALIZER_BUZZER, 3000);
  delay(150);
  LED.set_rgbw(0, color_off);
  LED.sync();
  noTone(SIGNALIZER_BUZZER);
}

void SignalPermDenied()
{
  for (char i = 0; i < 4; i++)
  {
    tone(SIGNALIZER_BUZZER, 3000);
    LED.set_rgbw(0, color_red);
    LED.sync();
    delay(120);

    LED.set_rgbw(0, color_off);
    LED.sync();
    noTone(SIGNALIZER_BUZZER);
    delay(120);
  }
}

void SignalReject()
{
  LED.set_rgbw(0, color_off);
  LED.sync();
  tone(SIGNALIZER_BUZZER, 3000);
  delay(350);
  LED.set_rgbw(0, color_red);
  LED.sync();
  delay(150);
  LED.set_rgbw(0, color_off);
  LED.sync();
  noTone(SIGNALIZER_BUZZER);
}

void SignalClose()
{
  tone(SIGNALIZER_BUZZER, 3000);
  delay(1000);
  noTone(SIGNALIZER_BUZZER);
}

void SignalResetWhitelist()
{
  LED.set_rgbw(0, color_off);
  LED.sync();
  tone(SIGNALIZER_BUZZER, 3000);
  delay(500);
  noTone(SIGNALIZER_BUZZER);
  delay(120);
  tone(SIGNALIZER_BUZZER, 3000);
  delay(150);
  noTone(SIGNALIZER_BUZZER);
}

void SignalFullReset()
{
  for (char i = 0; i < 2; i++)
  {
    tone(SIGNALIZER_BUZZER, 3000);
    LED.set_rgbw(0, color_off);
    LED.sync();
    delay(120);

    noTone(SIGNALIZER_BUZZER);
    delay(120);
  }
  delay(1500);

  LED.set_rgbw(0, color_green);
  LED.sync();
  delay(800);
  LED.set_rgbw(0, color_off);
  LED.sync();
}

/*----------Program Logic Functions*/

bool checkMaster()
{
  status = NULL;
  byte status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 2, &key, &(mfrc522.uid));

  if (status != MFRC522::STATUS_OK)
    return 0;

  /* Reading data from the Block */
  status = mfrc522.MIFARE_Read(2, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK)
    return 0;
  else
  {
    for (int j = 0; j < 16; j++)
    {
      if (blockData[j] != readBlockData[j])
      {
        j = 17;
        return 0;
      }
      if (j == 15)
        return 1;
    }
  }
}

bool tagPresent()
{

  if (!mfrc522.PICC_IsNewCardPresent())
  {
    if (repeatFlagPresent)
    {
      repeatFlagPresent = 0;
      return 1;
    }
    return 0;
  }
  if (mfrc522.PICC_ReadCardSerial())
  {
    if (mfrc522.PICC_IsNewCardPresent())
    {
    }
    if (!mfrc522.PICC_IsNewCardPresent())
    {
      if (repeatFlagPresent)
      {
        repeatFlagPresent = 0;
        return 1;
      }
      return 0;
    }

    /* Select one of the cards */
    if (!mfrc522.PICC_ReadCardSerial())
    {
      if (repeatFlagPresent)
      {
        repeatFlagPresent = 0;
        return 1;
      }
      return 0;
    }
    repeatFlagPresent = 1;
    return 1;
  }
}

unsigned long getUID()
{
  status = NULL;
  byte status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 2, &key, &(mfrc522.uid));

  if (status != MFRC522::STATUS_OK) return TagUID;

  /* Reading data from the Block */
  status = mfrc522.MIFARE_Read(2, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK) return 0;
  else
  {
    unsigned long tempUID[4] = {0};
    for (byte i = 0; i < mfrc522.uid.size; i++)
    tempUID[i] = mfrc522.uid.uidByte[i];

    volatile unsigned long UID = 0;
    UID |= (tempUID[0] << 24);
    UID |= (tempUID[1] << 16);
    UID |= tempUID[2] << 8;
    UID |= tempUID[3];

    return UID;    
  }
}

/*----------Whitelist Functions*/

void whitelistRemove(unsigned long UID)
{
  for (unsigned char searchLoop = 0; searchLoop < WHITELIST_SIZE; searchLoop++)
  {
    if (whitelist[searchLoop] == UID)
    {
      // Deletes User
      whitelist[searchLoop] == NULL;

      // Moves back the "NULL" value
      for (int moveLoop = searchLoop; moveLoop < WHITELIST_SIZE; moveLoop++)
        whitelist[moveLoop] = whitelist[moveLoop + 1];
      
      whitelist[WHITELIST_SIZE] = NULL;

      EEPROM.put(ADDRESS_WHITELIST, whitelist);
      break;
    }
  }
  return;
}

void whitelistAdd(unsigned long UID)
{
  for (unsigned char searchLoop = 0; searchLoop < WHITELIST_SIZE; searchLoop++)
  {
    if (whitelist[searchLoop] == UID)
    {
      // Signal Positive | UID ist bereits in liste gespeichert
      return;
    }
  }

  for (unsigned char nextNull = 0; nextNull < WHITELIST_SIZE; nextNull++)
  {
    if (whitelist[nextNull] == 0 || whitelist[nextNull] == NULL)
    {
      whitelist[nextNull] = UID;
      EEPROM.put(ADDRESS_WHITELIST, whitelist);
      // Signal positive | user zur liste hinzugefügt
      return;
    }
  }

  // Signal list full reject
  return;
}

void whitelistReset()
{
  for (unsigned char deleteLoop = 0; deleteLoop < WHITELIST_SIZE; deleteLoop++)
  {
    whitelist[deleteLoop] = NULL;
    EEPROM.put(ADDRESS_WHITELIST, whitelist);
  }

  void SignalResetWhitelist();
  return;
}

bool isWhitelistMember(unsigned long UID)
{
  for (unsigned char searchLoop = 0; searchLoop < WHITELIST_SIZE; searchLoop++)
  {
    if (whitelist[searchLoop] == 0 || whitelist[searchLoop] == NULL)
      return 0;
    if (whitelist[searchLoop] == UID)
      return 1;
  }
  return 0;
}

/*Master Functions*/
void masterSet(unsigned long UID)
{
  registeredMaster = UID;
  EEPROM.put(ADDRESS_MASTER, registeredMaster);
}

void masterReset()
{
  registeredMaster = 0;
  EEPROM.put(ADDRESS_MASTER, 0);
}
