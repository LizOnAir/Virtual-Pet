#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>

// Hardware configuration
constexpr uint8_t ENCODER_PIN_A = 4;
constexpr uint8_t ENCODER_PIN_B = 3;
constexpr uint8_t ENCODER_BTN_PIN = 1;
constexpr uint8_t TFT_BACKLIGHT_PIN = 8;

// Game tuning constants
constexpr uint32_t ONE_MINUTE_MS = 60000;
constexpr uint32_t ONE_DAY_MS = 24UL * 60UL * ONE_MINUTE_MS;
constexpr uint16_t LONG_PRESS_MS = 1200;
constexpr uint8_t MAX_NEED = 100;
constexpr uint8_t MAX_STARS = 5;
constexpr uint8_t MAX_WEIGHT = 100;
constexpr uint8_t MIN_WEIGHT = 1;
constexpr uint8_t ADULT_AGE_DAYS = 30;

// Need decay per in-game day.
constexpr int8_t HUNGER_DECAY = 22;
constexpr int8_t THIRST_DECAY = 18;
constexpr int8_t HAPPINESS_DECAY = 24;

// Game enums
enum class LifeStage : uint8_t {
  EGG = 0,
  BABY,
  CHILD,
  TEEN,
  ADULT
};

enum class MenuItem : uint8_t {
  FEED_MEAL = 0,
  FEED_SNACK,
  PLAY,
  MEDICINE,
  STATUS,
  SLEEP,
  COUNT
};

struct PetState {
  bool started = false;
  bool alive = true;
  bool sleeping = false;
  bool ill = false;

  uint8_t hunger = 70;
  uint8_t thirst = 70;
  uint8_t happiness = 70;
  uint8_t stars = 5;
  uint8_t weight = 12;

  uint16_t ageDays = 0;
  uint16_t growthDays = 0;
  uint16_t careMistakes = 0;

  uint32_t lastTickMs = 0;
};

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
PetState pet;

int menuIndex = 0;
bool displaySleeping = false;

// rotary state
int lastEncA = HIGH;
uint32_t btnPressedAt = 0;
bool btnWasPressed = false;

// Utility helpers
const char *stageName(LifeStage s) {
  switch (s) {
    case LifeStage::EGG: return "Egg";
    case LifeStage::BABY: return "Baby";
    case LifeStage::CHILD: return "Child";
    case LifeStage::TEEN: return "Teen";
    case LifeStage::ADULT: return "Adult";
  }
  return "Unknown";
}

LifeStage currentStage() {
  if (!pet.started) return LifeStage::EGG;
  if (pet.growthDays < 3) return LifeStage::BABY;
  if (pet.growthDays < 10) return LifeStage::CHILD;
  if (pet.growthDays < ADULT_AGE_DAYS) return LifeStage::TEEN;
  return LifeStage::ADULT;
}

void saveState() {
  prefs.putBool("started", pet.started);
  prefs.putBool("alive", pet.alive);
  prefs.putBool("sleeping", pet.sleeping);
  prefs.putBool("ill", pet.ill);
  prefs.putUChar("hunger", pet.hunger);
  prefs.putUChar("thirst", pet.thirst);
  prefs.putUChar("happy", pet.happiness);
  prefs.putUChar("stars", pet.stars);
  prefs.putUChar("weight", pet.weight);
  prefs.putUShort("age", pet.ageDays);
  prefs.putUShort("growth", pet.growthDays);
  prefs.putUShort("mistakes", pet.careMistakes);
  prefs.putULong("tick", pet.lastTickMs);
}

void loadState() {
  pet.started = prefs.getBool("started", false);
  pet.alive = prefs.getBool("alive", true);
  pet.sleeping = prefs.getBool("sleeping", false);
  pet.ill = prefs.getBool("ill", false);
  pet.hunger = prefs.getUChar("hunger", 70);
  pet.thirst = prefs.getUChar("thirst", 70);
  pet.happiness = prefs.getUChar("happy", 70);
  pet.stars = prefs.getUChar("stars", 5);
  pet.weight = prefs.getUChar("weight", 12);
  pet.ageDays = prefs.getUShort("age", 0);
  pet.growthDays = prefs.getUShort("growth", 0);
  pet.careMistakes = prefs.getUShort("mistakes", 0);
  pet.lastTickMs = millis();
}

