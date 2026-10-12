/**
*  Polargraph Server for ESP32 based microcontroller boards.
*  Written by Sandy Noble
*  Released under GNU License version 3.
*  http://www.polargraph.co.uk
*  https://github.com/euphy/polargraph_server_polarshield_esp32


Specific features for Polarshield / arduino mega.
Implementation.

So this file is the interface for the extra features available in the
mega/polarshield version of the polargraph.

*/

/*  Implementation of executeCommand for MEGA-sized boards that
have command storage features. */
void impl_processCommand(String inCmd, String inParam1, String inParam2, String inParam3, String inParam4, int inNoOfParams)
{
  // lcd_echoLastCommandToDisplay(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams, "usb:");

  // The MEGA can change from LIVE to STORING modes
  // LIVE is where it acts on the commands,
  // STORING is where the commands are pushed into a file on an SD card.

  // If the command is to switch between these modes, then these are ALWAYS
  // executed as if LIVE.
  // You can't store a mode-change command to SD.
  if (inCmd.startsWith(CMD_MODE_STORE_COMMANDS)
   || inCmd.startsWith(CMD_MODE_LIVE))
  {
    Serial.println("Changing mode.");
    impl_executeCommand(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams);
  }
  // else execute / store the command
  else if (storeCommands)
  {
    Serial.print(F("Storing command:"));
    Serial.println(inCmd);
    sd_storeCommand(inCmd);
  }
  else
  {
    impl_executeCommand(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams);
  }
}

/**
*  This includes the extra commands the MEGA is capable of handling.
*  It tries to run the command using the core executeBasicCommand
*  first, but if that doesn't work, then it will go through
*  it's own decision tree to try and run one of the additional
*  routines.
*/
void impl_executeCommand(String inCmd, String inParam1, String inParam2, String inParam3, String inParam4, int inNoOfParams)
{
  if (exec_executeBasicCommand(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams))
  {
    // that's nice, it worked
    Serial.print(MSG_COMPLETE_STR);
    Serial.println(lastParsedCommandRaw);
  }
  else if (inCmd.startsWith(CMD_DRAWCIRCLEPIXEL))
    curves_pixel_drawCircularPixel();
  else if (inCmd.startsWith(CMD_TESTPENWIDTHSCRIBBLE))
    impl_pixel_testPenWidthScribble();
  else if (inCmd.startsWith(CMD_DRAWSAWPIXEL))
    impl_pixel_drawSawtoothPixel();
  else if (inCmd.startsWith(CMD_DRAWDIRECTIONTEST))
    impl_exec_drawTestDirectionSquare();
  else if (inCmd.startsWith(CMD_MODE_STORE_COMMANDS))
    impl_exec_changeToStoreCommandMode();
  else if (inCmd.startsWith(CMD_MODE_LIVE))
    impl_exec_changeToLiveCommandMode();
  else if (inCmd.startsWith(CMD_MODE_EXEC_FROM_STORE))
    impl_exec_execFromStore();
 else if (inCmd.startsWith(CMD_SET_ROVE_AREA))
   rove_setRoveArea();
  else if (inCmd.startsWith(CMD_CHANGELENGTH_RELATIVE))
    exec_changeLength();
 else if (inCmd.startsWith(CMD_DRAW_NORWEGIAN))
   rove_drawNorwegianFromFile();
 else if (inCmd.startsWith(CMD_DRAW_NORWEGIAN_OUTLINE))
   rove_drawRoveAreaFittedToImage();
  else if (inCmd.startsWith(CMD_AUTO_CALIBRATE))
    calibrate_doCalibration();
  else if (inCmd.startsWith(CMD_SET_DEBUGCOMMS))
    impl_setDebugComms();
  else
  {
    comms_unrecognisedCommand(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams);
    // comms_ready();
  }
}

/* Polarshield implementation of runBackgroundProcesses. 
*  This is basically stuff to do with the screen.
*/
void impl_serviceUi()
{
  impl_runBackgroundTouchProcesses();
  impl_runBackgroundDrawProcesses();

  long motorCutoffTime = millis() - lastOperationTime;
  if ((automaticPowerDown) && (powerIsOn) && (motorCutoffTime > motorIdleTimeBeforePowerDown))
  {
    Serial.println("Powering down because of inactivity.");
    buttons_actions_motorsOff();
  }
}

void impl_runBackgroundProcesses()
{
  #ifdef DEBUG_FUNCTION_BOUNDARIES
  printf("\n\n\tEnter %s at %d\n", __FUNCTION__, millis());
  #endif

  // File playback calls impl_serviceUi() itself, between SD reads.
  // This task also calling it clocks the touch controller while the
  // card is using the same SPI pins.
  if (!currentlyDrawingFromFile) {
    impl_serviceUi();
  }
 
  #ifdef DEBUG_FUNCTION_BOUNDARIES
  printf("\tExit %s at %d\n", __FUNCTION__, millis());
  #endif
}

