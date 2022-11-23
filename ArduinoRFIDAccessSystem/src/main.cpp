//==================== Includes ====================

#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <MFRC522.h>
#include <string.h>
#include "../lib/Arduino_SK6812/SK6812.h"


//==================== Defines ====================

/*Pin definition*/
#define RST_PIN 9
#define SS_PIN 10

#define SIGNALIZER_BUZZER 14
#define SIGNALIZER_LED 15
#define SIGNALIZER_OPENER 17

/*How long the Lock should be open after authentication in seconds*/
#define OPEN_TIME 3
/*Define size of Whitelist (depends on RAM size of Controller)*/
#define WHITELIST_SIZE 100

#define ADDRESS_WHITELIST 0x020
#define ADDRESS_WHITELISTCOUNT 0x005
#define ADDRESS_MASTER 0x010

//==================== Objects ====================

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

/*enum for states*/
enum states_t {noMaster, idle, keying};

/*Classes*/
SK6812 LED(1);                    // Numbers of LEDs in LED chain
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

/*Colors*/
RGBW color_red = {100, 0, 0, 0}; // Values from 0-255
RGBW color_green = {0, 100, 0, 0};
RGBW color_off = {0, 0, 0, 0};

//==================== Function Prototypes ====================

// Signalisation Functions
void SignalPositive();
void SignalPositiveSound();
void SignalRemovedMember();
void SignalWhitelistFull();
void SignalEndKeying();
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

//==================== Global Variables ====================

/*RFID reading variables*/
int blockNum = 2;
byte blockData[16] = {"MasterMediumCard"};
 
MFRC522::StatusCode status;
MFRC522::MIFARE_Key key;

/*Flags*/
bool repeatFlagPresent = 0;

/*UID*/
unsigned long TagUID = 0;
unsigned long whitelist[WHITELIST_SIZE] = {0};
unsigned char whitelistMemberCount = 0;
unsigned long registeredMaster = 0;

byte bufferLen = 18;
byte readBlockData[18];


//==================== Setup ====================

void setup()
{
  /*Initialisation*/
  Serial.begin(9600);
  SPI.begin();        // Initiate  SPI bus
  mfrc522.PCD_Init(); // Initiate MFRC522

  /*Pin Initialisation*/
  pinMode(SIGNALIZER_BUZZER, OUTPUT);
  pinMode(SIGNALIZER_LED, OUTPUT);
  pinMode(SIGNALIZER_OPENER, OUTPUT);
  LED.set_output(SIGNALIZER_LED); // Digital Pin

  /*Signalisation setup*/
  delay(100);
  LED.set_rgbw(0, color_off);
  LED.sync();
  noTone(SIGNALIZER_BUZZER);


  //-------- EEPROM --------
  
  //Get Whitelist 
  EEPROM.get(ADDRESS_WHITELIST, whitelist);

  for(uint8_t loop = 0; loop < WHITELIST_SIZE; loop++)
  {
    if(whitelist[loop] == 0xFFFFFFFF) whitelist[loop] = 0;
  }


  //get whitelistcount
  EEPROM.get(ADDRESS_WHITELISTCOUNT, whitelistMemberCount);
  if(registeredMaster == 0xFF) registeredMaster = 0;


  //Get Master
  EEPROM.get(ADDRESS_MASTER, registeredMaster);
  if(registeredMaster == 0xFFFFFFFF) registeredMaster = 0;

  if(registeredMaster == 0)
  {
    for(uint8_t loop = 0; loop < WHITELIST_SIZE; loop++)
    {
      whitelist[loop] = 0;
    }

    whitelist[WHITELIST_SIZE] = 0;
  }  
}

//==================== Loop ====================

