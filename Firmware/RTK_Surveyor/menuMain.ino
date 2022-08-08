//Check to see if we've received serial over USB
//Report status if ~ received, otherwise present config menu
void updateSerial()
{
  if (Serial.available())
  {
    byte incoming = Serial.read();

    if (incoming == '~')
    {
      //Output custom GNTXT message with all current system data
      printCurrentConditionsNMEA();
    }
    else
      menuMain(); //Present user menu
  }
}

//Display the options
//If user doesn't respond within a few seconds, return to main loop
void menuMain()
{
  inMainMenu = true;
  displaySerialConfig(); //Display 'Serial Config' while user is configuring

  while (1)
  {
    Serial.println();
    Serial.printf("SparkFun RTK %s v%d.%d-%s\r\n", platformPrefix, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, __DATE__);

#ifdef COMPILE_BT
    Serial.print("** Bluetooth broadcasting as: ");
    Serial.print(deviceName);
    Serial.println(" **");
#else
    Serial.println("** Bluetooth Not Compiled **");
#endif

    Serial.println("Menu: Main Menu");

    Serial.println("1) Configure GNSS Receiver");

    Serial.println("2) Configure GNSS Messages");

    if (zedModuleType == PLATFORM_F9P)
      Serial.println("3) Configure Base");
    else if (zedModuleType == PLATFORM_F9R)
      Serial.println("3) Configure Sensor Fusion");

    Serial.println("4) Configure Ports");

    Serial.println("5) Configure Logging");

    Serial.println("p) Configure Profiles");

    Serial.println("r) Configure Radios");

    if (online.lband == true)
      Serial.println("P) Configure PointPerfect");

    Serial.println("s) System Status");

    if (binCount > 0)
      Serial.println("f) Firmware upgrade");

    Serial.println("x) Exit");

    byte incoming = getByteChoice(menuTimeout); //Timeout after x seconds

    if (incoming == '1')
      menuGNSS();
    else if (incoming == '2')
      menuMessages();
    else if (incoming == '3' && zedModuleType == PLATFORM_F9P)
      menuBase();
    else if (incoming == '3' && zedModuleType == PLATFORM_F9R)
      menuSensorFusion();
    else if (incoming == '4')
      menuPorts();
    else if (incoming == '5')
      menuLog();
    else if (incoming == 's')
      menuSystem();
    else if (incoming == 'p')
      menuUserProfiles();
    else if (incoming == 'P' && online.lband == true)
      menuPointPerfect();
#ifdef COMPILE_ESPNOW
    else if (incoming == 'r')
      menuRadio();
#endif
    else if (incoming == 'f' && binCount > 0)
      menuFirmware();
    else if (incoming == 'x')
      break;
    else if (incoming == STATUS_GETBYTE_TIMEOUT)
      break;
    else
      printUnknown(incoming);
  }

  recordSystemSettings(); //Once all menus have exited, record the new settings to LittleFS and config file

  if (online.gnss == true)
    i2cGNSS.saveConfiguration(); //Save the current settings to flash and BBR on the ZED-F9P

  //Reboot as base only if currently operating as a base station
  if (restartBase && (systemState >= STATE_BASE_NOT_STARTED) && (systemState < STATE_BUBBLE_LEVEL))
  {
    restartBase = false;
    requestChangeState(STATE_BASE_NOT_STARTED); //Restart base upon exit for latest changes to take effect
  }

  if (restartRover == true)
  {
    restartRover = false;
    requestChangeState(STATE_ROVER_NOT_STARTED); //Restart rover upon exit for latest changes to take effect
  }

  while (Serial.available()) Serial.read(); //Empty buffer of any newline chars
  inMainMenu = false;
}

