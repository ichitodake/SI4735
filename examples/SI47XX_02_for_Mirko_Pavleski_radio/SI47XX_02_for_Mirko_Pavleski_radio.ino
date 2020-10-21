/*
  Under construction....
  This skect is still in development and might not work properly.

  This sketch was build to work with the project "DIY Si4730 All Band Radio (LW, MW, SW, FM)" receiver from Mirko Pavleski.
  The original project can be found on https://create.arduino.cc/projecthub/mircemk/diy-si4730-all-band-radio-lw-mw-sw-fm-1894d9
  Please, follow the circuit available on that link.

  If you are using a SI4735-D60 or SI4732-A10, you can also use this sketch to add the SSB functionalities to the
  original Pavleski's project. If you are using another SI4730-D60, the SSB wil not work. But you will still have
  the SW functionalities.

  ATTENTION:

  It is important to say that this sketch was designed to work with the circuit implemented by Mirko Pavleski (see link above).
  The visual interface, control commands, band plan, and some functionalities are different if compared with the original
  sketch. Be sure you are using the SI4735 Arduino Library written by PU2CLR to run this sketch. The library used by the original
  sketch will not work here. Also, you have to install the LiquidCrystal library.

  It is  a  complete  radio  capable  to  tune  LW,  MW,  SW  on  AM  and  SSB  mode  and  also  receive  the
  regular  comercial  stations. 
  

  Features:   AM; SSB; LW/MW/SW; external mute circuit control; AGC; Attenuation gain control; 
              SSB filter; CW; AM filter; 1, 5, 10, 50 and 500KHz step on AM and 10Hhz sep on SSB

  Wire up on Arduino UNO, Pro mini and SI4735-D60
  | Device name               | Device Pin / Description      |  Arduino Pin  |
  | ----------------          | ----------------------------- | ------------  |
  |    LCD 16x2 or 20x4       |                               |               |
  |                           | D4                            |     D7        |
  |                           | D5                            |     D6        |
  |                           | D6                            |     D5        |
  |                           | D7                            |     D4        |
  |                           | RS                            |     D12       |
  |                           | E/ENA                         |     D13       |
  |                           | RW & VSS & K (16)             |    GND        |
  |                           | A (15) & VDD                  |    +Vcc       |
  |                           | VO (see 20K tripot connection)|   ---------   |
  |     SS473X                |                               |               |
  |                           | RESET (pin 15)                |      9        |
  |                           | SDIO (pin 18)                 |     A4        |
  |                           | SCLK (pin 17)                 |     A5        |
  |                           | (*1)SEN (pin 16)              |  +Vcc or GND  |
  |    Encoder                |                               |               |
  |                           | A                             |       2       |
  |                           | B                             |       3       |
  |                           | PUSH BUTTON (encoder)         |     A0/14     |


  (*1) If you are using the SI4732-A10, check the corresponding pin numbers.
  (*1) The PU2CLR SI4735 Arduino Library has resources to detect the I2C bus address automatically.
       It seems the original project connect the SEN pin to the +Vcc. By using this sketch, you do
       not need to worry about this setting.

  ATTENTION: Read the file user_manual.txt  

  Prototype documentation: https://pu2clr.github.io/SI4735/
  PU2CLR Si47XX API documentation: https://pu2clr.github.io/SI4735/extras/apidoc/html/

  By PU2CLR, Ricardo, Oct  2020.
*/

#include <SI4735.h>
#include <LiquidCrystal.h>
#include "Rotary.h"

#include "patch_init.h" // SSB patch for whole SSBRX initialization string

const uint16_t size_content = sizeof ssb_patch_content; // see patch_init.h

#define FM_BAND_TYPE 0
#define MW_BAND_TYPE 1
#define SW_BAND_TYPE 2
#define LW_BAND_TYPE 3

#define RESET_PIN 9

// Enconder PINs
#define ENCODER_PIN_A 2
#define ENCODER_PIN_B 3

