#include <Arduino.h>
#include <SPIFFS.h>

// Tweak these as desired
#define SERIAL_EN false       // Disable when we're ready for "production"
#define PWM_FREQ 80000        // This isn't exactly what it'll be, but that's totally fine.
#define SAMPLE_FREQ 8000      // Input and output samples/second
#define CLOCK_DIVIDER 80      // the APB clock (used to time stuff) should be running at 80MHz. This divider lets us use smaller numbers :)
#define SAMPLE_COUNT_FUDGE  0 // ehhh nobody's perfect. Our sample rate maybe slower than ideal and this might help
#define FORMAT_SPIFFS false   // only needs to happen once ever

// don't change this one
#define COUNT_PER_SAMPLE (APB_CLK_FREQ / CLOCK_DIVIDER / SAMPLE_FREQ) + SAMPLE_COUNT_FUDGE // Just a pre-processor calculation to simplify runtime

// Play state variable enumerations - yes, this is dumb, but whatever
#define PLAY_IDLE 0
#define ABOUT_TO_PLAY 1
#define PLAYING 2
#define FINISHED_PLAYING 3

uint8_t analogOut = 10;
uint8_t sayButton = 8;
uint8_t playButton = 20;
uint8_t amplifierShutdown = 9;
uint8_t microphone = 2;
hw_timer_t *timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// used for button state and edge detection
boolean sayPressed = false;
boolean wasSayPressed = false;
boolean sayJustPressed = false;
boolean sayJustReleased = false;
boolean playPressed = false;
boolean wasPlayPressed = false;
boolean playJustPressed = false;

// used to store data in memory temporarily
uint8_t recordBuffer[2048];
uint16_t recordBufferSize;
uint8_t playBuffer[2048];
uint16_t playBufferSize;

// used for SPIFFS formatting
bool formatted = false;

// used to keep time
uint64_t useconds;

// first time that we write to a file, we need to use a different function
boolean firstWrite;

// state variable for playing sounds
uint8_t playState;

// and the file handle for playing sounds
File playFile;

