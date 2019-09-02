/*
The purpose of this code is to enable the use of the MicroView Arduino to be used as a replacement board for the 
2011 Dye Proto Matrix Rail (PMR). The current board is out of production and many of the predetermined settings
are outdated. This code can also be applied in the future to other models of paintball markers as the IO and
settings are universally very similar. Just the electronics and ergonomics would be different.

Developer: Matthew Rodriguez - salsarodriguez@outlook.com

Last update: Aug 28, 2019


ToDo:
- <WRITTEN - 8/23/19 | TESTED - ????> ABS implemented
- <WRITTEN - 8/21/19 | TESTED - ????> Configuration routine (with scrolling option)
- <WRITTEN - 8/23/19 | TESTED - ????> eye power pin on and off
- <WRITTEN - 8/28/19 | TESTED - ????> add ramping as firing mode
- eye error when state doesn't change after firing <?> symbol
- optimaze ROF and dwell to include code timing?
- safety lock feature which turns off on 3 trigger pulls?
- switch from polling to interrupts?
- first shot safety option on semi-auto. bascially doesn't auto on first shot but waits for second trigger pull within certain time
- low battery indicator (need to figure out at waht voltage the gun won't fire)
- bitmaps for battery, eye state, firing mode
- game modes (fastest ROF with disabled firing with a gauge that calculates the ROF and holds the highest value to see who can fire the fastest)

*/

#include <MicroView.h>  // for use with the MicroView arduino package
#include <EEPROM.h>     // to be able to save variables when power is cycled
#include <Timer.h>      // timer for RoF measurment
 
const int ver = 004;        // version number for EEPROM control


// Pin configuration
const int solenoidPin = 1;    // the number of the solenoid pin
const int triggerPin = 0;     // the number of the trigger pin
const int ledPin =  A0;       // the number of the LED pin (may not be used)
const int buttonUpPin = 3;    // the number of the up botton pin
const int buttonDownPin = 2;  // the number of the down button pin
const int eyePin = A1;        // the number of the eye pin
const int eyePowerPin = A2;   // turn eyes on or off to conserve battery
const int batteryLevel = A3;  // the number of the battery voltage pin to determine battery level


// EEPROM and other variables
int triggerState = 1;               // variable for reading the pushbutton status
int ROF;                            // variable for the rate of fire of the marker x10 - EEPROM Address 0
int dwell;                          // ms for solenoid to stay open - EEPROM Address 1
//int recharge;                     // this would be the amount of time to wait after dwell for the full firing cycle (dwell+recharge) before marker can fire again and would replace the calculation for ROF (1000ms/ROF - dwell = recharge)
int rampROF;                        // ROF for ramp mode (wanted to seperate from auto ROF not to kill fun of full auto)
int rampTriggerBPS;                 // ROF at which is switches from semi to auto
int triggerSensitivity;             // ms delay before trigger is considered active (debounce basically)
int ABS;                            // 0 = OFF, 1 = ON - Anti Bolt Stick enables higher dwell for first shot
int ABStime = 10;                   // additional time to add to dwell if ABS is enabled
int eyeState = 0;                   // 0 = Eye empty, 1 = Ball in Eye, 2 = Error - variables for reading the state of the eye
int eyesOnOff;                      // 0 = Eyes Off, 1 = Eyes On
int firingMode;                     // 0 = Semi, 1 = Burst, 2 = Auto, 3 = Ramp
int upButtonState = 1;              // variable for reading the pushbutton status
int downButtonState = 1;            // variable for reading the pushbutton status
int solenoidReset = 0;              // variable to marking whether the gun has just been fired and is reset on trigger release
int shotsSincePower = 0;            // ball counter
int rampCount = 0;                  // number of shots fired within ramp ROF trigger time
int bat = 9;                        // battery voltage level
//int shotsSinceLastMaintenance;    // maintenance tracker which stores shots in EEPROM and can be reset (concern is EEPROM read/write limit) 
unsigned long timerStart = 0;       // the time the delay started
unsigned long buttonHoldTimer = 0;  // the time both buttons were pressed
unsigned long gameTimer = 0;        // variable for game timer on first shot after resetting ball count
unsigned long screenResetTimer = 0; // timer for resetting the screen after no input for a set time
unsigned long rampTimer = 0;        // timer to ramp mode