//Change system wide settings based on current user profile
//Ways to change the ZED settings:
//Menus - we apply ZED changes at the exit of each sub menu
//Settings file - we detect differences between NVM and settings txt file and updateZEDSettings = true
//Profile - Before profile is changed, set updateZEDSettings = true
//AP - once new settings are parsed, set updateZEDSettings = true
//Setup button -
//Factory reset - updatesZEDSettings = true by default
void menuUserProfiles()
{
  int menuTimeoutExtended = 30; //Increase time needed for complex data entry (mount point ID, ECEF coords, etc).
  uint8_t originalProfileNumber = profileNumber;

  while (1)
  {
    Serial.println();
    Serial.println("Menu: User Profiles Menu");

    //List available profiles
    for (int x = 0 ; x < MAX_PROFILE_COUNT ; x++)
    {
      if (activeProfiles & (1 << x))
        Serial.printf("%d) Select %s", x + 1, profileNames[x]);
      else
        Serial.printf("%d) Select (Empty)", x + 1);

      if (x == profileNumber) Serial.print(" <- Current");

      Serial.println();
    }

    Serial.printf("%d) Edit profile name: %s\n\r", MAX_PROFILE_COUNT + 1, profileNames[profileNumber]);

    Serial.printf("%d) Delete profile '%s'\n\r", MAX_PROFILE_COUNT + 2, profileNames[profileNumber]);

    Serial.println("x) Exit");

    int incoming = getNumber(menuTimeout); //Timeout after x seconds

    if (incoming >= 1 && incoming <= MAX_PROFILE_COUNT)
    {
      changeProfileNumber(incoming - 1); //Align inputs to array
    }
    else if (incoming == MAX_PROFILE_COUNT + 1)
    {
      Serial.print("Enter new profile name: ");
      readLine(settings.profileName, sizeof(settings.profileName), menuTimeoutExtended);
      recordSystemSettings(); //We need to update this immediately in case user lists the available profiles again
      setProfileName(profileNumber);
    }
    else if (incoming == MAX_PROFILE_COUNT + 2)
    {
      Serial.printf("\r\nDelete profile '%s'. Press 'y' to confirm:", profileNames[profileNumber]);
      byte bContinue = getByteChoice(menuTimeout);
      if (bContinue == 'y')
      {
        //Remove profile from LittleFS
        if (LittleFS.exists(settingsFileName))
          LittleFS.remove(settingsFileName);

        //Remove profile from SD if available
        if (online.microSD == true)
        {
          if (sd->exists(settingsFileName))
            sd->remove(settingsFileName);
        }

        recordProfileNumber(0); //Move to Profile1
        profileNumber = 0;

        sprintf(settingsFileName, "/%s_Settings_%d.txt", platformFilePrefix, profileNumber); //Update file name with new profileNumber

        //We need to load these settings from file so that we can record a profile name change correctly
        bool responseLFS = loadSystemSettingsFromFileLFS(settingsFileName, &settings);
        bool responseSD = loadSystemSettingsFromFileSD(settingsFileName, &settings);

        //If this is an empty/new profile slot, overwrite our current settings with defaults
        if (responseLFS == false && responseSD == false)
        {
          Settings tempSettings;
          settings = tempSettings;
        }

        //Get bitmask of active profiles
        activeProfiles = loadProfileNames();
      }
      else
        Serial.println("Delete aborted");
    }

    else if (incoming == STATUS_PRESSED_X)
      break;
    else if (incoming == STATUS_GETNUMBER_TIMEOUT)
      break;
    else
      printUnknown(incoming);
  }

  if (originalProfileNumber != profileNumber)
  {
    Serial.println("Changing profiles. Rebooting. Goodbye!");
    delay(2000);
    ESP.restart();
  }

  //A user may edit the name of a profile, but then switch back to original profile.
  //Thus, no reset, and activeProfiles is not updated. Do it here.
  //Get bitmask of active profiles
  activeProfiles = loadProfileNames();

  while (Serial.available()) Serial.read(); //Empty buffer of any newline chars
}

//Change the active profile number, without unit reset
void changeProfileNumber(byte newProfileNumber)
{
  settings.updateZEDSettings = true; //When this profile is loaded next, force system to update ZED settings.
  recordSystemSettings(); //Before switching, we need to record the current settings to LittleFS and SD

  recordProfileNumber(newProfileNumber);
  profileNumber = newProfileNumber;
  setSettingsFileName(); //Load the settings file name into memory (enabled profile name delete)

  //We need to load these settings from file so that we can record a profile name change correctly
  bool responseLFS = loadSystemSettingsFromFileLFS(settingsFileName, &settings);
  bool responseSD = loadSystemSettingsFromFileSD(settingsFileName, &settings);

  //If this is an empty/new profile slot, overwrite our current settings with defaults
  if (responseLFS == false && responseSD == false)
  {
    Serial.println("Default the settings");
    //Create a temporary settings struc on the heap (not the stack because it is ~4500 bytes)
    Settings *tempSettings = new Settings;
    settings = *tempSettings;
    delete tempSettings;
  }
}

