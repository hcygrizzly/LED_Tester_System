#include <Adafruit_PWMServoDriver.h>
#include <Countimer.h>
#include <DTIOI2CtoParallelConverter.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

const char * app_ver = "v1.0";

const byte PWM_OUTPUT_EN = 4; //default is low
const byte PWM_OUTPUT_PIN_R = 0;
const byte PWM_OUTPUT_PIN_G = 1;
const byte PWM_OUTPUT_PIN_B = 2;
const byte PWM_OUTPUT_PIN_FW = 3;
const byte PWM_OUTPUT_PIN_CW = 4;

const byte EXP_INTR_PIN = 2;
const byte SW_INTR_PIN = 3;

const byte EXP_ROTARY_SW_1 = PIN0_0;
const byte EXP_ROTARY_SW_2 = PIN0_1;
const byte EXP_ROTARY_SW_3 = PIN0_2;
const byte EXP_ROTARY_SW_4 = PIN0_3;
const byte EXP_ROTARY_SW_5 = PIN0_4;
const byte EXP_ROTARY_SW_6 = PIN0_5;
const byte EXP_ROTARY_SW_7 = PIN0_6;
const byte EXP_ROTARY_SW_8 = PIN0_7;
const byte EXP_ROTARY_SW_9 = PIN1_0;
const byte EXP_ROTARY_SW_10 = PIN1_1;

const uint32_t REFRESH_RATE_MSEC = 1000; //update lcd timer count every 1 sec
const uint32_t ON_MSEC = 1000; //stable time before registering on
const uint32_t OFF_MSEC = 500; //stable time before registering off
const uint32_t CHECK_MSEC = 100; //read switch every 100ms when detected state change

char *LEDTestMsg[] =
{
  "1 Red Primary",
  "2 Green Primary",
  "3 Blue Primary",
  "4 2200K Primary",
  "5 6500K Primary",
  "6 Yellow",
  "7 Cyan",
  "8 Magenta",
  "9 4000K"
};

//types of LED test configurations
enum _LED_config
{
  RED_PRI,
  GREEN_PRI,
  BLUE_PRI,
  TEMP_2200K_PRI,
  TEMP_6500K_PRI,
  YELLOW,
  CYAN,
  MAGENTA,
  TEMP_4000K
};

typedef struct _LED_dutycycle_config_t
{
  uint16_t red_dutycycle;
  uint16_t green_dutycycle;
  uint16_t blue_dutycycle;
  uint16_t fw_dutycycle;
  uint16_t cw_dutycycle;
}LED_dutycycle_config_t;

//the calculation for the PCA9685 LED controller PWM output register
//duty cycle in % * 4096 - 1 (i.e. 85.7% * 4096 - 1 = ~3509)
//the counter starts at 0 and ends at 4095, so minus 1 is required
//where 4095 will be fully on LED and 0 will be fully off LED
LED_dutycycle_config_t LED_cfg_table[] = 
{ 
  {0x0FFF, 0x0000, 0x0000, 0x0000, 0x0000}, //RED_PRI
  {0x0000, 0x0FFF, 0x0000, 0x0000, 0x0000}, //GREEN_PRI
  {0x0000, 0x0000, 0x0FFF, 0x0000, 0x0000}, //BLUE_PRI
  {0x0000, 0x0000, 0x0000, 0x0FFF, 0x0000}, //TEMP_2200K_PRI
  {0x0000, 0x0000, 0x0000, 0x0000, 0x0FFF}, //TEMP_6500K_PRI
  {0x0FFF, 0x0DB5, 0x0000, 0x0000, 0x0000}, //YELLOW
  {0x0000, 0x0FFF, 0x022C, 0x0000, 0x006D}, //CYAN
  {0x0FFF, 0x0000, 0x0395, 0x0000, 0x0000}, //MAGENTA
  {0x0000, 0x0FCA, 0x0000, 0x0FFF, 0x0D47}  //TEMP_4000K
};

//timer for counting the test duration
Countimer countUpTimer;

//timer for start/stop switch debouncing
Countimer debounceTimer;

//uses the default address 0x40
Adafruit_PWMServoDriver pwmLEDDrv = Adafruit_PWMServoDriver();

// The LCD constructor - I2C address 0x27
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

//PCA9539 I/O Expander (with A1 = 0 and A0 = 0)
DTIOI2CtoParallelConverter ioExpandr(0x77); 

int g_test_selection = RED_PRI; //default is RED_PRI
int g_display_selection = RED_PRI;
volatile int g_sw_intr_state = 0;
volatile int g_exp_intr_state = 0;
byte g_debouncedSwState = 1; //off

void swInterruptHandler()
{
  g_sw_intr_state = 1;
}

void expInterruptHandler()
{
  g_exp_intr_state = 1;
}

int getSWSelection()
{
  int ret = 0;
  byte input_sw_9 = EXP_ROTARY_SW_9;
  byte input_sw_1_8[8] = { EXP_ROTARY_SW_1, EXP_ROTARY_SW_2, EXP_ROTARY_SW_3, EXP_ROTARY_SW_4,\
                          EXP_ROTARY_SW_5, EXP_ROTARY_SW_6, EXP_ROTARY_SW_7, EXP_ROTARY_SW_8 };

  if(ioExpandr.digitalRead1(input_sw_9))
  {
    if(!input_sw_9)
    {
      ret = TEMP_4000K;
    }
    else
    {
      int index = 0;
      
      for(index = 0; index < sizeof(input_sw_1_8); index++)
      {
        if(ioExpandr.digitalRead0(input_sw_1_8[index]))
        {
          if(!input_sw_1_8[index])
          {
            ret = index;
            break;
          }
        } 
      }
      
      //reached max and no inputs toggled
      if(sizeof(input_sw_1_8) == index)
      {
        ret = g_test_selection; // return the current test selection
      }
    }
  }
  
  return ret;
}