MicroViewWidget *sliderWidget, *sliderWidgetMenu;      // creates widget for slider for configuration routine


void setup() {
  // Set pin modes
  pinMode(ledPin, OUTPUT);                // initialize the LED pin as an output
  pinMode(triggerPin, INPUT_PULLUP);      // initialize the trigger pin as an input
  pinMode(buttonUpPin, INPUT_PULLUP);     // initialize the pushbutton pin as an input
  pinMode(buttonDownPin, INPUT_PULLUP);   // initialize the pushbutton pin as an input
  pinMode(solenoidPin, OUTPUT);           // initialize the solenoid pin as an output
  pinMode(eyePin, INPUT);                 // initialize the eye pin as an input
  pinMode(eyePowerPin, OUTPUT);           // initialize the eye LED as an output 
  
  // Set splash display on screen
  uView.begin();                      // start MicroView
  uView.clear(PAGE);                  // clear page
  uView.print("2011 PMR\n");          // display string
  uView.print("Salsa\n");
  uView.print("08/28/2019");
  
  // first time configuration on startup
  if(EEPROM.read(8) != ver){
    uView.print("EEPROM P");
    EEPROM.write(0, 100);         // ROF
    EEPROM.write(1, 30);          // dwell
    EEPROM.write(2, 100);         // ramp ROF
    EEPROM.write(3, 50);          // ramp BPS trigger requirement to enter ramp (5bps)
    EEPROM.write(4, 5);           // trigger sensitivity 
    EEPROM.write(5, 0);           // ABS (anti-bolt stick) - OFF
    EEPROM.write(6, 1);           // eyes on or off - ON
    EEPROM.write(7, 0);           // firing mode - Semi
    EEPROM.write(8, ver);         // value marking that the EEPROM is now configured with the default settings (value needs to be <255)
  }
  else{
    uView.print("EEPROM OK");
  }
  
  
  // Read and populate variables from EEPROM
  ROF = EEPROM.read(0);
  dwell = EEPROM.read(1);
  rampROF = EEPROM.read(2);
  rampTriggerBPS = EEPROM.read(3);
  triggerSensitivity = EEPROM.read(4);
  ABS = EEPROM.read(5);
  eyesOnOff = EEPROM.read(6);
  firingMode = EEPROM.read(7);
  
  
  //Section to electronically turn eyes off if moved to ouput pin instead of just ignoring them? saves battery
  if(eyesOnOff == 0){
    digitalWrite(eyePowerPin, LOW);
  }
  else{
    digitalWrite(eyePowerPin, HIGH);
  } 
  
  uView.display();                // prints what is in the cache to the display
  delay(3000);                    // allows splash screen to be visible
  mainDisplayUpdate();            //updates display to home screen

}