// LCD 16x02 or LCD20x4 PINs
#define LCD_D7    4
#define LCD_D6    5
#define LCD_D5    6
#define LCD_D4    7
#define LCD_RS    12
#define LCD_E     13

// Buttons controllers
#define ENCODER_PUSH_BUTTON 14      // Pin A0/14
#define DUMMY_BUTTON 15

#define MIN_ELAPSED_TIME 300
#define MIN_ELAPSED_RSSI_TIME 150
#define ELAPSED_COMMAND 2500 // time to turn off the last command controlled by encoder
#define ELAPSED_CLICK 500    // time to check the click commands
#define DEFAULT_VOLUME 40    // change it for your favorite sound volume

#define FM 0
#define LSB 1
#define USB 2
#define AM 3
#define LW 4

#define SSB 1

bool bfoOn = false;
bool ssbLoaded = false;

// AGC and attenuation control
int8_t agcIdx = 0;
uint8_t disableAgc = 0;
int8_t agcNdx = 0;

uint8_t countClick = 0;

bool cmdBand = false;
bool cmdVolume = false;
bool cmdAgc = false;
bool cmdBandwidth = false;
bool cmdStep = false;
bool cmdMode = false;
bool cmdMenu = false;

int currentBFO = 0;
uint8_t seekDirection = 1; // Tells the SEEK direction (botton or upper limit)

long elapsedRSSI = millis();
long elapsedButton = millis();
long elapsedCommand = millis();
long elapsedClick = millis();

// Encoder control variables
volatile int encoderCount = 0;

// Some variables to check the SI4735 status
uint16_t currentFrequency;

uint8_t currentBFOStep = 10;


const char * menu[] = {"<Select>", "STEP", "MODE", "BW", "AGG/ATT", "VOLUME", "BFO"};
int8_t menuIdx = 0;
const int lastMenu = 6;
int8_t currentMenuCmd = -1;

typedef struct
{
  uint8_t idx;      // SI473X device bandwitdth index
  const char *desc; // bandwitdth description
} Bandwitdth;

int8_t bwIdxSSB = 4;
Bandwitdth bandwitdthSSB[] = {{4, "0.5"},
  {5, "1.0"},
  {0, "1.2"},
  {1, "2.2"},
  {2, "3.0"},
  {3, "4.0"}
};

int8_t bwIdxAM = 4;
Bandwitdth bandwitdthAM[] = {{4, "1.0"},
  {5, "1.8"},
  {3, "2.0"},
  {6, "2.5"},
  {2, "3.0"},
  {1, "4.0"},
  {0, "6.0"}
};

const char *bandModeDesc[] = {"FM ", "LSB", "USB", "AM "};
uint8_t currentMode = FM;

uint16_t currentStep = 1;

/*
   Band data structure
*/
typedef struct
{
  const char *bandName; // Band description
  uint8_t bandType;     // Band type (FM, MW or SW)
  uint16_t minimumFreq; // Minimum frequency of the band
  uint16_t maximumFreq; // maximum frequency of the band
  uint16_t currentFreq; // Default frequency or current frequency
  uint16_t currentStep; // Defeult step (increment and decrement)
} Band;

/*
   Band table
   You can customize this table for your own band plan
*/
Band band[] = {
  {"FM ", FM_BAND_TYPE, 6400, 10800,  10390, 10},
  {"MW1", MW_BAND_TYPE,   150,  1720,   810, 10},
  {"MW2", MW_BAND_TYPE,  1700,  3500,  2500, 5},
  {"SW2", SW_BAND_TYPE,  3500,  7000,  4500, 5},
  {"SW3", SW_BAND_TYPE,  7000,  9000,  7200, 5},
  {"SW4", SW_BAND_TYPE,  9000, 11000,  9500, 5},
  {"SW5", SW_BAND_TYPE, 11100, 13000, 11900, 5},
  {"SW5", SW_BAND_TYPE, 13000, 15000, 13500, 5},
  {"SW6", SW_BAND_TYPE, 15000, 17000, 15300, 5},
  {"SW7", SW_BAND_TYPE, 18000, 22000, 21500, 5},
  {"11M", SW_BAND_TYPE, 26000, 28000, 27500, 1},
  {"10M", SW_BAND_TYPE, 28000, 30000, 28400, 1},
  {"ALL", SW_BAND_TYPE, 150, 30000, 15000, 1}
}; // Super band: 150KHz to 30MHz

