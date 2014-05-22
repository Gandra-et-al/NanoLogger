 
 
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////*** NANO LOGGER CODE ***////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 // Miguel Gandra // m3gandra@gmail.com /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
 
  // To interface the Logger with the Thermocouple Amplifier MAX31855 breakout board from Adafruit
  
  //Mode1 - if the button is clicked, it starts immediately
  //Mode2 - if the button is pushed the logger only begins collecting data in the chosen StartTime
 
  //Definable sampling rate, duration, frequency and start time
  //Ability to set up the logger via parameters saved on a text file in the MicroSD card
  //Outputs reader-friendly, plain text logs (CSV format, i.e. comma separated values)
  //Automatic time sync
  //Error function with visual feedback through the red LED
  //Improved autonomy achieved using sleep functions
  //Arduino ID stored on EEPROM
 
 

 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// LIBRARIES /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  #include <EEPROM.h>
  #include <Wire.h>
  #include <RTClib.h>
  #include <SdFat.h>
  #include <LowPower.h>
  #include <FlexiTimer2.h>
  #include <NilFIFO.h>
  
  #include <Adafruit_MAX31855.h>
  
  
 //Clock pre-scaling to save power
  #define Set_prescaler(x)   (CLKPR = (1<<CLKPCE),CLKPR = x) 
  const byte Prescaler = 2; 
  //Prescaler=0 ---> 16MHz (default)
  //Prescaler=1 ----> 8MHz 
  //Prescaler=2 ---> 4MHz
  //Prescaler=3 ---> 2MHz
  //Prescaler=4 ---> 1MHz
  
  
  
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// GLOBAL VARIABLES //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


    //// DATA COLLECTING VARIABLES  /////////////////////////////////////////////////////
    
    unsigned int Rate = 1;               // in Hz - samples/second
    unsigned int Duration = 1;           // in seconds - time each sensor is read
    unsigned int ReadInterval = 1;       // in seconds - time between reading sessions
                                         //  3h = 10800, 1h = 3600, 30 min = 1800, 15 min = 900
                                     
                                   
    DateTime StartTime (2014,5,20,16,30);        //(year,month,day,hour,min) - when data will start to be collected in Mode 2
    long StartTimeUnix = StartTime.unixtime();   //convert StartTime to unix format (seconds since 01/01/1970)
    
    int FileThreshold = 10000;                     //create a new file after x sampling events
    
    const char ArduinoID[7] = "GOLIAS";          //ID of the Arduino
    
      
    
    //// FIFO BUFFER VARIABLES  /////////////////////////////////////////////////////////
    
    const size_t FifoSize = 550;                      // FIFO buffer size in bytes
    struct Data  {DateTime logtime; double temperature;};
    const size_t NSamples = FifoSize / sizeof(Data);  // number of data records in the FIFO
    NilFIFO <Data, NSamples> DataBuffer;              // declare FIFO buffer
      
 
 
    //// PINS ///////////////////////////////////////////////////////////////////////////
 
    //microSD CS Pin
    const byte chipSelect = 10;     
  
    //digital Pins that connect to logging LEDs
    const byte redLED = 4;
    const byte greenLED = 5;
  
    //pushbutton
    const byte onButton = 6;
   
    //thermocouple
    int thermoDO = 9;
    int thermoCS = 8;
    int thermoCLK = 3; 
    
    Adafruit_MAX31855 thermocouple(thermoCLK, thermoCS, thermoDO);
   
   
    //// FILE VARIABLES /////////////////////////////////////////////////////////////////
    
    // SD file system
    SdFat sd;
    //logging file
    SdFile logfile;
    //create a new filename
    char filename[9] = {"000.CSV"};
  
    
    
    //// TIME VARIABLES /////////////////////////////////////////////////////////////////
    
    RTC_DS1307 RTC;                  //the DS1307/DS3232 hardware RTC    
    unsigned long NextLogTime;       //stores the nex log time in unix format
    unsigned long EndTimeUnix;       //will convert the ending time to unix time
    long TimeNowUnix;                //stores the current time in unix format (Sleep functions)
       
    
      
    //// OTHERS /////////////////////////////////////////////////////////////////////////
         
    //button variables
    boolean ButtonPress = 0;           //the button has been pressed?
    boolean ButtonClick = 0;           //the button has been clicked?
    unsigned int PressLimit = 1750;    //in milliseconds - pressing time required until ButtonPress mode is recognized
   
    boolean FirstSleep = 1;
    
    volatile unsigned long Samples=0;
    byte ReadsCounter=0;
   
    

    
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// FUNCTIONS /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   
   
    //// ERROR FUNCTION /////////////////////////////////////////////////////////////////////////////////
    //// to let us know if anything goes wrong - it turns on the redLED and prints the error ////////////
   
    void Error (char *str)  {
             
           MiniSerial.print(F("Error: "));
           MiniSerial.println(str);
           digitalWrite(redLED, HIGH);   
           while(1);
     }
     
     
          
    //// TIME UPDATE FUNCTION /////////////////////////////////////////////////////////////////////////////
    //// to update the RTC ////////////////////////////////////////////////////////////////////////////////
     
      void TimeUpdate()  {
    
          //get date and time from the computer
          DateTime Compiled = DateTime(__DATE__, __TIME__);
          
          //read last date/time stored in the EEPROM
          byte OC_Second = EEPROM.read(10);
          byte OC_Minute = EEPROM.read(11);
          byte OC_Hour = EEPROM.read(12);
          byte OC_Day = EEPROM.read(13);
          byte OC_Month = EEPROM.read(14);
          int OC_Year = (EEPROM.read(15) + 2000);
            
           
          DateTime OldCompiled (OC_Year, OC_Month, OC_Day, OC_Hour, OC_Minute, OC_Second);
         
          //if they match the Arduino didn´ reset after a upload so RTC can´ be updated
          if (Compiled.unixtime() == OldCompiled.unixtime())  {return;}
         
          else {
               
               //update the RTC
               RTC.adjust(DateTime(__DATE__, __TIME__)); 
                
               MiniSerial.println(F("Updating RTC..."));
               
               MiniSerial.print(Compiled.year());
               MiniSerial.print('/');
               MiniSerial.print(Compiled.month());
               MiniSerial.print('/');
               MiniSerial.print(Compiled.day());
               MiniSerial.print(' ');
               MiniSerial.print(Compiled.hour());
               MiniSerial.print(':');
               MiniSerial.print(Compiled.minute());
               MiniSerial.print(':');
               MiniSerial.println(Compiled.second());
               
               digitalWrite(greenLED, HIGH);   
               delay(500);
               digitalWrite(greenLED, LOW); 
               
               //save the last compiled date/time to the EEPROM
               EEPROM.write(10, Compiled.second());
               EEPROM.write(11, Compiled.minute());
               EEPROM.write(12, Compiled.hour());
               EEPROM.write(13, Compiled.day());
               EEPROM.write(14, Compiled.month());
               EEPROM.write(15, (Compiled.year()-2000));
               
               
         }
    }
       
    
    //// CHECK ID FUNCTION ///////////////////////////////////////////////////////////////////////////
    //// Reads/Writes Arduino ID from/to EEPROM //////////////////////////////////////////////////////
     
     void CheckID ()  {
       
           char readID[7];           
           
            for (int i=0; i<7; i++) {readID[i] = EEPROM.read(i);}
            
            // if ArduinoID and readID do not match, write ArduinoID
            if (strcmp(readID,ArduinoID)!=0)  {
                  for (int i=0; i<7; i++) {EEPROM.write(i, ArduinoID[i]);}
                  MiniSerial.print("ArduinoID = ");
                  MiniSerial.println(ArduinoID);
              }
  
     }
          
           
    
   //// BUTTON CHECK FUNCTION /////////////////////////////////////////////////////////////////////////  
   //// to read the pushbutton and recognize the two different pushes - single click or long-time press
   
    void ButtonCheck() {
    
           int count = 0;  
           boolean hold = 0;
          
           while ((digitalRead(onButton)==HIGH) && (hold==0)) {
            
                  delay(10);
                  count = count+10;
                  if (count >= PressLimit) {ButtonPress=1; hold=1;} 
                 
              }
           
           if ((count > 0) && (hold==0)) {ButtonClick=1;}
          
     }
               
         
     
    //// DATE_TIME FUNCTION //////////////////////////////////////////////////////////////////////////
    //// to return date and time using FAT_DATE macro to format fields ///////////////////////////////
     
    void Date_Time (uint16_t* date, uint16_t* time)  {
           
           DateTime now = RTC.now();
           *date = FAT_DATE(now.year(), now.month(), now.day());      
           *time = FAT_TIME(now.hour(), now.minute(), now.second()); 
     }
     
     
     
     //// GET SETTINGS FUNCTION //////////////////////////////////////////////////////////////////////////
     //// to read SD configuration file and set the variables ////////////////////////////////////////////
    
    void GetSettings()  {
      
  
           // open the settings file
           SdFile SettingsFile ("settings.txt", O_READ);
           
    
           if (!SettingsFile.isOpen()) {
                  MiniSerial.println(F("Settings file not available"));
                  return;
              }
           
           
           int ST_Year;
           byte ST_Month;
           byte ST_Day;
           byte ST_Hour;
           byte ST_Minute;

            
           for (byte i=0; i<=5; i++)  {    
             
                   char line[24];
                   SettingsFile.fgets(line, 24);     // read one line
                   char* p_pos = strchr(line, '=');  // find the '=' position
                   
                   if(i==0)  {Rate = atoi(p_pos+1);}
                   if(i==1)  {Duration = atoi(p_pos+1);}
                   if(i==2)  {ReadInterval = atoi(p_pos+1);}
                   if(i==3)  {ST_Year = atoi(p_pos+1); ST_Month = atoi(p_pos+7); ST_Day = atoi(p_pos+8);}
                   if(i==4)  {ST_Hour = atoi(p_pos+1); ST_Minute = atoi(p_pos+5);}
                   if(i==5)  {FileThreshold = atoi(p_pos+1);}
           }
           
          
           SettingsFile.close();
           
           DateTime StartTime (ST_Year, ST_Month, ST_Day, ST_Hour, ST_Minute);
           
           MiniSerial.print(F("Rate = "));
           MiniSerial.println(Rate);
           MiniSerial.print(F("Duration = "));
           MiniSerial.println(Duration);
           MiniSerial.print(F("ReadInterval = "));
           MiniSerial.println(ReadInterval);
           
           MiniSerial.print(F("Start Time = "));
           MiniSerial.print(StartTime.year(), DEC);
           MiniSerial.print('/');
           MiniSerial.print(StartTime.month(), DEC);
           MiniSerial.print('/');
           MiniSerial.print(StartTime.day(), DEC);
           MiniSerial.print(' ');
           MiniSerial.print(StartTime.hour(), DEC);
           MiniSerial.print(':');
           MiniSerial.print(StartTime.minute(), DEC);
           MiniSerial.println();
           
           MiniSerial.print(F("FileThreshold = "));
           MiniSerial.println(FileThreshold);
           
           if (Rate==0 || Duration==0 || ReadInterval==0) {Error("Error in settings file variables");}
                 
    }
      
 
       
         
   //// NEW LOG FILE FUNCTION /////////////////////////////////////////////////////////////////////////
   //// to create a new file and print the header /////////////////////////////////////////////////////

    void NewLogFile ()  {
      
      
           if (ReadsCounter != 0 && ReadsCounter < FileThreshold)  {return;}
           ReadsCounter=0;
           
           //get time
           DateTime FileTime = RTC.now();
           
          
           for (int i=0; i<1000; i++)  {
          
               filename[0] = i/100 + '0';
               filename[1] = ((i/10) % 10) + '0';
               filename[2] = i%10 + '0';    
              
               // only open a new file if it doesn't exist
               if (! sd.exists(filename)) {
                     logfile.open (filename, O_CREAT | O_WRITE);
                     break;
                  } 
              }
          
           MiniSerial.print(F("New File Created: "));
           MiniSerial.println(filename);
          
                     
           //print header
           logfile.println(ArduinoID);
           logfile.println(F(__FILE__));
                     
           
           //print time
           if (FileTime.day() < 10)  {logfile.print('0');}
           logfile.print(FileTime.day());
           logfile.print('/');
           if (FileTime.month() < 10)  {logfile.print('0');}
           logfile.print(FileTime.month());
           logfile.print('/');
           logfile.print(FileTime.year()); 
           logfile.print(" - ");
           if (FileTime.hour() < 10)  {logfile.print('0');}
           logfile.print(FileTime.hour());
           logfile.print(':');
           if (FileTime.minute() < 10)  {logfile.print('0');}
           logfile.print(FileTime.minute());
           logfile.print(':');
           if (FileTime.second() < 10)  {logfile.print('0');}
           logfile.println(FileTime.second());
          
             
           logfile.print(F("Rate (Hz): "));
           logfile.println(Rate);
           logfile.print(F("Duration (s): "));
           logfile.println(Duration);
           logfile.print(F("ReadInterval (s): "));
           logfile.println(ReadInterval);
           logfile.println();
           
     
           logfile.println("Timestamp,Temperature(C)"); 
           
           logfile.println();
           
           logfile.close();  
               
     }
     
     
   // READ FUNCTION /////////////////////////////////////////////////////////////////////////
   //// to read the data in the ISR (Interrupt Service Routine) /////////////////////////////
   
    void Read() {
       
         digitalWrite(greenLED, HIGH);
         
         Data* p = DataBuffer.waitFree(TIME_IMMEDIATE);
         
         if (!p) {Error("Overrun");}
               
         p -> logtime = RTC.now();
         p -> temperature = thermocouple.readCelsius();
         
         DataBuffer.signalData();
         
         Samples++;
         
         if (isnan(p->temperature)) {return;} 

         digitalWrite(greenLED, LOW);

      }
      
        
   //// LOG FUNCTION /////////////////////////////////////////////////////////////////////////
   //// to save the data to the microSD card /////////////////////////////////////////////////
   
    void Log() {
         
      
         logfile.open (filename, O_APPEND | O_WRITE); 
         Samples=0;
         
         FlexiTimer2::start();
         
         while (Samples < (Rate*Duration))  {
           
                 Data* p = DataBuffer.waitData(TIME_IMMEDIATE);
                 if (!p) {continue;}
                 char timeStamp[10];
                 sprintf (timeStamp, "%02u:%02u:%02u,",p->logtime.hour(),p->logtime.minute(),p->logtime.second());    
                 logfile.print(timeStamp);
                 logfile.println(p->temperature);                  
                 DataBuffer.signalFree();
                 
            }
 
         FlexiTimer2::stop();
         
         DateTime now = RTC.now();
         EndTimeUnix = now.unixtime();
                
         while (DataBuffer.dataCount() > 0)  {
           
                 Data* p = DataBuffer.waitData(TIME_IMMEDIATE);
                 char timeStamp[10];
                 sprintf (timeStamp, "%02u:%02u:%02u,",p->logtime.hour(),p->logtime.minute(),p->logtime.second());
                 logfile.print(timeStamp);
                 logfile.println(p->temperature);
                 DataBuffer.signalFree();
           }
         
         
         digitalWrite(redLED, HIGH);
         logfile.close(); 
         digitalWrite(redLED, LOW);
         
         ReadsCounter++;
         
         MiniSerial.println(F("End!"));
         MiniSerial.println();
         
         delay(10);
                     
  }
            
  
   //// SLEEP FUNCTION 1 ////////////////////////////////////////////////////////////////////////////////////////////
   //// to manage sleep before the start time, in Button Click Mode /////////////////////////////////////////////////
    
    void Sleep1 () {
          
       if (FirstSleep==1)  {
               
               DateTime now = RTC.now();
               TimeNowUnix = now.unixtime();
               unsigned long TimeLeft = StartTimeUnix - TimeNowUnix;
               
               if (TimeLeft <= 0)  {return;}
       
               MiniSerial.println(F("Sleeping..."));
            
               for (int i=0; i<(TimeLeft-2); i++)  {LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);}
               
               while(TimeNowUnix < StartTimeUnix)  {DateTime now = RTC.now(); TimeNowUnix = now.unixtime();}
          
               FirstSleep=0;
            
         }
         
       else return;
               
  }
      
     
   //// SLEEP FUNCTION 2 ////////////////////////////////////////////////////////////////////////////////////////////
   //// to manage sleep between read cycles /////////////////////////////////////////////////////////////////////////
    
    void Sleep2 () {
      
          NextLogTime = EndTimeUnix + ReadInterval;
     
          MiniSerial.println(F("Sleeping..."));
          
          for (int i=0; i<ReadInterval; i++)  {
                
                DateTime now = RTC.now();
                TimeNowUnix = now.unixtime();
                if(TimeNowUnix >= (NextLogTime-2))  {break;}
                LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
              }
          
          NewLogFile();
           
          while(TimeNowUnix < NextLogTime)  {DateTime now = RTC.now(); TimeNowUnix = now.unixtime();}
  
               
      }
  
   
          
           
                
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// VOID SETUP ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  
 void setup () {   

     //adjust clock speed to save power
     Set_prescaler(Prescaler);     
  
     //start serial comunication   
     MiniSerial.begin(9600);
     MiniSerial.println();
      
      
     //initialize the button pin as a input:
     pinMode(onButton, INPUT);
   
     //initialize the LED as an output:
     pinMode(redLED, OUTPUT);
     pinMode(greenLED, OUTPUT);
     
     
     //connect and initialize RTC
     Wire.begin();
     RTC.begin();
     if (!RTC.begin())  {Error("RTC failed"); }
     else {MiniSerial.println(F("RTC initialized!")); }
     
     
     //update the RTC
     TimeUpdate();
     
     
     //make sure that the default chip select pin is set to output, even if you don't use it:
     pinMode(chipSelect, OUTPUT);	
		
     //see if the card is present and can be initialized:
     if (!sd.begin(chipSelect))  {Error("Card failed, or not present");}
     else {MiniSerial.println(F("Card initialized!")); }
     
     //callback for file timestamps
     SdFile::dateTimeCallback(Date_Time);
     
     
     //set unused pins to low to reduce power consumption
     pinMode(0, OUTPUT);
     digitalWrite(0, LOW); 
     pinMode(1, OUTPUT);
     digitalWrite(1, LOW); 
     pinMode(2, OUTPUT);
     digitalWrite(2, LOW);  
     pinMode(7, OUTPUT);
     digitalWrite(7, LOW);    
     
     
     //set voltage reference
     analogReference(EXTERNAL);
     
     //check Arduino ID
     CheckID();
    
 
     //read settings file
     GetSettings();
     
     //set the Interrupt     
     if(Rate>1000)  {FlexiTimer2::set(1, 1.0/Rate, Read);}
     else  {FlexiTimer2::set(1000/Rate, Read);}
      
 }
 
          
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //// VOID LOOP /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  
  void loop () {
    
      
      ButtonCheck();
      
      /////////////////////////////////////////////////////////////////////////////////////////////////////
      //// MODE 1 - Start logging immediately /////////////////////////////////////////////////////////////
  
      if (ButtonClick==1)  {
          
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
    
 
      while (ButtonClick==1)  {Log(); Sleep2();}
             
       
      ///////////////////////////////////////////////////////////////////////////////////////////////////// 
      //// MODE 2 - Start logging in the defined StartTime ////////////////////////////////////////////////
       
      if (ButtonPress==1)  {
            
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
           
           NewLogFile();
           
       }
       
              
      while (ButtonPress==1)  {Sleep1(); Log(); Sleep2();}
      
      
       
         
      
}
  
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