void clampVitals() {
  pet.hunger = constrain(pet.hunger, 0, MAX_NEED);
  pet.thirst = constrain(pet.thirst, 0, MAX_NEED);
  pet.happiness = constrain(pet.happiness, 0, MAX_NEED);
  pet.stars = constrain(pet.stars, 0, MAX_STARS);
  pet.weight = constrain(pet.weight, MIN_WEIGHT, MAX_WEIGHT);
}

void setDisplaySleep(bool sleepOn) {
  displaySleeping = sleepOn;
  if (sleepOn) {
    digitalWrite(TFT_BACKLIGHT_PIN, LOW);
    tft.writecommand(0x28); // Display OFF
    tft.writecommand(0x10); // Sleep IN
  } else {
    tft.writecommand(0x11); // Sleep OUT
    delay(120);
    tft.writecommand(0x29); // Display ON
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  }
}

// Rendering
String starsString() {
  String s;
  for (uint8_t i = 0; i < pet.stars; ++i) s += "*";
  for (uint8_t i = pet.stars; i < MAX_STARS; ++i) s += "-";
  return s;
}

void drawHeader(const char *title) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillRect(0, 0, 240, 18, TFT_DARKGREY);
  tft.setCursor(4, 4);
  tft.print(title);
}

void drawStatusBars() {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(5, 24);
  tft.printf("Hunger: %3u", pet.hunger);
  tft.setCursor(5, 40);
  tft.printf("Thirst: %3u", pet.thirst);
  tft.setCursor(5, 56);
  tft.printf("Happy : %3u", pet.happiness);
  tft.setCursor(5, 72);
  tft.printf("Health: %s", starsString().c_str());
  tft.setCursor(5, 88);
  tft.printf("Weight: %u", pet.weight);
  tft.setCursor(5, 104);
  tft.printf("Age   : %ud", pet.ageDays);
}

const char *menuName(MenuItem item) {
  switch (item) {
    case MenuItem::FEED_MEAL: return "Feed meal";
    case MenuItem::FEED_SNACK: return "Give snack";
    case MenuItem::PLAY: return "Play mini-game";
    case MenuItem::MEDICINE: return "Give medicine";
    case MenuItem::STATUS: return "Show status";
    case MenuItem::SLEEP: return "Toggle sleep";
    case MenuItem::COUNT: break;
  }
  return "-";
}