const int lastBand = (sizeof band / sizeof(Band)) - 1;
int bandIdx = 0;

int tabStep[] = {1, 5, 10, 50, 100, 500, 1000};
const int lastStep = (sizeof tabStep / sizeof(int)) - 1;
int idxStep = 0;

uint8_t rssi = 0;
uint8_t volume = DEFAULT_VOLUME;

// Devices class declarations
Rotary encoder = Rotary(ENCODER_PIN_A, ENCODER_PIN_B);

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

SI4735 rx;

void setup()
{
  // Encoder pins
  pinMode(ENCODER_PUSH_BUTTON, INPUT_PULLUP);
  lcd.begin(16, 2);
  // Splash - Change it for your introduction text.
  lcd.setCursor(0, 0);
  lcd.print("PU2CLR-SI4735");
  lcd.setCursor(0, 1);
  lcd.print("Arduino Library");
  Flash(2500);
  lcd.setCursor(0, 0);
  lcd.print("All in One Radio");
  lcd.setCursor(0, 1);
  lcd.print("By RICARDO/2020");
  Flash(4500);

  // Encoder interrupt
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

  rx.getDeviceI2CAddress(RESET_PIN); // Looks for the I2C bus address and set it.  Returns 0 if error
  rx.setup(RESET_PIN, -1, 1, SI473X_ANALOG_DIGITAL_AUDIO); // Starts FM mode and ANALOG and DIGITAL audio mode.

  // Set up the radio for the current band (see index table variable bandIdx )
  useBand();
  rx.setVolume(volume);
  showStatus();
}

/**
   Cleans the screen with a visual effect.
*/
void Flash (int d)
{
  delay(d);
  lcd.clear();
  lcd.noDisplay();
  delay(500);
  lcd.display();
}

/**
    Set all command flags to false
    When all flags are disabled (false), the encoder controls the frequency
*/
void disableCommands()
{
  cmdBand = false;
  bfoOn = false;
  cmdVolume = false;
  cmdAgc = false;
  cmdBandwidth = false;
  cmdStep = false;
  cmdMode = false;
  cmdMenu = false;
  countClick = 0;

  lcd.setCursor(6, 0);
  lcd.print("   ");
}

/**
     Reads encoder via interrupt
     Use Rotary.h and  Rotary.cpp implementation to process encoder via interrupt
*/
void rotaryEncoder()
{ // rotary encoder events
  uint8_t encoderStatus = encoder.process();

  if (encoderStatus)
    encoderCount = (encoderStatus == DIR_CW) ? 1 : -1;
}

/**
    Shows frequency information on Display
*/
void showFrequency()
{
  char tmp[15];
  char bufferDisplay[15];
  char * unit;
  // It is better than use dtostrf or String to save space.
  sprintf(tmp, "%5.5u", currentFrequency);

  bufferDisplay[0] = (tmp[0] == '0') ? ' ' : tmp[0];
  bufferDisplay[1] = tmp[1];
  if (rx.isCurrentTuneFM())
  {
    bufferDisplay[2] = tmp[2];
    bufferDisplay[3] = '.';
    bufferDisplay[4] = tmp[3];
    unit = (char *) " MHz";
  }
  else
  {
    if ( currentFrequency  < 1000 ) {
      bufferDisplay[1] = ' ';
      bufferDisplay[2] = tmp[2] ;
      bufferDisplay[3] = tmp[3];
      bufferDisplay[4] = tmp[4];
    } else {
      bufferDisplay[2] = tmp[2];
      bufferDisplay[3] = tmp[3];
      bufferDisplay[4] = tmp[4];
    }
    unit = (char *) " KHz";
  }
  bufferDisplay[5] = '\0';
  strcat(bufferDisplay,unit);
  lcd.setCursor(0, 1);
  lcd.print(bufferDisplay);

  showMode();

}