void setPWMLEDsOff()
{
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_R, 0x0000, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_G, 0x0000, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_B, 0x0000, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_FW, 0x0000, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_CW, 0x0000, true);
}

void setPWMOutput(LED_dutycycle_config_t * cfg)
{  
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_R, cfg->red_dutycycle, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_G, cfg->green_dutycycle, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_B, cfg->blue_dutycycle, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_FW, cfg->fw_dutycycle, true);
  pwmLEDDrv.setPin(PWM_OUTPUT_PIN_CW, cfg->cw_dutycycle, true);
}

void completeTimerCount()
{
  countUpTimer.restart(); //restart the timer once end
}

void displayTimerCount()
{
  lcd.setCursor(0,1);
  lcd.print("Timer");
  lcd.setCursor(6,1);
  lcd.print(countUpTimer.getCurrentTime());
}

void displayLEDTestMsg()
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(LEDTestMsg[g_test_selection]);
  Serial.print("Displaying LED test msg no:");
  Serial.println(g_test_selection);
}

void displayStartMsg()
{
  lcd.setCursor(0,0);
  lcd.print("LED Tester");
  lcd.setCursor(11,0);
  lcd.print(app_ver);
  lcd.setCursor(0,1);
  lcd.print("Select test:1-9");
}

//returns true if state changed
bool debounceSwitch(byte *state)
{
  static uint8_t count = OFF_MSEC/CHECK_MSEC;
  bool state_changed = false;

  //read the switch from the HW
  byte raw_state = digitalRead(SW_INTR_PIN);
  *state = g_debouncedSwState;

    if (raw_state == g_debouncedSwState)
    {
        //set the timer which allows a change from current state.
        if(g_debouncedSwState)
        {
            count = OFF_MSEC/CHECK_MSEC;
        }
        else
        {
            count = ON_MSEC/CHECK_MSEC;
        }
    }
    else
    {
        //state has changed - wait for new state to become stable.
        if (--count == 0)
        {
            // Timer expired - accept the change.
            g_debouncedSwState = raw_state;
            state_changed = true;
            *state = g_debouncedSwState;
            
            // And reset the timer.
            if(g_debouncedSwState) //sw is off
            {
                count = OFF_MSEC/CHECK_MSEC;
            }
            else //sw is on
            {
                count = ON_MSEC/CHECK_MSEC;
            }
        }
    }

    return state_changed;
}

void debounceSwRoutine()
{
  byte switch_state = 0;
    
  //if switch state changed, update the state
  if(debounceSwitch(&switch_state))
  {
    if(switch_state)
    {
      debounceTimer.stop();
      
      countUpTimer.pause();
      
      //off all PWM outputs
      setPWMLEDsOff();
    }
    else
    {
      debounceTimer.stop();
      
      //begin selected test sequence
      setPWMOutput(&LED_cfg_table[g_test_selection]);

      displayLEDTestMsg(); //update the display
      countUpTimer.restart();
    }
  }
}

void setup()
{
  Serial.begin(9600);
  Wire.begin(); //need to start the Wire for I2C devices to function

  //initialize the timer to count up to max 999 hours 59 mins and 59 secs
  countUpTimer.setCounter(COUNTIMER_MAX_HOURS, COUNTIMER_MAX_MINUTES_SECONDS, COUNTIMER_MAX_MINUTES_SECONDS, countUpTimer.COUNT_UP, completeTimerCount);
  countUpTimer.setInterval(displayTimerCount, REFRESH_RATE_MSEC);

  debounceTimer.setInterval(debounceSwRoutine, CHECK_MSEC);

  pinMode(PWM_OUTPUT_EN, OUTPUT);
  pinMode(SW_INTR_PIN, INPUT);
  pinMode(EXP_INTR_PIN, INPUT);

  pwmLEDDrv.begin(); //default will set the PWM frequency to 1000Hz
  digitalWrite(PWM_OUTPUT_EN, LOW); //enable the PWM outputs to follow the ON_OFF registers

  //sets all the PWM output signal to off
  setPWMLEDsOff();

  lcd.begin(16,2); // sixteen characters across - 2 lines
  lcd.backlight();

  //intialize the rotary switch input pins
  ioExpandr.portMode0(ALLINPUT);
  ioExpandr.pinMode1(EXP_ROTARY_SW_9, HIGH);
  ioExpandr.pinMode1(EXP_ROTARY_SW_10, HIGH); //spare

  attachInterrupt(digitalPinToInterrupt(SW_INTR_PIN), swInterruptHandler, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EXP_INTR_PIN), expInterruptHandler, CHANGE);

  //display app title and version
  displayStartMsg();
}

void loop()
{
  debounceTimer.run();
  countUpTimer.run();
  
  //check test selection from rotary switch
  if(g_exp_intr_state)
  {
    g_exp_intr_state = 0;
    g_test_selection = getSWSelection();
    Serial.print("Changing LED test selection to:");
    Serial.println(g_test_selection);
  }

  //handle start_stop switch interrupt
  if(g_sw_intr_state)
  {
    g_sw_intr_state = 0;
    debounceTimer.start();
  }

  //update the display if test selection is changed
  if(g_display_selection != g_test_selection)
  {
    g_display_selection = g_test_selection;
    
    countUpTimer.pause();
    displayLEDTestMsg(); //update the display
  }
}