void drawFace() {
  tft.fillRoundRect(145, 25, 85, 85, 12, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(154, 35);

  if (!pet.started) {
    tft.print("( )");
    tft.setCursor(153, 55);
    tft.print("EGG");
    tft.setCursor(148, 75);
    tft.print("Incubate!");
    return;
  }

  if (!pet.alive) {
    tft.print("x   x");
    tft.setCursor(165, 56);
    tft.print(" o ");
    tft.setCursor(157, 76);
    tft.print("Ghost");
    return;
  }

  if (pet.sleeping) {
    tft.print("z  z");
    tft.setCursor(165, 56);
    tft.print("--");
    tft.setCursor(158, 76);
    tft.print("Sleep");
    return;
  }

  if (pet.ill) {
    tft.print("x  x");
    tft.setCursor(165, 56);
    tft.print("~o~");
    tft.setCursor(157, 76);
    tft.print("Dizzy");
    return;
  }

  if (pet.happiness < 25) {
    tft.print(";  ;");
    tft.setCursor(165, 56);
    tft.print("__");
    tft.setCursor(151, 76);
    tft.print("Crying");
    return;
  }

  tft.print("^  ^");
  tft.setCursor(165, 56);
  tft.print("~~");
  tft.setCursor(153, 76);
  tft.print("Happy");
}

void drawMenu() {
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(6, 128);
  tft.print("Action:");

  for (uint8_t i = 0; i < static_cast<uint8_t>(MenuItem::COUNT); ++i) {
    const int y = 146 + (i * 14);
    if (i == menuIndex) {
      tft.fillRect(0, y - 1, 240, 12, TFT_DARKGREEN);
      tft.setTextColor(TFT_BLACK, TFT_DARKGREEN);
      tft.setCursor(8, y);
      tft.printf("> %s", menuName(static_cast<MenuItem>(i)));
    } else {
      tft.fillRect(0, y - 1, 240, 12, TFT_BLACK);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(8, y);
      tft.printf("  %s", menuName(static_cast<MenuItem>(i)));
    }
  }
}

void drawScreen() {
  if (displaySleeping) return;

  tft.fillScreen(TFT_BLACK);

  if (pet.sleeping) {
    drawHeader("Sleeping");
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(56, 100);
    tft.setTextSize(3);
    tft.print("zzz");
    tft.setTextSize(1);
    return;
  }

  drawHeader(stageName(currentStage()));
  drawStatusBars();
  drawFace();
  drawMenu();
}

void showToast(const String &msg, uint16_t color = TFT_ORANGE) {
  if (displaySleeping) return;
  tft.fillRect(0, 220, 240, 20, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(4, 224);
  tft.print(msg);
}

// Game logic
void applyDailyDecay() {
  if (!pet.started || !pet.alive) return;

  pet.ageDays++;
  pet.hunger = max(0, pet.hunger + HUNGER_DECAY * -1);
  pet.thirst = max(0, pet.thirst + THIRST_DECAY * -1);
  pet.happiness = max(0, pet.happiness + HAPPINESS_DECAY * -1);

  bool ignoredNeeds = (pet.hunger < 20) || (pet.thirst < 20) || (pet.happiness < 20);
  if (ignoredNeeds) {
    pet.careMistakes++;
    if (pet.stars > 0) pet.stars--;
  }

  if (pet.weight > 75 && random(0, 100) < 35) {
    pet.ill = true;
  }

  if (pet.stars <= 3 || pet.hunger == 0 || pet.thirst == 0 || pet.happiness == 0) {
    pet.ill = true;
  }

  if (!pet.ill) {
    pet.growthDays++;  // Growth stunts while ill.
  }

  // Death conditions
  if (pet.stars == 0 || (pet.ill && pet.careMistakes > 5 && random(0, 100) < 45) || pet.ageDays > 200) {
    pet.alive = false;
    pet.sleeping = false;
  }

  clampVitals();
}

void incubateEgg() {
  if (pet.started) return;
  pet.started = true;
  pet.alive = true;
  pet.sleeping = false;
  pet.ill = false;
  pet.hunger = 80;
  pet.thirst = 80;
  pet.happiness = 80;
  pet.stars = 5;
  pet.weight = 10;
  pet.ageDays = 0;
  pet.growthDays = 0;
  pet.careMistakes = 0;
  pet.lastTickMs = millis();
  showToast("Egg incubated. Baby born!", TFT_GREENYELLOW);
}

void resetGame() {
  pet = PetState{};
  pet.lastTickMs = millis();
  showToast("Game reset. Incubate egg.", TFT_GREENYELLOW);
}

void feedMeal() {
  if (!pet.started || !pet.alive) return;
  pet.hunger = min<int>(MAX_NEED, pet.hunger + 30);
  pet.weight = min<int>(MAX_WEIGHT, pet.weight + 2);
  showToast("Meal served +hunger +weight");
}

void feedSnack() {
  if (!pet.started || !pet.alive) return;
  pet.happiness = min<int>(MAX_NEED, pet.happiness + 22);
  pet.hunger = min<int>(MAX_NEED, pet.hunger + 5);
  pet.weight = min<int>(MAX_WEIGHT, pet.weight + 4);
  showToast("Snack! +happy +weight");
}

void playMiniGame() {
  if (!pet.started || !pet.alive) return;
  const int score = random(20, 101);
  pet.happiness = min<int>(MAX_NEED, pet.happiness + score / 3);
  pet.weight = max<int>(MIN_WEIGHT, pet.weight - 3);
  pet.thirst = max(0, pet.thirst - 8);
  showToast("Played! Score " + String(score));
}

void curePet() {
  if (!pet.started || !pet.alive) return;
  if (!pet.ill) {
    showToast("No illness to cure", TFT_SKYBLUE);
    return;
  }
  pet.ill = false;
  pet.happiness = min<int>(MAX_NEED, pet.happiness + 8);
  showToast("Medicine applied!", TFT_GREEN);
}

void togglePetSleep() {
  if (!pet.started || !pet.alive) return;
  pet.sleeping = !pet.sleeping;
  showToast(pet.sleeping ? "Pet sleeping" : "Pet awake", TFT_SKYBLUE);
}

void doSelectedAction() {
  if (!pet.alive && pet.started) {
    resetGame();
    return;
  }

  switch (static_cast<MenuItem>(menuIndex)) {
    case MenuItem::FEED_MEAL:
      if (!pet.started) incubateEgg();
      else feedMeal();
      break;
    case MenuItem::FEED_SNACK:
      if (!pet.started) incubateEgg();
      else feedSnack();
      break;
    case MenuItem::PLAY:
      if (!pet.started) incubateEgg();
      else playMiniGame();
      break;
    case MenuItem::MEDICINE:
      if (!pet.started) incubateEgg();
      else curePet();
      break;
    case MenuItem::STATUS:
      if (!pet.started) incubateEgg();
      else showToast("Stage: " + String(stageName(currentStage())));
      break;
    case MenuItem::SLEEP:
      if (!pet.started) incubateEgg();
      else togglePetSleep();
      break;
    case MenuItem::COUNT:
      break;
  }

  clampVitals();
  saveState();
}

void handleClock() {
  const uint32_t now = millis();
  if (pet.lastTickMs == 0) pet.lastTickMs = now;

  while ((now - pet.lastTickMs) >= ONE_DAY_MS) {
    pet.lastTickMs += ONE_DAY_MS;
    applyDailyDecay();
  }
}

// Input handling
void handleEncoderTurn() {
  int a = digitalRead(ENCODER_PIN_A);
  int b = digitalRead(ENCODER_PIN_B);

  if (a != lastEncA && a == LOW) {
    if (b == HIGH) {
      menuIndex++;
    } else {
      menuIndex--;
    }

    if (menuIndex < 0) menuIndex = static_cast<int>(MenuItem::COUNT) - 1;
    if (menuIndex >= static_cast<int>(MenuItem::COUNT)) menuIndex = 0;
  }

  lastEncA = a;
}

void handleEncoderButton() {
  bool pressed = (digitalRead(ENCODER_BTN_PIN) == LOW);
  uint32_t now = millis();

  if (pressed && !btnWasPressed) {
    btnPressedAt = now;
    btnWasPressed = true;
  }

  if (!pressed && btnWasPressed) {
    const uint32_t held = now - btnPressedAt;
    btnWasPressed = false;

    if (held >= LONG_PRESS_MS) {
      setDisplaySleep(!displaySleeping);
      showToast(displaySleeping ? "Display OFF" : "Display ON", TFT_SKYBLUE);
    } else {
      doSelectedAction();
    }
  }
}

// Arduino entrypoints
void setup() {
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  pinMode(ENCODER_BTN_PIN, INPUT_PULLUP);
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

  randomSeed(esp_random());
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  prefs.begin("tamagotchi", false);
  loadState();

  drawScreen();
  showToast("Hold button: display sleep/wake", TFT_SKYBLUE);
}

void loop() {
  handleClock();
  handleEncoderTurn();
  handleEncoderButton();

  static uint32_t lastFrame = 0;
  const uint32_t now = millis();
  if (now - lastFrame > 120) {
    drawScreen();
    lastFrame = now;
  }

  static uint32_t lastSave = 0;
  if (now - lastSave > 30000) {
    saveState();
    lastSave = now;
  }

  delay(5);
}