void showMode() {

  char * bandMode;
  
  if (currentFrequency < 520)
    bandMode = (char *) "LW  ";
  else
    bandMode = (char *) bandModeDesc[currentMode];

  lcd.setCursor(0, 0);
  lcd.print(bandMode);

}


/**
    This function is called by the seek function process.
*/
void showFrequencySeek(uint16_t freq)
{
  currentFrequency = freq;
  showFrequency();
}

/**
     Show some basic information on display
*/
void showStatus()
{
  lcd.clear();
  showFrequency();
  showRSSI();
}

/**
   Shows the current Bandwitdth status
*/
void showBandwitdth()
{
  char bufferDisplay[15];
  // Bandwidth
  if (currentMode == LSB || currentMode == USB || currentMode == AM)
  {
    char *bw;
    if (currentMode == AM)
      bw = (char *)bandwitdthAM[bwIdxAM].desc;
    else
      bw = (char *)bandwitdthSSB[bwIdxSSB].desc;
    sprintf(bufferDisplay, "BW: %s", bw);
  }
  else
    bufferDisplay[0] = '\0';

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(bufferDisplay);
}

/**
    Shows the current RSSI and SNR status
*/
void showRSSI()
{
  int rssiAux = 0;
  char sMeter[7];
  if (rssi < 2)
    rssiAux = 4;
  else if (rssi < 4)
    rssiAux = 5;
  else if (rssi < 12)
    rssiAux = 6;
  else if (rssi < 25)
    rssiAux = 7;
  else if (rssi < 50)
    rssiAux = 8;
  else
    rssiAux = 9;

  sprintf(sMeter, "S%1.1u%c", rssiAux,(rssi >=60)? '+':' ');

  lcd.setCursor(13, 1);
  lcd.print(sMeter);

  if (currentMode == FM)
  {
    lcd.setCursor(10, 0);
    lcd.print((rx.getCurrentPilot()) ? "STEREO" : "  MONO");
  }

}

/**
     Shows the current AGC and Attenuation status
*/
void showAgcAtt()
{
  char sAgc[15];

  lcd.clear();
  
  rx.getAutomaticGainControl();
  if (agcNdx == 0 && agcIdx == 0)
    strcpy(sAgc, "AGC ON");
  else
    sprintf(sAgc, "ATT: %2.2d", agcNdx);
    
  lcd.setCursor(0, 0);
  lcd.print(sAgc);
}

/**
    Shows the current step
*/
void showStep()
{
    char stAux[10];
    sprintf(stAux,"STEP: %4u",currentStep);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(stAux);
 }

/**
   Shows the current BFO value
*/
void showBFO()
{
  char bfo[10];

  if (currentBFO > 0)
    sprintf(bfo,"BFO: +%4.4d",currentBFO);
  else
    sprintf(bfo, "BFO: %4.4d", currentBFO);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(bfo);
  elapsedCommand = millis();
}

/*
   Shows the volume level on LCD
*/
void showVolume()
{
    char volAux[12];
    sprintf(volAux,"VOLUME: %2u",rx.getVolume());
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(volAux);  
}

/**
    Sets Band up (1) or down (!1)
*/
void setBand(int8_t up_down)
{
  band[bandIdx].currentFreq = currentFrequency;
  band[bandIdx].currentStep = currentStep;
  if (up_down == 1)
    bandIdx = (bandIdx < lastBand) ? (bandIdx + 1) : 0;
  else
    bandIdx = (bandIdx > 0) ? (bandIdx - 1) : lastBand;
  useBand();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}

