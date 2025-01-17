 
   /*bricbric  update 14/12/2022
    * 
    * if section 1 or 2 or 3  activ   gps speed  is activ   if  not   no gps  signal
    * 
    * 
    * 
    * This program only turns the relays for section control
    * On and Off. Connect to the Relay Port in AgOpenGPS
    * 
    * Hydraulic Raise D4
    * Hydraulic Lower D3
    * 
    * Tram Right Side D5
    * Tram left Side D6
    * 
    * Section 0 to 5 -- D7 to D12
    * 
    */
    
  //loop time variables in microseconds

  #include <EEPROM.h> 
  #define EEP_Ident 0x5005  

    //Program counter reset
    void(* resetFunc) (void) = 0;

  //Variables for config - 0 is false  
  struct Config {
  uint8_t raiseTime = 2;
  uint8_t lowerTime = 4;
  uint8_t enableToolLift = 0;
  uint8_t isRelayActiveHigh = 0; //if zero, active low (default)
  
  };  Config aogConfig;   //4 bytes
  
  const uint8_t LOOP_TIME = 200; //5hz
  uint32_t lastTime = LOOP_TIME;
  uint32_t currentTime = LOOP_TIME;
  uint32_t fifthTime = 0;
  uint16_t count = 0;

  //Comm checks
  uint8_t watchdogTimer = 0; //make sure we are talking to AOG
  uint8_t serialResetTimer = 0; //if serial buffer is getting full, empty it

  bool isRaise = false;
  bool isLower = false;
  
   //Communication with AgOpenGPS
  int16_t temp, EEread = 0;

   //Parsing PGN
  bool isPGNFound = false, isHeaderFound = false;
  uint8_t pgn = 0, dataLength = 0, idx = 0;
  int16_t tempHeader = 0;
  
  uint8_t AOG[] = {0x80,0x81, 0x7f, 0xED, 8, 0, 0, 0, 0, 0,0,0,0, 0xCC };

  //The variables used for storage
  uint8_t relayHi=0, relayLo = 0, tramline = 0, uTurn = 0, hydLift = 0; 
  float gpsSpeed, hertz = 0;
  bool semis = false;
  
  uint8_t raiseTimer = 0, lowerTimer = 0, lastTrigger = 0;  
  #define tram1  A3
  #define tram2  A4
  #define sec1  13
  #define sec2  A1
  #define sec3  A2
  #define sec4  A5
  #define sec5  A6
  #define vit  9

  #include <NewTone.h>

  //Pins and variable for speed:
#define PinOutputImpuls 9
#define PULSE_BY_100M 10019
#define PULSE_ERROR -0.13


void setup()
  {
    //set the baud rate
     Serial.begin(38400);  
     while (!Serial) { ; } // wait for serial port to connect. Needed for native USB
     
     EEPROM.get(0, EEread);              // read identifier
    
  if (EEread != EEP_Ident)   // check on first start and write EEPROM
  {           
    EEPROM.put(0, EEP_Ident);
    EEPROM.put(6, aogConfig);
  }
  else 
  { 
    EEPROM.get(6, aogConfig);
  }

  //set the pins to be outputs (pin numbers)
      pinMode(tram1, OUTPUT);
      pinMode(tram2, OUTPUT);

      pinMode(sec1, OUTPUT);
      pinMode(sec2, OUTPUT);
      pinMode(sec3, OUTPUT);
      pinMode(sec4, OUTPUT);
      pinMode(sec5, OUTPUT);

      
      pinMode(vit, OUTPUT);
}