void loop() {
  // read the state of the pushbutton values
  triggerState = digitalRead(triggerPin);
  upButtonState = digitalRead(buttonUpPin);
  downButtonState = digitalRead(buttonDownPin);
  
  
  // if the up and down buttons are held down for 3 seconds then enter configuration mode
  if (upButtonState == 0 && downButtonState == 0){
    if(buttonHoldTimer == 0){
      buttonHoldTimer = millis();
    }
    else if(millis() - buttonHoldTimer >= 3000){
      configuration();
      buttonHoldTimer = 0;
      mainDisplayUpdate();
    }
  }
  
  
  // reset ball counter if down button and trigger are held at same time for 2 seconds
  else if (downButtonState == 0 && triggerState == 0){
    if(buttonHoldTimer == 0){
      buttonHoldTimer = millis();
    }
    else if(millis() - buttonHoldTimer >= 2000){
      shotsSincePower = 0;
      gameTimer = 0;
      buttonHoldTimer = 0;
      mainDisplayUpdate();
      while(digitalRead(triggerPin)==LOW){
        delay(100);
      }
    }
  }
 
  
  // check if the trigger is pulled
  else if (triggerState == LOW) {
 
    // SEMI-AUTOMATIC
    if (firingMode == 0 && solenoidReset == 0){         // if in semi-auto and the trigger has been released once
      fire();
    }
    
    
    // BURST MODE
    else if(firingMode == 1 && solenoidReset == 0){                         // burst mode (maybe make burst mode not accessible while eyes are disabled?)
      for(int i = 0; i < 3; i++){                                             // 3 shots
        fire();
        delay(100);
        while(eyeStatus() == 0 && (millis()-timerStart) < 2000){            // times out after 2 seconds if ball isn't detected in eye (needed to maintain 3 shot burst if eyes are delayed)
        }
      }
    }
    
    
    // AUTOMATIC
    else if(firingMode == 2){                           // full auto mode
      if((millis() - timerStart) >= (1000/(ROF/10))){   //ensure that BPS does not exceed ROF
        fire();
      } 
    }
    
    
    // RAMP (speeds up into automatic fire after 3 pulls at a set ROF)
    else if(firingMode == 3 && solenoidReset==0){                           // ramp mode
      if(rampCount == 0){                               // if first new shot after ramp count expired then set the timer, increase count, and fire
        rampTimer = millis();
        rampCount++;
        fire();
      }
      else if(rampCount < 3 && (millis()-rampTimer) < (1000/ (rampTriggerBPS/10) ) ){     // if count is less than 3 required to switch into full auto and the ROF is higher than required trigger then increase count, set time, and fire (semi-mode)
        rampCount++;
        rampTimer = millis();
        if(solenoidReset = 0){
          fire();
        }
      }
      // perhaps need to move this section into else or outside of if sctructure and just set a flag
      else if(rampCount == 3 && (millis()-rampTimer) < (1000/ (rampTriggerBPS/10) ) ){       // if trigger pulled 3 times at higher ROF than required then switch into full auro and ensure that BPS does not exceed ROF
        if( (millis() - timerStart) >= ( 1000/(rampROF/10) ) ){
          fire();
        }
        rampTimer = millis();
      } 
      
      if((millis()-rampTimer) > (1000/ (rampTriggerBPS/10) )){                            // if ROF is not within required BPS then reset the ramp count 
        rampCount = 0;
      }
    }
    
    
    mainDisplayUpdate();
  }
  
  
  // If nothing, then just reset solenoid pin to let semi-auto know it is ok to fire on next trigger pull
  else {
    digitalWrite(solenoidPin, LOW);                   // turn solenoid off (not necessary but just insurance)
    solenoidReset = 0;                                // mark that trigger has been let go for semi-auto mode
    buttonHoldTimer = 0;
    
    if(screenResetTimer == 0){                        // resets the display if no action is taken in 0.5s
        screenResetTimer = millis();
      }
    else if(millis() - screenResetTimer >= 500){      
      screenResetTimer = 0;
      mainDisplayUpdate();
    }
    
    
    /*
    
    If in ramp mode we need to set a flag and keep firing at the ROF for ramp
    
    */
    // redundant section here needed outside of trigger pull to keep it firing until triggerBPS is not maintained
    if(firingMode == 3){
      if(rampCount == 3 && (millis()-rampTimer) < (1000/ (rampTriggerBPS/10) ) ){       // if trigger pulled 3 times at higher ROF than required then switch into full auro and ensure that BPS does not exceed ROF
        if( (millis() - timerStart) >= ( 1000/(rampROF/10) ) ){
          fire();
        }
        rampTimer = millis();
      } 
      
      if((millis()-rampTimer) > (1000/ (rampTriggerBPS/10) )){                            // if ROF is not within required BPS then reset the ramp count 
        rampCount = 0;
      } 
      
    }
    
    
  }
}