void loop()
{
  //----------Local Declarations  
  edge_t RfidPresent = {0};
  timer_t time = {0};
  states_t state = noMaster;

  unsigned long wasPresent = 0;
  bool wasPresentMaster = 0;
  bool isMaster = 0;

  //keying
  uint8_t keyingPresentTime = 0;
  uint8_t keyingTimeout = 0;
  bool openkeying = 0;

  //flags
  bool keyingResetWhitelist = 0;
  bool keyingResetMaster = 0;
  
  //If Master registered, go to idle state
  if(registeredMaster != 0) state = idle;

  while(1)
  {
    //----------Loop Header

    // edge trigger setup
    RfidPresent.act = tagPresent();
    RfidPresent.edge = RfidPresent.act ^ RfidPresent.old;
    RfidPresent.edge_pos = RfidPresent.edge & RfidPresent.act;
    RfidPresent.edge_neg = RfidPresent.edge & RfidPresent.old;


    //Tag Information
    isMaster = checkMaster();
    if(RfidPresent.edge_pos) TagUID = getUID();
    
    if(isMaster) wasPresentMaster = 1;
    if(RfidPresent.edge_neg) wasPresent = TagUID;


    //keying variables
    if(state != keying)
    {
      keyingPresentTime = 0;
      keyingTimeout = 0;
      openkeying = 0;
    }

    for (byte i = 0; i < 6; i++)
      key.keyByte[i] = 0xFF;

    //----------Loop Main
    
    switch (state)
    {
//==================== noMaster

      case noMaster:
      //Don't register Master, if Reset was executed
      if(keyingResetMaster)
      {
        if(RfidPresent.edge_neg) keyingResetMaster = 0;
      }
      else
      {
        //Register Master if Master is presented
        if(RfidPresent.act && isMaster)
        {
          LED.set_rgbw(0, color_green);
          LED.sync();
          masterSet(TagUID);        
          
          openkeying = 1;
          state = keying;
        }
        break;
      }
        
//==================== Idle

      case idle:
        if(RfidPresent.edge_pos)
        {
          Serial.println(TagUID);
          if(isMaster)
          {
            //Go to keying state, if registered Master is presented
            if(TagUID == registeredMaster)
            {
              openkeying = 1;
              state = keying;
            }
            else
            {
              //Presented not registered Master
              SignalReject();
            }
          }
          //is User
          else
          {
            if(isWhitelistMember(TagUID))
            {
              //Access Granted
              digitalWrite(SIGNALIZER_OPENER, HIGH);
              SignalPositive();
              delay(OPEN_TIME * 1000);
              digitalWrite(SIGNALIZER_OPENER, LOW);
            }
            //Access Denied
            else if(TagUID != 0) SignalPermDenied();
          }
        }
        break;

//==================== Keying

      case keying:
      if(RfidPresent.edge_pos)
      {
        //Not registered Master presented
        if(isMaster && TagUID != registeredMaster)
        {
          SignalEndKeying();
          keyingTimeout = 0;
          keyingPresentTime = 0;
          state = idle;
        }
      }

        if(RfidPresent.act)
        {
          //Reset timeout
          keyingTimeout = 0;
          
          //Count present time
          if(time.pulse) keyingPresentTime++;

          //Light up signalization LED
          if(isMaster == 0 || (isMaster && TagUID == registeredMaster))
          {
            LED.set_rgbw(0, color_green);
            LED.sync();
          }

          //Remove if user is presented 5 seconds
          if(keyingPresentTime == 5 && isMaster == 0 && isWhitelistMember(TagUID))
          {
            Serial.println("Removed");
            SignalRemovedMember();
            whitelistRemove(TagUID);
          }

          if(isMaster)
          {
            //Master held longer than 10 seconds, reset Whitelist
            if(keyingPresentTime == 10 && keyingResetWhitelist == 0) 
            {
              keyingResetWhitelist = 1;

              delay(5);
              LED.set_rgbw(0, color_off);
              LED.sync();
              SignalResetWhitelist();
              
              whitelistReset();
            }
            //Master held longer than 15 seconds, reset Master + Whitelist
            if(keyingPresentTime == 13 && keyingResetMaster == 0)
            {
              keyingResetMaster = 1;
              SignalFullReset();
              whitelistReset();
              masterReset();

              state = noMaster;
            }
          }
        }
        else
        {
          //Timout handler
          if(time.pulse) 
          {
            keyingTimeout++;

            if(keyingTimeout >= 10)
            {
              keyingTimeout = 0;
              SignalEndKeying();
              state = idle;
            }
          }
        }


        //Card is not rpesented anymore
        if(RfidPresent.edge_neg && keyingResetMaster == 0)
        {
          isMaster = checkMaster();
          if(isMaster) wasPresentMaster = 1;
          keyingResetWhitelist = 0;
          keyingTimeout = 0;
          delay(100);

          LED.set_rgbw(0, color_off);
          LED.sync();

          if(wasPresentMaster)
          {
            if(keyingPresentTime < 10)
            {
              //Master was presented, opening keying process
              if(openkeying) SignalPositive();
              else
              {
                //Master presented, closing Keying process
                SignalEndKeying();
                state = idle;
              }
            }
            
          }
          else
          {
            if(keyingPresentTime >= 5) {}
            else
            {
              //Add User to Whitelist
              if(whitelistMemberCount < WHITELIST_SIZE)
              {
                SignalPositiveSound();
                whitelistAdd(wasPresent);
              }
              else 
              {
                //Whitelist is full, reject adding user to Whitelist
                SignalWhitelistFull();
              }
            }
          }
          
          keyingTimeout = 0;
          openkeying = 0;
          keyingPresentTime = 0;
          time.loopcounter = 0;
        }
      break;

      default:
        break;
    }

    //----------Loop Footer

    //Reset Values
    wasPresent = 0;
    RfidPresent.old = RfidPresent.act;
    if(RfidPresent.edge_neg) 
    {
      wasPresentMaster = 0;
      TagUID = 0;
    }

    //----------Timer Setup

    time.pulse = 0;

    if (time.loopcounter++ >= 16)
    {
      time.loopcounter = 0;
      time.pulse = 1;
    }

    delay(10);
  }
}