void loop()
{
  //Loop triggers every 200 msec and sends back gyro heading, and roll, steer angle etc

  currentTime = millis();

  if (currentTime - lastTime >= LOOP_TIME)
  {
    lastTime = currentTime;

    //If connection lost to AgOpenGPS, the watchdog will count up 
    if (watchdogTimer++ > 250) watchdogTimer = 12;

    //clean out serial buffer to prevent buffer overflow
    if (serialResetTimer++ > 20)
    {
      while (Serial.available() > 0) Serial.read();
      serialResetTimer = 0;
    }

    if (watchdogTimer > 12)
    {
      if (aogConfig.isRelayActiveHigh) {
          relayLo = 255;
          relayHi = 255;
        } else {
          relayLo = 0;
          relayHi = 0;        
        }          
    }
    
    //hydraulic lift

    if (hydLift != lastTrigger && (hydLift == 1 || hydLift == 2))
    {
      lastTrigger = hydLift;
      lowerTimer = 0;
      raiseTimer = 0;

      //200 msec per frame so 5 per second
      switch (hydLift)
      {
        //lower
        case 1:
          lowerTimer = aogConfig.lowerTime * 5;
        break;

        //raise
        case 2:
          raiseTimer = aogConfig.raiseTime * 5;     
        break;
      }
    }

    //countdown if not zero, make sure up only
    if (raiseTimer) 
    {
      raiseTimer--;
      lowerTimer = 0;
    }
    if (lowerTimer) lowerTimer--; 

    //if anything wrong, shut off hydraulics, reset last
    if ((hydLift != 1 && hydLift != 2) || watchdogTimer > 10 ) //|| gpsSpeed < 2)
    {
      lowerTimer = 0;
      raiseTimer = 0;
      lastTrigger = 0;
    }

    if (aogConfig.isRelayActiveHigh)
    {
      isLower = isRaise = false;
      if (lowerTimer) isLower = true;
      if (raiseTimer) isRaise = true;
    }
    else
    {
      isLower = isRaise = true;
      if (lowerTimer) isLower = false;
      if (raiseTimer) isRaise = false;        
    }

    //section relays
    SetRelays();

    AOG[5] = aogConfig.isRelayActiveHigh;
    AOG[6] = (uint8_t)relayLo;
    AOG[7] = (uint8_t)tramline;
    
    
    //add the checksum
    int16_t CK_A = 0;
    for (uint8_t i = 2; i < sizeof(AOG)-1; i++)
    {
      CK_A = (CK_A + AOG[i]);
    }
    AOG[sizeof(AOG)-1] = CK_A;
    
    Serial.write(AOG,sizeof(AOG));        
    Serial.flush();   // flush out buffer
      
  } //end of timed loop

  // Serial Receive
  //Do we have a match with 0x8081?    
  if (Serial.available() > 4 && !isHeaderFound && !isPGNFound) 
  {
    uint8_t temp = Serial.read();
    if (tempHeader == 0x80 && temp == 0x81) 
    {
      isHeaderFound = true;
      tempHeader = 0;        
    }
    else  
    {
      tempHeader = temp;     //save for next time
      return;    
    }
  }

  //Find Source, PGN, and Length
  if (Serial.available() > 2 && isHeaderFound && !isPGNFound)
  {
    Serial.read(); //The 7F or less
    pgn = Serial.read();
    dataLength = Serial.read();
    isPGNFound = true;
    idx=0;
  } 

  //The data package
  if (Serial.available() > dataLength && isHeaderFound && isPGNFound)
  {
    if (pgn == 239) // EF Machine Data
    {
      uTurn = Serial.read();  
      gpsSpeed = (float)Serial.read();
      
      hydLift = Serial.read();
      tramline = Serial.read();  //actual speed times 4, single uint8_t
      
      //just get the rest of bytes
      Serial.read();   //high,low bytes   
      Serial.read();  
      
      relayLo = Serial.read();          // read relay control from AgOpenGPS
      relayHi = Serial.read();

      if (aogConfig.isRelayActiveHigh)
      {
        tramline = 255 - tramline;
        relayLo = 255 - relayLo;
      }
      
      //Bit 13 CRC
      Serial.read();
      
      //reset watchdog
      watchdogTimer = 0;
  
      //Reset serial Watchdog  
      serialResetTimer = 0;        
      
      //reset for next pgn sentence
      isHeaderFound = isPGNFound = false;
      pgn=dataLength=0;                  
    }
            
    else if (pgn==238) //EE Machine Settings 
    {         
      aogConfig.raiseTime = Serial.read();
      aogConfig.lowerTime = Serial.read();    
      aogConfig.enableToolLift = Serial.read();
      
      //set1 
      uint8_t sett = Serial.read();  //setting0     
      if (bitRead(sett,0)) aogConfig.isRelayActiveHigh = 1; else aogConfig.isRelayActiveHigh = 0;
      
      Serial.read();
      Serial.read();
      Serial.read();
      Serial.read();    

      //crc
      //udpData[13];        //crc
      Serial.read();
  
      //save in EEPROM and restart
      EEPROM.put(6, aogConfig);
      resetFunc();

      //reset for next pgn sentence
      isHeaderFound = isPGNFound = false;
      pgn=dataLength=0;
    }
    else //nothing found, clean up
    {
      isHeaderFound = isPGNFound = false;
      pgn=dataLength=0;   
    }
  }
}

 void SetRelays(void)
 { 
    //pin, rate, duration  130 pp meter, 3.6 kmh = 1 m/sec or gpsSpeed * 130/3.6 or gpsSpeed * 36.1111
    //gpsSpeed is 10x actual speed so 3.61111
 //   gpsSpeed *= 3.61111;
 //   gpsSpeed = gpsSpeed *10;
 //   tone(vit,gpsSpeed);
       hertz = gpsSpeed + PULSE_ERROR ;
       hertz =  hertz * PULSE_BY_100M;
       hertz = hertz / 3600;
      //hertz = ((gpsSpeed + PULSE_ERROR*10)  * PULSE_BY_100M / 60 / 60) ; //(= pulse/H) / min / s = Hertz
 
      semis  = false;
       if (!bitRead(relayLo,0) ) semis =  true ;
       if (!bitRead(relayLo,1) ) semis =  true ;
       if (!bitRead(relayLo,2) ) semis =  true ;
    
      if (gpsSpeed and (semis == true) ) NewTone(PinOutputImpuls, hertz);
      else noNewTone();

    
    //change the pin number as required (pinD#, bitRead....)               
    digitalWrite (sec1, bitRead(relayLo,0)); //section 1 thru 8
    digitalWrite (sec2, bitRead(relayLo,1));
    digitalWrite (sec3, bitRead(relayLo,2));
    digitalWrite (sec4, bitRead(relayLo,3));
    digitalWrite (sec5, bitRead(relayLo,4));

    //digitalWrite (10, isLower); //hydraulic control D10, D11
    //digitalWrite (11, isRaise);
    
    digitalWrite (tram1, bitRead(tramline,0)); //left and right tram
    digitalWrite (tram2, bitRead(tramline,2)); //
 }