///////////////////////////////////////////////////
//  THIS SECTION IS FOR SPIFFS   //////////////////
///////////////////////////////////////////////////

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.print(file.name());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
      if (levels)
      {
        listDir(fs, file.path(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.print(file.size());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available())
  {
    Serial.printf("%d\t", file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char *path, const uint8_t *buffer, const uint16_t buffsize)
{
  Serial.printf("Writing file: %s with buffer size: %d\n", path, buffsize);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (!file.write(buffer, buffsize))
  {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const uint8_t *buffer, const uint16_t buffsize)
{
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (!file.write(buffer, buffsize))
  {
    Serial.println("Append failed");
  }
  file.close();
}

void deleteFile(fs::FS &fs, const char *path)
{
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path))
  {
    Serial.println("File deleted");
  }
  else
  {
    Serial.println("Delete failed");
  }
}

/////////////////////////////////////////////////////////////////
//   Ok. The SPIFFS section is over now.  ///////////////////////
/////////////////////////////////////////////////////////////////

void setup()
{
  // Serial first
  if (SERIAL_EN)
  {
    Serial.begin();
    delay(5000); // delay so we have time to connect
  }

  // Pinmodes
  pinMode(analogOut, OUTPUT);
  pinMode(sayButton, INPUT_PULLUP);
  pinMode(playButton, INPUT_PULLUP);
  pinMode(amplifierShutdown, OUTPUT);
  pinMode(microphone, ANALOG);

  // turn off the amplifier
  digitalWrite(amplifierShutdown, LOW);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS mount Failed");
    return;
  }
  if (FORMAT_SPIFFS)
  {
    formatted = SPIFFS.format();
    if (formatted)
    {
      Serial.println("\n\nSuccess formatting");
    }
    else
    {
      Serial.println("\n\nError formatting");
    }
  }

  // set up the ADC stuff
  analogReadResolution(8);

  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Set the PWM frequency, get the actual PWM frequency back
  uint32_t clockFreq = sigmaDeltaSetup(analogOut, 0, PWM_FREQ);
  // Print out the frequency via serial
  if (clockFreq == 0)
  {
    (SERIAL_EN) ? Serial.println("Could not set PWM clock frequency") : 0;
  }
  else
  {
    (SERIAL_EN) ? Serial.printf("PWM clock frequency: %d\n", clockFreq) : 0;
  }
  // No output yet
  sigmaDeltaWrite(0, 0);

  // Setup timer at the sample rate
  timer = timerBegin(0, CLOCK_DIVIDER, true);

  // initialize a few variables
  recordBufferSize = 0;
  firstWrite = true;
  playState = PLAY_IDLE;
}

void loop()
{
  // put your main code here, to run repeatedly:
  //(SERIAL_EN)? Serial.printf("Last time: %d\n",timerReadMicros(timer)-useconds):0;
  // capture current time
  useconds = timerReadMicros(timer);
  //(SERIAL_EN)? Serial.printf("This time: %d\n",timerReadMicros(timer)-useconds):0;
  // button push states and edges
  wasSayPressed = sayPressed;
  sayPressed = !digitalRead(sayButton);
  sayJustPressed = !wasSayPressed && sayPressed;
  sayJustReleased = wasSayPressed && !sayPressed;
  wasPlayPressed = playPressed;
  playPressed = !digitalRead(playButton);
  playJustPressed = !wasPlayPressed && playPressed;

  //-------------------------//
  // Say button stuff        //
  //-------------------------//
  // DON'T DO ANYTHING WHILE PLAYING
  if (playState == PLAY_IDLE)
  {
    // Delete the old data if the say button was just pressed
    if (sayJustPressed)
    {
      deleteFile(SPIFFS, "/audio.wav");
      (SERIAL_EN) ? Serial.printf("Recording now!\n") : 0;
      // reset the firstWrite var!
      firstWrite = true;
    }
    // Record for as long as the say button is pressed
    if (sayPressed)
    {
      // stuff it in the buffer
      recordBuffer[recordBufferSize] = analogRead(microphone);
      (SERIAL_EN) ? Serial.printf("Sample: %d\n", recordBuffer[recordBufferSize]) : 0;
      // and then index the next spot in the buffer
      // this is basically a pointer
      recordBufferSize++;
    }
    // Every now and then we need to dump the data to a file - oh also when say is released.
    if (recordBufferSize >= 1024 || sayJustReleased)
    {
      (SERIAL_EN) ? Serial.printf("Hold on - let me write this down.\nBuffer size: %d\n", recordBufferSize) : 0;
      // different funtion for the first write vs appends
      (firstWrite) ? writeFile(SPIFFS, "/audio.wav", recordBuffer, recordBufferSize) : appendFile(SPIFFS, "/audio.wav", recordBuffer, recordBufferSize);
      // ok, now only append.
      firstWrite = false;
      // fun fact - this clears the buffer by resetting the "pointer"
      recordBufferSize = 0;
    }
    // When the say button is released, we can helpfully show we recorded something via serial.
    if (sayJustReleased && SERIAL_EN)
    {
      Serial.printf("Stopped recording. Here's the results\n");
      readFile(SPIFFS, "/audio.wav");
    }
  }
  //-------------------------//
  // End of Say button stuff //
  //-------------------------//

  //--------------------------//
  // Play button stuff        //
  //--------------------------//
  // idle:
  if (playState == PLAY_IDLE)
  {
    // state = aboutToPlay if play button just pressed
    if (playJustPressed)
    {
      playState = ABOUT_TO_PLAY;
    }
  }

  // aboutToPlay:
  if (playState == ABOUT_TO_PLAY)
  {
    (SERIAL_EN) ? Serial.printf("Oh boy, let's get ready to play!\n") : 0;
    // open the file
    playFile = SPIFFS.open("/audio.wav");
    // turn on the amp
    digitalWrite(amplifierShutdown, HIGH);
    // state = playing
    playState = PLAYING;
  }

  // playing:
  if (playState == PLAYING)
  {
    if (playFile.available())
    {
      // read one byte from the file
      static char playBuffer;
      playFile.readBytes(&playBuffer, 1);
      (SERIAL_EN) ? Serial.printf("%d\t",playBuffer) : 0;
      // and write that byte to sigmaDelta
      sigmaDeltaWrite(0, (uint8_t)playBuffer);
    }
    else
    {
      // stop outputting
      sigmaDeltaWrite(0,0);
      // state = finishedPlaying if no file left
      playState = FINISHED_PLAYING;
    }
  }

  // finishedPlaying:
  if (playState == FINISHED_PLAYING)
  {
    // turn off the amp
    digitalWrite(amplifierShutdown, LOW);
    // close the file
    playFile.close();
    // state = idle
    playState = PLAY_IDLE;
  }
  // //--------------------------//
  // End of Play button stuff //
  //--------------------------//

  // Spin until we should loop again
  while (useconds + COUNT_PER_SAMPLE > timerReadMicros(timer))
  {
    delayMicroseconds(1);
  };
}