void impl_runBackgroundTouchProcesses()
{
  touch_input();
  touch_doScheduledEnable();
}

void impl_runBackgroundDrawProcesses()
{
  lcd_doScheduledRedraw();
}

void impl_loadMachineSpecFromEeprom()
{}

void impl_exec_execFromStore()
{
  String fileToExec = inParam1;
  if (fileToExec != "")
  {
    currentlyDrawingFromFile = true;
    Serial.print("Filename to read from: ");
    Serial.println(fileToExec);
    commandFilename = fileToExec;
    impl_exec_execFromStore(commandFilename);
    // The playback function clears currentlyDrawingFromFile when it
    // finishes or is stopped. Leaving it true here made the machine
    // look busy after a stored C34 had already ended.
  }
  else
  {
    Serial.println("No filename supplied to read from.");
  }

}

// Drop leading and trailing spaces in a stored command line. The serial
// parser never sees those; files sometimes have them, and trim() used to
// do this on the Arduino String.
static void playback_trimLine(char *command, int *length)
{
  int start = 0;
  int end = *length;
  while (start < end && (command[start] == ' ' || command[start] == '\t')) {
    start++;
  }
  while (end > start && (command[end - 1] == ' ' || command[end - 1] == '\t')) {
    end--;
  }
  int kept = end - start;
  if (start > 0 && kept > 0) {
    memmove(command, command + start, kept);
  }
  command[kept] = 0;
  *length = kept;
}

void impl_exec_execFromStore(String inFilename)
{
  // Playback is synchronous and not re-entrant. A second entry (another
  // touch, or a stored C34) would open the card again and share inCmd
  // with the caller.
  static boolean playbackActive = false;
  if (playbackActive) {
    Serial.println("Already drawing from a file.");
    return;
  }

  if (inFilename == "")
  {
    Serial.println("No filename supplied to read from.");
    currentlyDrawingFromFile = false;
    return;
  }
  playbackActive = true;

  String noBlanks = "";
  // remove blanks
  for (int i = 0; i<inFilename.length(); i++)
  {
    if (inFilename[i] != ' ')
      noBlanks = noBlanks + inFilename[i];
  }

  char filename[noBlanks.length()+1];
  noBlanks.toCharArray(filename, noBlanks.length()+1);
#ifdef DEBUG_SD
  Serial.print("Array to read from: ");
  Serial.println(filename);
#endif

  // The directory listing keeps `root` open for the whole session. The
  // ESP32 SD driver has corrupted sequential reads when a second file is
  // open at the same time, which showed up as lines drawn in the wrong
  // place. Close it for the duration of playback.
  boolean reopenRoot = false;
  if (root) {
    reopenRoot = true;
    root.close();
  }

  sd_deselectSharedSpiDevices();
  File readFile = SD.open(filename, FILE_READ);
  if (!readFile && filename[0] != '/') {
    String slashName = String("/") + noBlanks;
    char slashFile[slashName.length() + 1];
    slashName.toCharArray(slashFile, slashName.length() + 1);
    readFile = SD.open(slashFile, FILE_READ);
  }

  // Hold the serial command slot for the whole file. The comms ticker and
  // loop() share inCmd / currentCommand with this parser; letting a USB
  // command run in the middle overwrites the move that was just read.
  boolean alreadyExecuting = currentlyExecutingACommand;
  currentlyExecutingACommand = true;

  if (!readFile)
  {
    Serial.println("Couldn't find that file, btw.");
    currentlyDrawingFromFile = false;
    currentlyExecutingACommand = alreadyExecuting;
    playbackActive = false;
    if (reopenRoot) {
      root = SD.open("/", FILE_READ);
    }
    return;
  }

  Serial.print("Opened file:");
  Serial.println(noBlanks);

  // Fixed line buffer, same ceiling as the serial parser (INLENGTH).
  // The old `String command += ch` reallocates on every byte. A vector
  // file is a couple of hundred thousand lines; that fragments the heap
  // until a later append silently drops characters or the allocator
  // fails. Dropped characters turn "C17,1266,1555,2,END" into a different
  // move (the "right line, wrong place" symptom) or into a line whose
  // segment length parses as 0, which hangs exec_drawBetweenPoints.
  // Lines longer than the serial limit are discarded, matching the
  // serial overrun path, instead of being truncated into a wrong command.
  char command[INLENGTH + 1];
  int commandLen = 0;
  boolean overflow = false;
  unsigned long lastUiMs = millis();
  uint8_t chunk[128];
  int chunkLen = 0;
  int chunkPos = 0;

  while (currentlyDrawingFromFile)
  {
    if (chunkPos >= chunkLen) {
      // Read a block with the display and touch chip-selects released,
      // then parse it from RAM. Touch was previously sampled on every
      // byte, interleaved with the card on the shared SPI bus.
      sd_deselectSharedSpiDevices();
      chunkLen = (int)readFile.read(chunk, sizeof(chunk));
      chunkPos = 0;
      if (chunkLen <= 0) {
        break;
      }
    }

    char ch = (char)chunk[chunkPos++];
    if (ch == '\r') {
      continue;
    }

    if (ch == INTERMINATOR || ch == SEMICOLON)
    {
      if (!overflow && commandLen > 0)
      {
        playback_trimLine(command, &commandLen);
      }
      if (!overflow && commandLen > 0)
      {
        memcpy(currentCommand, command, commandLen + 1);
        boolean commandParsed = comms_parseCommand(currentCommand);
        if (commandParsed)
        {
#ifdef DEBUG_SD
          Serial.println("Stored command parsed.");
#endif
          Serial.print(F("Executing command:"));
          Serial.println(command);
          if (echoingStoredCommands) {
            lcd_echoLastCommandToDisplay(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams, inFilename+": ");
          }
          impl_executeCommand(inCmd, inParam1, inParam2, inParam3, inParam4, inNoOfParams);
        }
#ifdef DEBUG_SD
        else Serial.println("Stored command WAS NOT parsed.");
#endif
      }
      else if (overflow)
      {
        Serial.println("Stored command longer than serial limit, skipped.");
      }
      commandLen = 0;
      overflow = false;

      // The stop button lives on this same task, so the UI has to be
      // pumped from here. Doing it on a timer, and only between card
      // reads, keeps the touch controller off the bus while the card
      // is clocked.
      unsigned long now = millis();
      if ((unsigned long)(now - lastUiMs) >= 50UL) {
        lastUiMs = now;
        sd_deselectSharedSpiDevices();
        impl_serviceUi();
      }
      motion_yieldIfDue();
    }
    else if (commandLen < INLENGTH)
    {
      command[commandLen++] = ch;
      command[commandLen] = 0;
    }
    else
    {
      overflow = true;
    }
  }

  Serial.println("Finished with the file.");
  currentlyDrawingFromFile = false;
  readFile.close();
  currentlyExecutingACommand = alreadyExecuting;
  playbackActive = false;
  if (reopenRoot) {
    root = SD.open("/", FILE_READ);
  }
}