// Triggers the solenoid to fire the marker
void fire(){
  if(eyeStatus() == 1){           // check if ball is detected by the eyes
    int dwellTotal = dwell;
    if(ABS == 1 && (millis()-timerStart) > 15000){      // apply ABS if gun hasn't fired in 15s
      dwellTotal = dwell + ABStime;
    }

    digitalWrite(solenoidPin, HIGH);  // turn on solenoid allowing air into spool
    delay(dwellTotal);                // keep solenoid open for dwell time period
    digitalWrite(solenoidPin, LOW);   // turn off solenoid
    timerStart = millis();            // mark when trigger was pulled for ROF timing in auto mode

    solenoidReset = 1;            // note that gun has been fired for semi-auto mode
    shotsSincePower++;            // increment shots fired
    if(gameTimer==0){             // sets game timer on first shot after shot count reset
      gameTimer = millis();
    }
    
  }  
}


// What is going on with the eyes? Is there a ball there or not? Returns 0 if empty, 1 if ball detected, and 2 if in error (future implementation)
int eyeStatus(){
  int eyeState = digitalRead(eyePin); // checks if beam is broken or not - HIGH = ball in eye, LOW = no ball???
  
  if(eyesOnOff == 0){                 // if the eyes are set to off, return a 1
    eyeState = 1;
  }

  return eyeState;
}


// Run to do a configuration of all of the parameters
void configuration(){
  //Menu first time setup
  int menuPosition = 5;
  configureScreenReset(menuPosition);
  menuPosition = 6; // first entry triggers down button, may move button detect delay before code
    
  while(1){
    triggerState = digitalRead(triggerPin);
    upButtonState = digitalRead(buttonUpPin);
    downButtonState = digitalRead(buttonDownPin);

    if(downButtonState == 0){                             // Check if down button pressed
      if(menuPosition > 0){
        menuPosition--;                                   // Move menu cursor up (it's inverted on the screen. the higher the number the lower the position)
        sliderWidgetMenu->setValue(menuPosition);
        uView.display();
        while(digitalRead(buttonUpPin)==LOW){             // wait for button to be unpressed
          delay(100);
        }
      }
    }

    else if(upButtonState == 0){                          // Check if up button is pressed
      if(menuPosition < 5){
        menuPosition++;                                   // Move menu cursor down (it's inverted on the screen. the higher the number the lower the position)
        sliderWidgetMenu->setValue(menuPosition);
        uView.display();
        while(digitalRead(buttonDownPin)==LOW){           // wait for button to be unpressed
          delay(100);
        }
      }
    }    

    else if(triggerState == 0){                           // Check if trigger is pulled
      while(digitalRead(triggerPin)==LOW){                // Wait for trigger to be unpulled
        delay(100);
      }
      
      delete sliderWidgetMenu;
      
      // Configure firing modes
      if(menuPosition == 5){
        firingMode = configureFiringMode(firingMode);                                                   // configure firing mode 
        EEPROM.update(7, firingMode);                                                                   // write firing mode value to address 7 
        if(firingMode == 2){                                                                            // configure AUTO mode
          ROF = configureNumericParameter("RoF (bps)", ROF, 90, 152, 1);                                // configured auto ROF
          EEPROM.update(0, ROF);                                                                        // write ROF value to address 0
        }
        else if(firingMode == 3){                                                                       // configure RAMP mode
          rampTriggerBPS = configureNumericParameter("Ramp Trig", rampTriggerBPS, 50, 150, 1);
          EEPROM.update(3, rampTriggerBPS);                                                             // write ramp bps trigger value to address 3
          rampROF = configureNumericParameter("Ramp ROF", rampROF, 90, 152, 1);
          EEPROM.update(2, rampROF);                                                                    // write ramp speed value to address 2
        }
      }
      
      // Configure Eyes
      else if(menuPosition == 4){
        eyesOnOff = configureBinaryParameter("Eyes", eyesOnOff);                                        // configure eyes setting
        EEPROM.update(6, eyesOnOff);                                                                    // write eye mode value to address 6 
      }  
      
      // Configure Dwell
      else if(menuPosition == 3){
        dwell = configureNumericParameter("Dwell(ms)", dwell, 8, 34, 1);                               // configure dwell setting
        EEPROM.update(1, dwell);                                                                        // write dwell value to address 1 
      }  

      // Configure ABS
      else if(menuPosition == 2){
        ABS = configureBinaryParameter("ABS", ABS);                                                     // configure ABS setting
        EEPROM.update(5, ABS);                                                                          // write anti bolt stick mode value to address 5 
      }  

      // Configure Trigger Sensitivity
      else if(menuPosition == 1){
        triggerSensitivity = configureNumericParameter("Trigger", triggerSensitivity, 0, 500, 10);  // configure trigger sensitivity setting
        EEPROM.update(4, triggerSensitivity);                                                           // write trigger sensitivity value to address 4 
      }  
      
      // Exit Menu and return to home screen
      else if(menuPosition == 0){
        mainDisplayUpdate();
        return;
      }
      
      configureScreenReset(menuPosition);
      
    }   
    
    delay(150);         // menu scroll responsiveness
  }
  
  buttonHoldTimer = 0;  //why is this here? shouldn't be able to be reached
}


