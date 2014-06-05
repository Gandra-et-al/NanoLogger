 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////*** NANOLOGGER SCRIPT ***///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 // Miguel Gandra // m3gandra@gmail.com // May 2014 /////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  /*
  This code is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but without any warranty; without even the implied warranty of
  merchantability or fitness for a particular purpose.  See the
  GNU General Public License for more details.
  */
  
  
  
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// SUMMARY ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  // Ability to set up mission parameters via a text file in the MicroSD card
  // Outputs reader-friendly, plain text logs in CSV format (comma separated values)
  // Automatic Real Time Clock sync
  // Error function with visual feedback using the red LED
  // Improved autonomy by using sleep functions
  // Arduino ID stored on EEPROM
  // Two modes of operation start:
    // Mode1 - the logger starts immediately if the pushbutton is clicked (pressed for less than 2 seconds)
    // Mode2 - if the pushbutton is pressed for more than 2 seconds, data collection starts at the preselected StartTime



 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// LIBRARIES /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  // for instructions on how to install libraries check http://arduino.cc/en/Guide/Libraries
  
  #include <EEPROM.h>
  #include <Wire.h>
  #include <RTClib.h>           //https://github.com/adafruit/RTClib
  #include <SdFat.h>            //https://code.google.com/p/sdfatlib/
  #include <LowPower.h>         //https://github.com/rocketscream/Low-Power
  #include <FlexiTimer2.h>      //https://github.com/wimleers/flexitimer2
  #include <NilFIFO.h>          //https://code.google.com/p/rtoslibs/
  
  
  
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// UNDERCLOCKING /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  // Clock pre-scaling to save power 
  
  #define Set_prescaler(x)   (CLKPR = (1<<CLKPCE), CLKPR = x) 
  const byte Prescaler = 0;
 
  // Prescaler value and its corresponding Arduino clock speed:
  // Prescaler = 0 ---> 16MHz (default)
  // Prescaler = 1 ---> 8MHz 
  // Prescaler = 2 ---> 4MHz
  // Prescaler = 3 ---> 2MHz
  // Prescaler = 4 ---> 1MHz
  
  
  
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// SAMPLING VARIABLES ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // parameters that will be fetched from the settings file
    unsigned long Rate;                               // in Hz - samples/second
    unsigned long LogDuration;                        // in seconds
    unsigned int  SleepDuration;                      // in seconds                          
    int FileThreshold;                                // generate a new file after x sampling sessions
    
    // parameters that must be set via this sketch
    const byte NADC = 6;                              // number of ADC channels to log (1 to 6); lower numbers will increase the maximum sampling rate attainable; can be left as 6 if rates above 100 Hz are not required
    const char ArduinoID[7] = "GOLIAS";               // default ID of the Arduino
    
      
      
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// OTHER VARIABLES ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////   
 
    //// FIFO Buffer //////////////////////////////////////////////////////////////////////
    const size_t FifoSize = 600;                      // FIFO buffer size in bytes
    struct Data  {unsigned int adc[NADC];};           // type for data record 
    const size_t NSamples = FifoSize / sizeof(Data);  // number of data records in the FIFO
    NilFIFO <Data, NSamples> DataBuffer;              // declare FIFO buffer
      
    //// Pins /////////////////////////////////////////////////////////////////////////////
    const byte redLED     = 4;                        // red LED pin
    const byte greenLED   = 5;                        // green LED pin
    const byte onButton   = 6;                        // pushbutton
  
    //// SD File ///////////////////////////////////////////////////////////////////////////
    SdFat sd;                                         // SD file system
    SdFile logfile;                                   // logging file instance
    char filename[9]  = {"000.CSV"};                  // default filename, will be changed sequentially as data is collected
    byte ReadsCounter = 0;
    
    //// Time //////////////////////////////////////////////////////////////////////////////
    RTC_DS1307 RTC;                                   // initialize the DS1307/DS3232 hardware RTC    
    unsigned long NextLogTime;                        // stores the next log time in unix time format
    unsigned long EndTimeUnix;                        // stores the converted ending time in unix time format
    long TimeNowUnix;                                 // stores the current time in unix time format  
    long StartTimeUnix;                               // converts StartTime to unix format (seconds since 1970-01-01)
    
    //// Button ////////////////////////////////////////////////////////////////////////////
    boolean ButtonPress = false;                      // the button has been pressed? (i.e., more than 2 seconds)
    boolean ButtonClick = false;                      // the button has been clicked?
    unsigned int PressLimit = 1750;                   // in milliseconds - pressing time required until ButtonPress mode is detected
       
    //// Samples ///////////////////////////////////////////////////////////////////////////  
    volatile unsigned long Samples = 0;               // samples counter (increased in the ISR)
   
   
    
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// FUNCTIONS /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    //// ERROR FUNCTION /////////////////////////////////////////////////////////////////////////////////
    //// called when an error is detected, turns the redLED on and prints an error message ////////////// 
    void Error(char *str) {
        
           MiniSerial.print(F("Error: "));
           MiniSerial.println(str);
           digitalWrite(redLED, HIGH);   
           while(1);
    }
     
          
    //// TIME UPDATE FUNCTION /////////////////////////////////////////////////////////////////////////////
    //// update the RTC whenever the sketch is uploaded ///////////////////////////////////////////////////
    void TimeUpdate() {
    
          // get date and time from the computer
          DateTime Compiled = DateTime(__DATE__, __TIME__);
          
          // read last date/time stored in the EEPROM
          byte OC_Second = EEPROM.read(10);
          byte OC_Minute = EEPROM.read(11);
          byte OC_Hour   = EEPROM.read(12);
          byte OC_Day    = EEPROM.read(13);
          byte OC_Month  = EEPROM.read(14);
          int OC_Year    = (EEPROM.read(15) + 2000);
            
          DateTime OldCompiled (OC_Year, OC_Month, OC_Day, OC_Hour, OC_Minute, OC_Second);
         
          // if they match the Arduino didn´t reset after a upload, and so the RTC can´t be updated
          if (Compiled.unixtime() == OldCompiled.unixtime()) {
               return;
          } else {
               
               // update the RTC
               RTC.adjust(DateTime(__DATE__, __TIME__)); 
                
               MiniSerial.println(F("Updating RTC..."));
               
               MiniSerial.print(Compiled.year());
               MiniSerial.print('-');
               MiniSerial.print(Compiled.month());
               MiniSerial.print('-');
               MiniSerial.print(Compiled.day());
               MiniSerial.print(' ');
               MiniSerial.print(Compiled.hour());
               MiniSerial.print(':');
               MiniSerial.print(Compiled.minute());
               MiniSerial.print(':');
               MiniSerial.println(Compiled.second());
               
               digitalWrite(greenLED, HIGH);   
               delay(100);
               digitalWrite(greenLED, LOW); 
               
               // save the last compiled date/time to the EEPROM
               EEPROM.write(10, Compiled.second());
               EEPROM.write(11, Compiled.minute());
               EEPROM.write(12, Compiled.hour());
               EEPROM.write(13, Compiled.day());
               EEPROM.write(14, Compiled.month());
               EEPROM.write(15, Compiled.year()-2000);  
         }
    }
       
    
    //// CHECK ID FUNCTION ///////////////////////////////////////////////////////////////////////////
    //// reads/writes Arduino ID from/to EEPROM //////////////////////////////////////////////////////
    void CheckID() {
       
          char readID[7];           
          for (int i=0; i<7; i++) {readID[i] = EEPROM.read(i);}
          
          // if ArduinoID and readID do not match, write ArduinoID
          // this is done to avoid writing to the EEPROM too much, as this memory type has limited write cycles
          if (strcmp(readID,ArduinoID) != 0) {
                  for (int i=0; i<7; i++) {EEPROM.write(i, ArduinoID[i]);}
                  MiniSerial.print("ArduinoID = ");
                  MiniSerial.println(ArduinoID);
          }
   }
                  
    
   //// BUTTON CHECK FUNCTION /////////////////////////////////////////////////////////////////////////  
   //// reads pushbutton values and recognizes the two different pushes - single click or long-time press
   void ButtonCheck() {
    
          int count = 0;  
          boolean hold = 0;
          while (digitalRead(onButton) == HIGH && hold == 0) {
                  delay(10);
                  count = count + 10;
                  if (count >= PressLimit) {ButtonPress = true; hold = 1;}  
          }
          if (count > 0 && hold == 0) {ButtonClick = true;}
    }
                   
     
    //// DATE_TIME FUNCTION //////////////////////////////////////////////////////////////////////////
    //// returns date and time using FAT_DATE macro to format fields /////////////////////////////////
    void Date_Time (uint16_t* date, uint16_t* time) {
           
           DateTime now = RTC.now();
           *date = FAT_DATE(now.year(), now.month(), now.day());      
           *time = FAT_TIME(now.hour(), now.minute(), now.second()); 
     }
     
          
    //// GET SETTINGS FUNCTION //////////////////////////////////////////////////////////////////////////
    //// reads SD configuration text file and sets mission parameters accordingly ///////////////////////
    void GetSettings() {
      
           // open the settings file
           SdFile SettingsFile ("settings.txt", O_READ);
           if (!SettingsFile.isOpen()) {Error("Settings file not available");}
             
           int  ST_Year;
           byte ST_Month;
           byte ST_Day;
           byte ST_Hour;
           byte ST_Minute;
            
           for (byte i=0; i<=5; i++)  {        
                   char line[24];
                   SettingsFile.fgets(line, 24);     // read one line
                   char* p_pos = strchr(line, '=');  // find the '=' position
                   
                   if(i==0)  Rate = atoi(p_pos+1);
                   if(i==1)  LogDuration = atoi(p_pos+1);
                   if(i==2)  SleepDuration = atoi(p_pos+1);
                   if(i==3)  {ST_Year = atoi(p_pos+1); ST_Month = atoi(p_pos+7); ST_Day = atoi(p_pos+10);}
                   if(i==4)  {ST_Hour = atoi(p_pos+1); ST_Minute = atoi(p_pos+5);}
                   if(i==5)  FileThreshold = atoi(p_pos+1);
           }
           
           SettingsFile.close();
           DateTime StartTime (ST_Year, ST_Month, ST_Day, ST_Hour, ST_Minute);
           StartTimeUnix = StartTime.unixtime();
           
           MiniSerial.print(F("Rate = "));
           MiniSerial.println(Rate);
           MiniSerial.print(F("LogDuration = "));
           MiniSerial.println(LogDuration);
           MiniSerial.print(F("SleepDuration = "));
           MiniSerial.println(SleepDuration);
           
           MiniSerial.print(F("Start Time = "));
           MiniSerial.print(StartTime.year(), DEC);
           MiniSerial.print('-');
           MiniSerial.print(StartTime.month(), DEC);
           MiniSerial.print('-');
           MiniSerial.print(StartTime.day(), DEC);
           MiniSerial.print(' ');
           MiniSerial.print(StartTime.hour(), DEC);
           MiniSerial.print(':');
           MiniSerial.print(StartTime.minute(), DEC);
           MiniSerial.println();
           
           MiniSerial.print(F("FileThreshold = "));
           MiniSerial.println(FileThreshold);
           
           if (Rate == 0 || LogDuration == 0 || SleepDuration == 0) {Error("Error in settings file variables");}
                 
    }
      
         
   //// NEW LOG FILE FUNCTION /////////////////////////////////////////////////////////////////////////
   //// generates a new file and prints the header ////////////////////////////////////////////////////
   void NewLogFile() {
            
           if (ReadsCounter != 0 && ReadsCounter < FileThreshold)  {return;}
           
           ReadsCounter = 0; // reset ReadsCounter
           
           // get time
           DateTime FileTime = RTC.now();
          
           for (int i=0; i<1000; i++) {
          
               filename[0] = i/100 + '0';
               filename[1] = ((i/10) % 10) + '0';
               filename[2] = i%10 + '0';    
              
               // only open a new file if the filename generated doesn't exist
               if (! sd.exists(filename)) {
                     logfile.open (filename, O_CREAT | O_WRITE);
                     break;
               } 
           }
          
           MiniSerial.print(F("New File Created: "));
           MiniSerial.println(filename);
                  
           // print header
           logfile.println(ArduinoID);
           logfile.println(F(__FILE__));
                     
           // print time (that can later be used as reference to rebuild logged timestamps)
           logfile.print(FileTime.year());
           logfile.print('-');
           if (FileTime.month() < 10)  {logfile.print('0');}
           logfile.print(FileTime.month());
           logfile.print('-');
           if (FileTime.day() < 10)  {logfile.print('0');}
           logfile.print(FileTime.day());
           
           logfile.print(" ");

           if (FileTime.hour() < 10)  {logfile.print('0');}
           logfile.print(FileTime.hour());
           logfile.print(':');
           if (FileTime.minute() < 10)  {logfile.print('0');}
           logfile.print(FileTime.minute());
           logfile.print(':');
           if (FileTime.second() < 10)  {logfile.print('0');}
           logfile.println(FileTime.second());
          
             
           // other mission parameters
           logfile.print(F("Rate (Hz): "));
           logfile.println(Rate);
           logfile.print(F("LogDuration (s): "));
           logfile.println(LogDuration);
           logfile.print(F("SleepDuration (s): "));
           logfile.println(SleepDuration);
           logfile.println();
           
     
           for (int i=0; i < NADC; i++)  {

                    logfile.print(F("A"));
                    if (i < 4)  {
                        logfile.print(i);
                    } else {
                        logfile.print(i+2);  // A4 and A5 are used by the RTC!
                    }
                    
                    if (i < NADC - 1) {logfile.print(',');}  // separate channels with ','             
            }
            
           logfile.println();
           logfile.println();
           logfile.close();           
   }
     
     
   //// READ FUNCTION ///////////////////////////////////////////////////////////////////////
   //// reads data in the ISR (Interrupt Service Routine) ///////////////////////////////////
   void Read() {
       
         digitalWrite(greenLED, HIGH);
         Data* p = DataBuffer.waitFree(TIME_IMMEDIATE);
         if (!p) {Error("Overrun");}
             
         for (int i=0; i < NADC; i++)  {
                 if (i < 4)  {
                        p -> adc[i] = analogRead(i);
                 } else {
                        p -> adc[i] = analogRead(i+2); // A4 and A5 are used by the RTC!
                 }
          }
         
         DataBuffer.signalData();      
         Samples++;
         digitalWrite(greenLED, LOW);
   }
      
        
   //// LOG FUNCTION /////////////////////////////////////////////////////////////////////////
   //// saves data to the microSD card ///////////////////////////////////////////////////////
   void Log() {
         
         logfile.open (filename, O_APPEND | O_WRITE); 
         Samples=0; 
         FlexiTimer2::start();
         
         while (Samples < (Rate*LogDuration)) {
           
                 Data* p = DataBuffer.waitData(TIME_IMMEDIATE);
                 if (!p) {continue;}
                 for (int i=0; i < NADC; i++)  {
                       if(i < NADC - 1) {
                             logfile.printField(p->adc[i], ',');
                       } else {
                             logfile.printField(p->adc[i], '\n');
                       }
                 }                                                  
                 
                 DataBuffer.signalFree();         
         }
 
         FlexiTimer2::stop();
         DateTime now = RTC.now();
         EndTimeUnix = now.unixtime();
                
         while (DataBuffer.dataCount() > 0) {
           
                 Data* p = DataBuffer.waitData(TIME_IMMEDIATE);
                 for (int i=0; i < NADC; i++)  {
                       if(i < NADC - 1) {
                             logfile.printField(p->adc[i], ',');
                       } else {
                             logfile.printField(p->adc[i],'\n');
                       }
                 }
        
                 DataBuffer.signalFree();
         }
         
         logfile.println();   
         digitalWrite(redLED, HIGH);
         logfile.close(); 
         digitalWrite(redLED, LOW);
         
         ReadsCounter++;   
         MiniSerial.println(F("End!"));
         MiniSerial.println();    
         delay(10);                
   }
            
  
   //// SLEEP FUNCTION 1 /////////////////////////////////////////////////////////////////////
   //// manages sleep before the start time //////////////////////////////////////////////////
   void Sleep1() {
                 
         DateTime now = RTC.now();
         TimeNowUnix = now.unixtime();
         unsigned long TimeLeft = StartTimeUnix - TimeNowUnix;    
         if (TimeLeft <= 0)  {return;}
         MiniSerial.println(F("Sleeping..."));
   
         for (int i=0; i<(TimeLeft-2); i++)  {LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);}
         
         analogRead(0);    
         NewLogFile();
         while(TimeNowUnix < StartTimeUnix) {DateTime now = RTC.now(); TimeNowUnix = now.unixtime();}          
   }
      
     
   //// SLEEP FUNCTION 2 ////////////////////////////////////////////////////////////////////////////////////////////
   //// manages sleep between read cycles ///////////////////////////////////////////////////////////////////////////
   void Sleep2() {
      
          NextLogTime = EndTimeUnix + SleepDuration;
          MiniSerial.println(F("Sleeping..."));
          
          for (int i=0; i<SleepDuration; i++) {
                
                DateTime now = RTC.now();
                TimeNowUnix = now.unixtime();
                if(TimeNowUnix >= (NextLogTime-2))  {break;}
                LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
          }
              
          analogRead(0);   
          NewLogFile();
          while(TimeNowUnix < NextLogTime) {DateTime now = RTC.now(); TimeNowUnix = now.unixtime();}       
 }
  
   
           
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// VOID SETUP ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// 
 void setup() {       
  
     // adjust clock speed to save power
     Set_prescaler(Prescaler);   
  
     // start serial comunication   
     MiniSerial.begin(9600);
     MiniSerial.println();
      
     // initialize the button pin as an input
     pinMode(onButton, INPUT);
   
     // initialize the LED pins as outputs
     pinMode(redLED,   OUTPUT);
     pinMode(greenLED, OUTPUT);
     
     // connect and initialize the RTC
     Wire.begin();
     RTC.begin();
     if (!RTC.begin())  {Error("RTC failed");}
     else {MiniSerial.println(F("RTC initialized!"));}
     
     // update the RTC
     TimeUpdate();
      
     // make sure that the default MicroSD chip select pin (pin 10) is set to output, even if the MicroSD card is not being used
     pinMode(10, OUTPUT);	
		
     // check if the card is present and can be initialized:
     if (!sd.begin(10))  {Error("Card failed, or not present");}
     else {MiniSerial.println(F("Card initialized!")); }
     
     // callback for file timestamps
     SdFile::dateTimeCallback(Date_Time);
     
     
     // set unused pins to low to reduce power consumption
     pinMode(0, OUTPUT);
     digitalWrite(0, LOW); 
     pinMode(1, OUTPUT);
     digitalWrite(1, LOW); 
     pinMode(2, OUTPUT);
     digitalWrite(2, LOW);  
     pinMode(3, OUTPUT);
     digitalWrite(3, LOW); 
     pinMode(7, OUTPUT);
     digitalWrite(7, LOW);    
     pinMode(8, OUTPUT);
     digitalWrite(8, LOW);    
     pinMode(9, OUTPUT);
     digitalWrite(9, LOW);    
     
     // set voltage reference
     analogReference(EXTERNAL);
     
     // check Arduino ID
     CheckID();
    
     // check number of channels
     if (NADC == 0 || NADC > 6) {Error("Incorrect number of channels, check NADC");}

     // read settings file
     GetSettings();
     
     
     // attach the Interrupt     
     if(Rate > 1000)  {FlexiTimer2::set(1, 1.0/Rate, Read);}
     else  {FlexiTimer2::set(1000/Rate, Read);}
      
 }
 
 
          
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// VOID LOOP /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 void loop() {
    
      ButtonCheck();
      
      /////////////////////////////////////////////////////////////////////////////////////////////////////
      //// MODE 1 - Start logging immediately /////////////////////////////////////////////////////////////
  
      if (ButtonClick)  {
          
           MiniSerial.print(F("ButtonClick Mode"));
           MiniSerial.println();
           
           // Blink LEDs
           delay(500);
           digitalWrite(greenLED, HIGH);
           digitalWrite(redLED, HIGH);   
           delay(1000);
           digitalWrite(greenLED, LOW);
           digitalWrite(redLED, LOW); 
           
           NewLogFile();
          
        }
    
      while (ButtonClick)  {Log(); Sleep2();}
    
             
       
      ///////////////////////////////////////////////////////////////////////////////////////////////////// 
      //// MODE 2 - Start logging when the predefined StartTime is reached ////////////////////////////////
       
      if (ButtonPress) {
            
           MiniSerial.print(F("ButtonPress Mode"));
           MiniSerial.println(); 
           
           // Blink LED
           delay(500);
           digitalWrite(greenLED, HIGH);   
           delay(500);
           digitalWrite(greenLED, LOW); 
           delay(500);
           digitalWrite(greenLED, HIGH);   
           delay(500);
           digitalWrite(greenLED, LOW);
           delay(500);
           digitalWrite(greenLED, HIGH); 
           delay(500);
           digitalWrite(greenLED, LOW);
           
           Sleep1(); 
          
       }
                 
      while (ButtonPress)  {Log(); Sleep2();}        
 }

  
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// end