void impl_exec_changeToStoreCommandMode()
{
  String newfilename = inParam1;
  String newFile = inParam2;
  if (newfilename != "")
  {
    Serial.print("Filename for command store: ");
    Serial.println(newfilename);
    storeCommands = true;
    commandFilename = newfilename;
    if (newFile.equals("R"))
    {
      // delete file if it exists
      char filename[newfilename.length()+1];
      newfilename.toCharArray(filename, newfilename.length()+1);

      if (SD.exists(filename))
      {
        // file exists
        Serial.println(F("File already exists."));
        boolean removed = SD.remove(filename);
        if (removed)
          Serial.println(F("File removed."));

      }
    }
  }
  else
  {
    Serial.println("No filename supplied to write to.");
  }
}

void impl_exec_changeToLiveCommandMode()
{
  Serial.println(F("Changing back to live mode."));
  storeCommands = false;
}

void impl_pixel_testPenWidthScribble()
{
  int rowWidth = multiplier(atoi(inParam1));
  float startWidth = atof(inParam2);
  float endWidth = atof(inParam3);
  float incSize = atof(inParam4);

  // boolean ltr = true;

  float oldPenWidth = penWidth;
  int iterations = 0;

  int posA = motorA.currentPosition();
  int posB = motorB.currentPosition();

  // int startColumn = posA;
  int startRow = posB;

  for (float pw = startWidth; pw <= endWidth; pw+=incSize)
  {
    iterations++;
    // int column = posA;

    penWidth = pw;
    int maxDens = pixel_maxDensity(penWidth, rowWidth);
    Serial.print(F("Penwidth test "));
    Serial.print(iterations);
    Serial.print(F(", pen width: "));
    Serial.print(penWidth);
    Serial.print(F(", max density: "));
    Serial.println(maxDens);

    for (int density = maxDens; density >= 0; density--)
    {
      pixel_drawScribblePixel(posA, posB, rowWidth, density);
      posB+=rowWidth;
    }

    posA+=rowWidth;
    posB = startRow;
  }

  changeLength(long(posA-(rowWidth/2)), long(startRow-(rowWidth/2)));

  penWidth = oldPenWidth;

  moveB(0-rowWidth);
  for (int i = 1; i <= iterations; i++)
  {
    moveB(0-(rowWidth/2));
    moveA(0-rowWidth);
    moveB(rowWidth/2);
  }

  penWidth = oldPenWidth;
}