//Resets the main configuration screen after a parameter has been configured
void configureScreenReset(int menuPos){
  uView.clear(PAGE);
  uView.setCursor(10,0);
  uView.print("Mode");   
  uView.setCursor(10,8);
  uView.print("EyeState");   
  uView.setCursor(10,16);
  uView.print("Dwell");   
  uView.setCursor(10,24);
  uView.print("ABS");   
  uView.setCursor(10,32);
  uView.print("Sensitiv");  
  uView.setCursor(10,40);
  uView.print("EXIT");
  sliderWidgetMenu = new MicroViewSlider(0, 3, 0, 5, WIDGETSTYLE3 + WIDGETNOVALUE);   // 6 list slider for menu
  sliderWidgetMenu->setValue(menuPos);
  uView.display();  
  return;
}


// configures the firing mode
int configureFiringMode(int currentValue){
  uView.clear(PAGE);                // clear page
  uView.setCursor(0,0);
  uView.print("Configure:\n");      
  uView.print("Fire Mode");
  uView.print("\nCurrently:\n");   
  if(currentValue == 0){
    uView.print("Semi");   
  }
  else if(currentValue == 1){
    uView.print("Burst");   
  }
  else if(currentValue == 2){
    uView.print("Auto");   
  } 
  else if(currentValue == 3){
    uView.print("Ramp");   
  }
  uView.display();
  
  
  while(1){
    triggerState = digitalRead(triggerPin);
    upButtonState = digitalRead(buttonUpPin);
    downButtonState = digitalRead(buttonDownPin);
  
    if(upButtonState == 0 || downButtonState == 0){
      //change state of current value
      if(upButtonState == 0 && currentValue < 3){
        currentValue++;
      }
      else if(downButtonState == 0 && currentValue > 0){
        currentValue--;
      }
      
      uView.clear(PAGE);                // clear page
      uView.setCursor(0,0);
      uView.print("Configure:\n");      
      uView.print("Fire Mode");
      uView.print("\nCurrently:\n");   
      if(currentValue == 0){
        uView.print("Semi");   
      }
      else if(currentValue == 1){
        uView.print("Burst");   
      }
      else if(currentValue == 2){
        uView.print("Auto");   
      } 
      else if(currentValue == 3){
        uView.print("Ramp");   
      }
      uView.display();
      
      while(digitalRead(buttonUpPin)==LOW || digitalRead(buttonDownPin)==LOW){
        delay(100);
      }
            
    }
    
    else if(triggerState == 0){
      while(digitalRead(triggerPin)==LOW){
        delay(100);
      }
      return currentValue;
    }
      
  }  

}