//Erase all settings. Upon restart, unit will use defaults
void factoryReset()
{
  displaySytemReset(); //Display friendly message on OLED

  Serial.println("Formatting file system...");
  LittleFS.format();

  //Attempt to write to file system. This avoids collisions with file writing from other functions like recordSystemSettingsToFile() and F9PSerialReadTask()
  if (settings.enableSD && online.microSD)
  {
    if (xSemaphoreTake(sdCardSemaphore, fatSemaphore_longWait_ms) == pdPASS)
    {
      //Remove this specific settings file. Don't remove the other profiles.
      sd->remove(settingsFileName);
      xSemaphoreGive(sdCardSemaphore);
    } //End sdCardSemaphore
    else
    {
      //An error occurs when a settings file is on the microSD card and it is not
      //deleted, as such the settings on the microSD card will be loaded when the
      //RTK reboots, resulting in failure to achieve the factory reset condition
      Serial.printf("sdCardSemaphore failed to yield, menuMain.ino line %d\r\n", __LINE__);
    }
  }

  if (online.gnss == true)
    i2cGNSS.factoryReset(); //Reset everything: baud rate, I2C address, update rate, everything.

  Serial.println("Settings erased successfully. Rebooting. Goodbye!");
  delay(2000);
  ESP.restart();
}

//Configure the internal radio, if available
void menuRadio()
{
#ifdef COMPILE_ESPNOW
  while (1)
  {
    Serial.println();
    Serial.println("Menu: Radio Menu");

    Serial.print("1) Select Radio Type: ");
    if (settings.radioType == RADIO_EXTERNAL) Serial.println("External only");
    else if (settings.radioType == RADIO_ESPNOW) Serial.println("Internal ESP NOW");

    if (settings.radioType == RADIO_ESPNOW)
    {
      //Pretty print the MAC of all radios

      //Get unit MAC address
      uint8_t unitMACAddress[6];
      esp_read_mac(unitMACAddress, ESP_MAC_WIFI_STA);

      Serial.print("  Radio MAC: ");
      for (int x = 0 ; x < 5 ; x++)
        Serial.printf("%02X:", unitMACAddress[x]);
      Serial.printf("%02X\n\r", unitMACAddress[5]);

      if (settings.espnowPeerCount > 0)
      {
        Serial.println("  Paired Radios: ");
        for (int x = 0 ; x < settings.espnowPeerCount ; x++)
        {
          Serial.print("    ");
          for (int y = 0 ; y < 5 ; y++)
            Serial.printf("%02X:", settings.espnowPeers[x][y]);
          Serial.printf("%02X\n\r", settings.espnowPeers[x][5]);
        }
      }
      else
        Serial.println("  No Paired Radios");
      

      Serial.println("2) Pair radios");
      Serial.println("3) Forget all radios");
    }

    Serial.println("x) Exit");

    int incoming = getNumber(menuTimeout); //Timeout after x seconds

    if (incoming == 1)
    {
      if (settings.radioType == RADIO_EXTERNAL) settings.radioType = RADIO_ESPNOW;
      else if (settings.radioType == RADIO_ESPNOW) settings.radioType = RADIO_EXTERNAL;
    }
    else if (settings.radioType == RADIO_ESPNOW && incoming == 2)
    {
      Serial.println("Begin ESP NOW Pairing");
      espnowBeginPairing();
    }
    else if (settings.radioType == RADIO_ESPNOW && incoming == 3)
    {
      Serial.println("\r\nForgetting all paired radios. Press 'y' to confirm:");
      byte bContinue = getByteChoice(menuTimeout);
      if (bContinue == 'y')
      {
        for (int x = 0 ; x < settings.espnowPeerCount ; x++)
          espnowRemovePeer(settings.espnowPeers[x]);
        settings.espnowPeerCount = 0;
        Serial.println("Radios forgotten");
      }
    }

    else if (incoming == STATUS_PRESSED_X)
      break;
    else if (incoming == STATUS_GETNUMBER_TIMEOUT)
      break;
    else
      printUnknown(incoming);
  }

  radioStart();

  while (Serial.available()) Serial.read(); //Empty buffer of any newline chars
#endif
}