void impl_engageMotors()
{
  motorA.enableOutputs();
  motorB.enableOutputs();
  powerIsOn = true;
  motorA.runToNewPosition(motorA.currentPosition()+multiplier(8));
  motorB.runToNewPosition(motorB.currentPosition()+multiplier(8));
  motorA.runToNewPosition(motorA.currentPosition()-multiplier(8));
  motorB.runToNewPosition(motorB.currentPosition()-multiplier(8));
  
  lcd_changeColourScheme(COLOURSCHEME_HOMED);
  lastOperationTime = millis();

  Serial.println("Engaged motors.");
}

void impl_releaseMotors()
{
  motorA.disableOutputs();
  motorB.disableOutputs();
  powerIsOn = false;
  isHomed = false;
  lcd_changeColourScheme(COLOURSCHEME_NOT_HOMED);
  Serial.println("Released motors");
}


void drawRandom()
{
  for (int i = 0; i < 1000; i++)
  {
    Serial.print("Drawing:");
    Serial.println(i);
    while (motorA.distanceToGo() != 0 && motorB.distanceToGo() != 0)
    {
      motorA.run();
      motorB.run();
    }

    if (motorA.distanceToGo() == 0)
    {
      int r = random(-2,3);
      motorA.move(r);

      Serial.print("Chosen new A target: ");
      Serial.println(r);
    }

    if (motorB.distanceToGo() == 0)
    {
      int r = random(-2,3);
      motorB.move(r);
      Serial.print("Chosen new B target: ");
      Serial.println(r);
    }

    reportPosition();
  }
}

void impl_exec_drawTestDirectionSquare()
{
  int rowWidth = multiplier(atoi(inParam1));
  int segments = atoi(inParam2);
  pixel_drawSquarePixel(rowWidth, rowWidth, segments, DIR_SE);
  moveA(rowWidth*2);

  pixel_drawSquarePixel(rowWidth, rowWidth, segments, DIR_SW);
  moveB(rowWidth*2);

  pixel_drawSquarePixel(rowWidth, rowWidth, segments, DIR_NW);
  moveA(0-(rowWidth*2));

  pixel_drawSquarePixel(rowWidth, rowWidth, segments, DIR_NE);
  moveB(0-(rowWidth*2));

}

void impl_pixel_drawSawtoothPixel()
{
    long originA = multiplier(atol(inParam1));
    long originB = multiplier(atol(inParam2));
    int size = multiplier(atoi(inParam3));
    int density = atoi(inParam4);

    int halfSize = size / 2;

    long startPointA;
    long startPointB;
    long endPointA;
    long endPointB;

    int calcFullSize = halfSize * 2; // see if there's any rounding errors
    int offsetStart = size - calcFullSize;

    if (globalDrawDirectionMode == DIR_MODE_AUTO)
      globalDrawDirection = pixel_getAutoDrawDirection(originA, originB, motorA.currentPosition(), motorB.currentPosition());


    if (globalDrawDirection == DIR_SE)
    {
//      Serial.println(F("d: SE"));
      startPointA = originA - halfSize;
      startPointA += offsetStart;
      startPointB = originB;
      endPointA = originA + halfSize;
      endPointB = originB;
    }
    else if (globalDrawDirection == DIR_SW)
    {
//      Serial.println(F("d: SW"));
      startPointA = originA;
      startPointB = originB - halfSize;
      startPointB += offsetStart;
      endPointA = originA;
      endPointB = originB + halfSize;
    }
    else if (globalDrawDirection == DIR_NW)
    {
//      Serial.println(F("d: NW"));
      startPointA = originA + halfSize;
      startPointA -= offsetStart;
      startPointB = originB;
      endPointA = originA - halfSize;
      endPointB = originB;
    }
    else //(drawDirection == DIR_NE)
    {
//      Serial.println(F("d: NE"));
      startPointA = originA;
      startPointB = originB + halfSize;
      startPointB -= offsetStart;
      endPointA = originA;
      endPointB = originB - halfSize;
    }

    density = pixel_scaleDensity(density, 255, pixel_maxDensity(penWidth, size));

    changeLength(startPointA, startPointB);
    if (density > 1)
    {
      pixel_drawWavePixel(size, size, density, globalDrawDirection, SAW_SHAPE);
    }
    changeLength(endPointA, endPointB);

}

void impl_setDebugComms() {
  int debugCommsValue = atoi(inParam1);
  switch (debugCommsValue) {
    case 0: debugComms = false; break;
    case 1: debugComms = true; break;
  }
}