// configures any binary parameter such as on/off
int configureBinaryParameter(String parameterStr, int currentValue){
  uView.clear(PAGE);                // clear page
  uView.setCursor(0,0);
  uView.print("Configure:\n");      
  uView.print(parameterStr);
  uView.print("\nCurrently:\n");   
  if(currentValue == 0){
      uView.print("OFF");   
  }
  else if(currentValue == 1){
      uView.print("ON");   
  }
  
  uView.display();
  
  
  while(1){
    triggerState = digitalRead(triggerPin);
    upButtonState = digitalRead(buttonUpPin);
    downButtonState = digitalRead(buttonDownPin);
  
    if(upButtonState == 0 || downButtonState == 0){
      //change state of current value
      if(currentValue == 0){
        currentValue = 1;
      }
      else if(currentValue == 1){
        currentValue = 0;
      }
      
      uView.clear(PAGE);                // clear page
      uView.setCursor(0,0);
      uView.print("Configure:\n");      
      uView.print(parameterStr);
      uView.print("\nCurrently:\n");   
      if(currentValue == 0){
          uView.print("OFF");   
      }
      else if(currentValue == 1){
          uView.print("ON");   
      }
      uView.display();
      while(digitalRead(buttonUpPin)==LOW || digitalRead(buttonDownPin)==LOW){
        delay(100);
      }
            
    }
    
    else if(triggerState == 0){
      while(digitalRead(triggerPin)==LOW){
        delay(100);
      }
      return currentValue;
    }
      
  }   

}


// call to update a numeric parameter
int configureNumericParameter(String parameterStr, int currentValue, int min, int max, int increment){
  uView.clear(PAGE);                // clear page
  uView.setCursor(0,0);
  uView.print("Configure:");   // display string
  uView.print(parameterStr);
  uView.print("\nCurrently:"); 
  uView.print(currentValue);
  sliderWidget = new MicroViewSlider(0,30,min,max, WIDGETSTYLE1);// draw Slider widget at x=0,y=20,min=9.8, max=15.2
  sliderWidget->setValue(currentValue);
  uView.display();
  
  while(digitalRead(triggerPin) == HIGH){
    if(digitalRead(buttonUpPin) == LOW && currentValue < max){
      currentValue = currentValue + increment;
      sliderWidget->setValue(currentValue);
      uView.display();
    }
    else if(digitalRead(buttonDownPin) == LOW && currentValue > min){
      currentValue = currentValue - increment;
      sliderWidget->setValue(currentValue);
      uView.display();
    }
    delay(250);
  }

  while(digitalRead(triggerPin)==LOW){          // wait for trigger to be released
    delay(100);
  }
  
  delete sliderWidget;
  return currentValue;    // returns value to write to EEPROM
}


//Updates the main display with current values
void mainDisplayUpdate(){
  uView.clear(PAGE);                  // clear page
  uView.setCursor(0,0);               // set cursor to 0,0
  //uView.print("2011 PMR\n");          // display string
  
  // Firing Mode
  uView.print("Mode:");
  if(firingMode == 0){
    uView.print(" Semi");
  }
  else if(firingMode == 1){
    uView.print("Burst");
  }
  else if(firingMode == 2){
    uView.print("Auto-");
    uView.print(String(ROF/10));
    uView.print(" bps");
  }
  else if(firingMode == 3){
    uView.print("Ramp-");
    uView.print(String(rampTriggerBPS/10));
    uView.print("/");
    uView.print(String(rampROF/10));
  } 
  
  //Battery Level
  bat = analogRead(batteryLevel);
  float battery = 2*bat*5/1024;
  uView.print("Bat: ");
  uView.print(String(bat));
  if(battery / int(battery) > 0){
    uView.print(".");
    uView.print(String(10*(battery-int(battery))));
  }
  uView.print("V");  

  
  // Status of Eyes
  uView.print("\n(");
  if(eyesOnOff == 0){
    uView.print("X");
  }
  else if(eyeStatus()==1){
    uView.print("0");
  }
  else{
    uView.print("_");
  }
  uView.print(")");
  
  
  // Ball count
  uView.print("\nShot: ");
  uView.print(String(shotsSincePower));
  
  // Game timer
  uView.print("\nTime: ");
  int timer = (millis()-gameTimer)/1000;
  int minutes = timer / 60;
  int seconds = timer % 60;
  uView.print(String(minutes));
  uView.print(":");
  if(seconds < 10){
    uView.print("0");
    uView.print(String(seconds));
  }
  else{
    uView.print(String(seconds));    
  }
  
  
  uView.display();                    // prints what is in the cache to the display
}