/**
    This function loads the contents of the ssb_patch_content array into the CI (Si4735) and starts the radio on
    SSB mode.
    See also loadPatch implementation in the SI4735 Arduino Library (SI4735.h/SI4735.cpp)
*/
void loadSSB()
{
  rx.reset();
  rx.queryLibraryId(); // Is it really necessary here? I will check it.
  rx.patchPowerUp();
  delay(50);
  rx.setI2CFastMode(); // Recommended
  // rx.setI2CFastModeCustom(500000); // It is a test and may crash.
  rx.downloadPatch(ssb_patch_content, size_content);
  rx.setI2CStandardMode(); // goes back to default (100KHz)

  // Parameters
  // AUDIOBW - SSB Audio bandwidth; 0 = 1.2KHz (default); 1=2.2KHz; 2=3KHz; 3=4KHz; 4=500Hz; 5=1KHz;
  // SBCUTFLT SSB - side band cutoff filter for band passand low pass filter ( 0 or 1)
  // AVC_DIVIDER  - set 0 for SSB mode; set 3 for SYNC mode.
  // AVCEN - SSB Automatic Volume Control (AVC) enable; 0=disable; 1=enable (default).
  // SMUTESEL - SSB Soft-mute Based on RSSI or SNR (0 or 1).
  // DSP_AFCDIS - DSP AFC Disable or enable; 0=SYNC MODE, AFC enable; 1=SSB MODE, AFC disable.
  rx.setSSBConfig(bandwitdthSSB[bwIdxSSB].idx, 1, 0, 0, 0, 1);
  delay(25);
  ssbLoaded = true;
  showStatus();
}

/**
    Switch the radio to current band
*/
void useBand()
{
  if (band[bandIdx].bandType == FM_BAND_TYPE)
  {
    currentMode = FM;
    rx.setTuneFrequencyAntennaCapacitor(0);
    rx.setFM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
    rx.setSeekFmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);
    bfoOn = ssbLoaded = false;
  }
  else
  {
    // set the tuning capacitor for SW or MW/LW
    rx.setTuneFrequencyAntennaCapacitor((band[bandIdx].bandType == MW_BAND_TYPE || band[bandIdx].bandType == LW_BAND_TYPE) ? 0 : 1);

    if (ssbLoaded)
    {
      rx.setSSB(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep, currentMode);
      rx.setSSBAutomaticVolumeControl(1);
    }
    else
    {
      currentMode = AM;
      rx.setAM(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq, band[bandIdx].currentFreq, band[bandIdx].currentStep);
      bfoOn = false;
    }
    rx.setAmSoftMuteMaxAttenuation(0); // Disable Soft Mute for AM or SSB
    rx.setAutomaticGainControl(disableAgc, agcNdx);
    rx.setSeekAmLimits(band[bandIdx].minimumFreq, band[bandIdx].maximumFreq);               // Consider the range all defined current band
    rx.setSeekAmSpacing((band[bandIdx].currentStep > 10) ? 10 : band[bandIdx].currentStep); // Max 10KHz for spacing
  }
  delay(100);
  currentFrequency = band[bandIdx].currentFreq;
  currentStep = band[bandIdx].currentStep;
  rssi = 0;
  showStatus();
  showCommandStatus();
}

/**
    Switches the Bandwidth
*/
void doBandwidth(int8_t v)
{
  if (currentMode == LSB || currentMode == USB)
  {
    bwIdxSSB = (v == 1) ? bwIdxSSB + 1 : bwIdxSSB - 1;

    if (bwIdxSSB > 5)
      bwIdxSSB = 0;
    else if (bwIdxSSB < 0)
      bwIdxSSB = 5;

    rx.setSSBAudioBandwidth(bandwitdthSSB[bwIdxSSB].idx);
    // If audio bandwidth selected is about 2 kHz or below, it is recommended to set Sideband Cutoff Filter to 0.
    if (bandwitdthSSB[bwIdxSSB].idx == 0 || bandwitdthSSB[bwIdxSSB].idx == 4 || bandwitdthSSB[bwIdxSSB].idx == 5)
      rx.setSBBSidebandCutoffFilter(0);
    else
      rx.setSBBSidebandCutoffFilter(1);
  }
  else if (currentMode == AM)
  {
    bwIdxAM = (v == 1) ? bwIdxAM + 1 : bwIdxAM - 1;

    if (bwIdxAM > 6)
      bwIdxAM = 0;
    else if (bwIdxAM < 0)
      bwIdxAM = 6;

    rx.setBandwidth(bandwitdthAM[bwIdxAM].idx, 1);
  }
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  showBandwitdth();
  // showCommandStatus();
  elapsedCommand = millis();
}