//==================== Signalisation Functions

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

void SignalRemovedMember()
{
  for (char i = 0; i < 3; i++)
  {
    tone(SIGNALIZER_BUZZER, 3000);
    LED.set_rgbw(0, color_off);
    LED.sync();
    delay(120);

    noTone(SIGNALIZER_BUZZER);
    delay(120);
  }
}

void SignalWhitelistFull()
{
  for (char i = 0; i < 8; i++)
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


void SignalEndKeying()
{
  tone(SIGNALIZER_BUZZER, 3000);
  delay(700);
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


//==================== RFID Functions ====================

//Checks if Tag is Master
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

//Checks if Tag is present
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

//Returns Tag UID
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

//==================== Whitelist Functions ====================

//Removes User from Whitelist
void whitelistRemove(unsigned long UID)
{
  for (unsigned char searchLoop = 0; searchLoop < WHITELIST_SIZE; searchLoop++)
  {
    if (whitelist[searchLoop] == UID)
    {
      // Deletes User
      whitelist[searchLoop] == 0;

      // Moves back the "NULL" value
      for (int moveLoop = searchLoop; moveLoop < WHITELIST_SIZE; moveLoop++)
        whitelist[moveLoop] = whitelist[moveLoop + 1];
      
      whitelist[WHITELIST_SIZE - 1] = 0x00000000;

      EEPROM.put(ADDRESS_WHITELIST, whitelist);

      whitelistMemberCount--;
      EEPROM.put(ADDRESS_WHITELISTCOUNT, whitelistMemberCount);
      break;
    }
  }
  return;
}

//Adds User to Whitelist
void whitelistAdd(unsigned long UID)
{
  if(UID == 0) return;

  for (unsigned char searchLoop = 0; searchLoop < WHITELIST_SIZE; searchLoop++)
  {
    if (whitelist[searchLoop] == UID)
    {
      return;
    }
  }

  for (unsigned char nextNull = 0; nextNull < WHITELIST_SIZE; nextNull++)
  {
    if (whitelist[nextNull] == 0 || whitelist[nextNull] == NULL)
    {
      whitelist[nextNull] = UID;
      EEPROM.put(ADDRESS_WHITELIST, whitelist);

      whitelistMemberCount++;
      EEPROM.put(ADDRESS_WHITELISTCOUNT, whitelistMemberCount);
      return;
    }
  }

  // Signal list full reject
  return;
}

//Deletes all Users from Whietlist
void whitelistReset()
{
  for (unsigned char deleteLoop = 0; deleteLoop < WHITELIST_SIZE; deleteLoop++)
  {
    whitelist[deleteLoop] = 0;
  }

  whitelist[WHITELIST_SIZE] = 0;

  EEPROM.put(ADDRESS_WHITELIST, whitelist);

  whitelistMemberCount = 0;
  EEPROM.put(ADDRESS_WHITELISTCOUNT, whitelistMemberCount);
  return;
}

//Checks if UID is contained in Whitelist
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

//==================== Master Functions ====================

//Sets the Master Tag
void masterSet(unsigned long UID)
{
  registeredMaster = UID;
  EEPROM.put(ADDRESS_MASTER, registeredMaster);
}

//Resets the Master Tag
void masterReset()
{
  registeredMaster = 0;
  EEPROM.put(ADDRESS_MASTER, registeredMaster);
}