void showCommandStatus()
{
    lcd.setCursor(6, 0);
    lcd.print("cmd");
}

void showMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MENU");
  lcd.setCursor(0, 1);
  lcd.print(menu[menuIdx]);
}

/**
    Deal with AGC and attenuattion
*/
void doAgc(int8_t v) {

  agcIdx = (v == 1) ? agcIdx + 1 : agcIdx - 1;
  if (agcIdx < 0 )
    agcIdx = 35;
  else if ( agcIdx > 35)
    agcIdx = 0;

  disableAgc = (agcIdx > 0); // if true, disable AGC; esle, AGC is enable

  if (agcIdx > 1)
    agcNdx = agcIdx - 1;
  else
    agcNdx = 0;

  rx.setAutomaticGainControl(disableAgc, agcNdx); // if agcNdx = 0, no attenuation
  showAgcAtt();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}

/**
    Gets the current step index.
*/
int getStepIndex(int st)
{
  for (int i = 0; i < lastStep; i++)
  {
    if (st == tabStep[i])
      return i;
  }
  return 0;
}

/**
    Switches the current step
*/
void doStep(int8_t v)
{
  idxStep = (v == 1) ? idxStep + 1 : idxStep - 1;
  if (idxStep > lastStep)
    idxStep = 0;
  else if (idxStep < 0)
    idxStep = lastStep;

  currentStep = tabStep[idxStep];

  rx.setFrequencyStep(currentStep);
  band[bandIdx].currentStep = currentStep;
  rx.setSeekAmSpacing((currentStep > 10) ? 10 : currentStep); // Max 10KHz for spacing
  showStep();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  // showCommandStatus();
  elapsedCommand = millis();
}

/**
    Switches to the AM, LSB or USB modes
*/
void doMode(int8_t v)
{
  if (currentMode != FM)
  {
    if (v == 1)  { // clockwise
      if (currentMode == AM)
      {
        // If you were in AM mode, it is necessary to load SSB patch (avery time)
        loadSSB();
        currentMode = LSB;
      }
      else if (currentMode == LSB)
        currentMode = USB;
      else if (currentMode == USB)
      {
        currentMode = AM;
        bfoOn = ssbLoaded = false;
      }
    } else { // and counterclockwise
      if (currentMode == AM)
      {
        // If you were in AM mode, it is necessary to load SSB patch (avery time)
        loadSSB();
        currentMode = USB;
      }
      else if (currentMode == USB)
        currentMode = LSB;
      else if (currentMode == LSB)
      {
        currentMode = AM;
        bfoOn = ssbLoaded = false;
      }
    }
    // Nothing to do if you are in FM mode
    band[bandIdx].currentFreq = currentFrequency;
    band[bandIdx].currentStep = currentStep;
    useBand();
  }

  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  // showCommandStatus();
  elapsedCommand = millis();
}

/**
    Find a station. The direction is based on the last encoder move clockwise or counterclockwise
*/
void doSeek()
{
  rx.seekStationProgress(showFrequencySeek, seekDirection);
  currentFrequency = rx.getFrequency();
}


void doVolume( int8_t v ) {
   if ( v == 1) 
      rx.volumeUp();
   else
      rx.volumeDown();  

   showVolume(); 
   
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();      
}

void doMenu( int8_t v) {
  int8_t lastOpt;

  menuIdx = (v == 1) ? menuIdx + 1 : menuIdx - 1;
  lastOpt = ((currentMode == LSB || currentMode == USB))? lastMenu: lastMenu - 1;

  if (menuIdx > lastOpt)
    menuIdx = 0;
  else if (menuIdx < 0)
    menuIdx = lastOpt;

  showMenu();
  delay(MIN_ELAPSED_TIME); // waits a little more for releasing the button.
  elapsedCommand = millis();
}


void doCurrentMenuCmd() {

  disableCommands();
   
  switch (currentMenuCmd) {
    case 1:                 // STEP
      cmdStep = true;
      showStep();
      break;
    case 2:                 // MODE
      cmdMode = true;
      lcd.clear();
      showMode();
      break;
    case 3:                 // BW
      cmdBandwidth = true;
      showBandwitdth();
      break;
    case 4:                 // AGC/ATT
      cmdAgc = true;
      showAgcAtt();
      break;
    case 5:                 // VOLUME
      cmdVolume = true;
      showVolume();
      break;
    case 6:
      bfoOn = !bfoOn;
      if ((currentMode == LSB || currentMode == USB))
        showBFO();
      showFrequency();
    default:
      break;
  }
  currentMenuCmd = -1;
  elapsedCommand = millis();
}



void loop()
{
  // Check if the encoder has moved.
  if (encoderCount != 0)
  {
    if (bfoOn & (currentMode == LSB || currentMode == USB))
    {
      currentBFO = (encoderCount == 1) ? (currentBFO + currentBFOStep) : (currentBFO - currentBFOStep);
      rx.setSSBBfo(currentBFO);
      showBFO();
    }
    else if (cmdMenu)
      doMenu(encoderCount);
    else if (cmdMode)
      doMode(encoderCount);
    else if (cmdStep)
      doStep(encoderCount);
    else if (cmdAgc)
      doAgc(encoderCount);
    else if (cmdBandwidth)
      doBandwidth(encoderCount);
    else if (cmdVolume) 
      doVolume(encoderCount);  
    else if (cmdBand)
      setBand(encoderCount);
    else
    {
      if (encoderCount == 1)
      {
        rx.frequencyUp();
        seekDirection = 1;
      }
      else
      {
        rx.frequencyDown();
        seekDirection = 0;
      }
      // Show the current frequency only if it has changed
      currentFrequency = rx.getFrequency();
      showFrequency();
    }
    encoderCount = 0;
    // elapsedCommand = millis();
  }
  else
  {
    if ( digitalRead(ENCODER_PUSH_BUTTON) == LOW ) {
      countClick++;
      if (  (millis() - elapsedClick) > ELAPSED_CLICK ) {
        if (cmdMenu ) {
          currentMenuCmd = menuIdx;
          doCurrentMenuCmd();
        } else if ( countClick == 1) { // If just one click, you can select the band by rotating the encoder
          cmdBand = !cmdBand;
          delay(MIN_ELAPSED_TIME);
          elapsedCommand = millis();
        } else { // GO to MENU if more than one click in less than 1/2 seconds.
          cmdMenu = !cmdMenu;
          if (cmdMenu) showMenu();
          delay(MIN_ELAPSED_TIME);
          elapsedCommand = millis();
        }
      }
    }

  }

  // Show RSSI status only if this condition has changed
  if ((millis() - elapsedRSSI) > MIN_ELAPSED_RSSI_TIME * 6)
  {
    rx.getCurrentReceivedSignalQuality();
    int aux = rx.getCurrentRSSI();
    if (rssi != aux &&  !(cmdStep | cmdMode | cmdBandwidth | cmdAgc | cmdVolume ) )
    {
      rssi = aux;
      showRSSI();
    }
    elapsedRSSI = millis();
  }

  // Disable commands control
  if ((millis() - elapsedCommand) > ELAPSED_COMMAND)
  {
    if ((currentMode == LSB || currentMode == USB))
    {
      bfoOn = false;
      showBFO();
    }
    showStatus();
    disableCommands();
    elapsedCommand = millis();
  }
  delay(5);
}
