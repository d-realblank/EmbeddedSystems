//
// SOEN422 - Lab 3
// Combines a WiFi Web Server for authentication with LoRaWAN for logging.
//
// Part 1: Web server for door control (LEDs + Servo)
// Part 2: LoRaWAN integration for sending authentication logs to TTN
//

// Core Libraries
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// LoRaWAN Libraries
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

// Needed for disabling brownout detector
#include "soc/rtc_cntl_reg.h"

//
// --- LoRaWAN Configuration ---
//

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from the TTN console, this means to reverse
// the bytes.
static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// This should also be in little-endian format, see above.
static const u1_t PROGMEM DEVEUI[8] = {0x99, 0x4D, 0x00, 0xD8, 0x7E, 0xD5, 0xB3, 0x70};
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// This key should be in big-endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from the TTN console can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = {0xD6, 0x9E, 0xEF, 0x8C, 0xE4, 0x12, 0xE6, 0x7F, 0x5D, 0x3C, 0xF7, 0x3C, 0x80, 0xF0, 0x7C, 0xB9};
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

static osjob_t sendjob;
const unsigned TX_INTERVAL = 60;

// Pin mapping for TTGO LoRa32
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,
    .dio = {/*dio0*/26, /*dio1*/ 33, /*dio2*/ 32},
};

// --- End LoRaWAN Configuration ---


//
// --- WiFi & Web Server Configuration ---
//

// WiFi credentials
const char *ssid = "DayveidT-T";
const char *password = "12345678";

// Web server on port 80
WebServer server(80);

// Pin definitions
const int GREEN_LED_PIN = 22;
const int RED_LED_PIN = 21;
const int SERVO_PIN = 13;

// Servo object
Servo doorServo;
int currentServoPosition = 0;

// Authentication credentials
String ROOM_USER = "dayveid";
String ROOM_PASSWORD = "1234";

// Admin credentials
const String ADMIN_USER = "admin";
const String ADMIN_PASSWORD = "admin123";

// Authentication tracking
int failedAttempts = 0;
unsigned long suspensionStartTime = 0;
const unsigned long SUSPENSION_DURATION = 120000; // 2 minutes
bool isSuspended = false;

// --- End WiFi & Web Server Configuration ---


//
// --- HTML Pages ---
// (HTML content is omitted for brevity, but is included in your file)
//
const char* loginPage = R"rawliteral( ... )rawliteral";
const char* suspensionPage = R"rawliteral( ... )rawliteral";
const char* successPage = R"rawliteral( ... )rawliteral";
const char* adminLoginPage = R"rawliteral( ... )rawliteral";
const char* adminPanelPage = R"rawliteral( ... )rawliteral";


//
// --- LoRaWAN Functions ---
//

// Function to queue a message for LoRaWAN transmission
void sendLoRaMessage(String message) {
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("LoRaWAN: OP_TXRXPEND, not sending"));
    } else {
        // Prepare data for transmission
        // Note: data must be a byte array
        byte payload[message.length() + 1];
        message.getBytes(payload, message.length() + 1);

        // Schedule the message for transmission
        LMIC_setTxData2(1, payload, sizeof(payload) - 1, 0);
        Serial.print(F("LoRaWAN: Packet queued: "));
        Serial.println(message);
    }
}

void onEvent(ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch (ev) {
    case EV_SCAN_TIMEOUT:
        Serial.println(F("EV_SCAN_TIMEOUT"));
        break;
    case EV_BEACON_FOUND:
        Serial.println(F("EV_BEACON_FOUND"));
        break;
    case EV_BEACON_MISSED:
        Serial.println(F("EV_BEACON_MISSED"));
        break;
    case EV_BEACON_TRACKED:
        Serial.println(F("EV_BEACON_TRACKED"));
        break;
    case EV_JOINING:
        Serial.println(F("EV_JOINING"));
        break;
    case EV_JOINED:
        Serial.println(F("EV_JOINED"));
        {
            u4_t netid = 0;
            devaddr_t devaddr = 0;
            u1_t nwkKey[16];
            u1_t artKey[16];
            LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
            Serial.print("netid: ");
            Serial.println(netid, DEC);
        }
        // Disable link check validation (automatically enabled
        // during join, but not supported by TTN at this time).
        LMIC_setLinkCheckMode(0);
        break;
    case EV_JOIN_FAILED:
        Serial.println(F("EV_JOIN_FAILED"));
        break;
    case EV_REJOIN_FAILED:
        Serial.println(F("EV_REJOIN_FAILED"));
        break;
    case EV_TXCOMPLETE:
        Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
        if (LMIC.txrxFlags & TXRX_ACK)
            Serial.println(F("Received ack"));
        if (LMIC.dataLen) {
            Serial.print(F("Received "));
            Serial.print(LMIC.dataLen);
            Serial.println(F(" bytes of payload"));
        }
        break;
    case EV_LOST_TSYNC:
        Serial.println(F("EV_LOST_TSYNC"));
        break;
    case EV_RESET:
        Serial.println(F("EV_RESET"));
        break;
    case EV_RXCOMPLETE:
        // data received in ping slot
        Serial.println(F("EV_RXCOMPLETE"));
        break;
    case EV_LINK_DEAD:
        Serial.println(F("EV_LINK_DEAD"));
        break;
    case EV_LINK_ALIVE:
        Serial.println(F("EV_LINK_ALIVE"));
        break;
    case EV_TXSTART:
        Serial.println(F("EV_TXSTART"));
        break;
    case EV_JOIN_TXCOMPLETE:
        Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
        break;
    default:
        Serial.print(F("Unknown event: "));
        Serial.println((unsigned)ev);
        break;
    }
}

void do_send(osjob_t *j) {
    // This function is only used to trigger the initial join.
    // After joining, messages are sent manually via sendLoRaMessage().
    if (LMIC.opmode & OP_JOINING) {
        Serial.println(F("Still trying to join..."));
    }
}

//
// --- Main Setup and Loop ---
//

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("Starting Lab 3..."));

    // Disable brownout detector
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // --- Initialize Hardware (LEDs & Servo) ---
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    doorServo.setPeriodHertz(50);
    doorServo.attach(SERVO_PIN, 500, 2400);
    if (doorServo.attached()) {
        doorServo.write(0);
        currentServoPosition = 0;
        Serial.println("Servo initialized.");
    } else {
        Serial.println("ERROR: Servo failed to attach!");
    }

    // --- Initialize LoRaWAN ---
    os_init();
    LMIC_reset();

    // IMPORTANT: Set the correct frequency plan for your region.
    // For US915 or AU915, TTN uses sub-band 2.
    // Comment this line out if you are in Europe (EU868).
    LMIC_selectSubBand(1);

    // Start job (triggers auto-join)
    do_send(&sendjob);
    Serial.println("LoRaWAN: Join request sent.");

    // --- Initialize WiFi and Web Server ---
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/admin", handleAdminRoot);
    server.on("/admin/login", HTTP_POST, handleAdminLogin);
    server.on("/admin/panel", handleAdminPanel);
    server.on("/admin/update", HTTP_POST, handleAdminUpdate);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("Web server started.");
}

void loop() {
    // Run the LoRaWAN state machine
    os_runloop_once();

    // Handle incoming web server clients
    server.handleClient();

    // Handle system suspension
    if (isSuspended && (millis() - suspensionStartTime >= SUSPENSION_DURATION)) {
        isSuspended = false;
        failedAttempts = 0;
        Serial.println("Suspension period ended. System ready.");
    }
}


//
// --- Web Server Handler Functions ---
//
void handleRoot() {
    if (isSuspended) {
        server.send(403, "text/html", suspensionPage);
        return;
    }
    server.send(200, "text/html", loginPage);
}

void handleLogin() {
    if (isSuspended) {
        server.send(403, "text/html", suspensionPage);
        return;
    }
    
    // Get credentials from POST request
    String username = server.arg("username");
    String password = server.arg("password");
    
    Serial.println("Authentication attempt:");
    Serial.print("Username: ");
    Serial.println(username);
    
    // Validate credentials
    if (username == ROOM_USER && password == ROOM_PASSWORD) {
        // Successful authentication
        Serial.println("✓ Authentication successful!");
        failedAttempts = 0;
        
        // Turn on ONLY green LED
        digitalWrite(RED_LED_PIN, LOW);   // Make sure red is off
        digitalWrite(GREEN_LED_PIN, HIGH); // Turn on green
        
        Serial.println("Green LED ON");
        
        // Unlock door (move servo)
        unlockDoor();
        
        // Send success page
        server.send(200, "text/html", successPage);
        
        // Keep door unlocked for 5 seconds
        Serial.println("Door unlocked for 5 seconds...");
        delay(5000);
        
        // Lock door again
        lockDoor();
        
        // Turn off green LED
        digitalWrite(GREEN_LED_PIN, LOW);
        Serial.println("Green LED OFF - Door locked");
        
    } else {
        // Failed authentication
        failedAttempts++;
        Serial.print("✗ Authentication failed! Attempt ");
        Serial.print(failedAttempts);
        Serial.println(" of 3");
        
        // Turn on ONLY red LED
        digitalWrite(GREEN_LED_PIN, LOW);  // Make sure green is off
        digitalWrite(RED_LED_PIN, HIGH);   // Turn on red
        
        Serial.println("Red LED ON");
        
        if (failedAttempts >= 3) {
            // Suspend system
            isSuspended = true;
            suspensionStartTime = millis();
            Serial.println("⚠ Too many failed attempts! System suspended for 2 minutes.");
            server.send(403, "text/html", suspensionPage);
        } else {
            // Send error message
            String errorPage = loginPage;
            errorPage.replace("</form>", 
                "<div class='error'>Invalid credentials! Attempt " + 
                String(failedAttempts) + " of 3</div></form>");
            server.send(401, "text/html", errorPage);
        }
        
        // Turn off red LED after 2 seconds
        delay(2000);
        digitalWrite(RED_LED_PIN, LOW);
        Serial.println("Red LED OFF");
    }
}

void handleNotFound() {
    server.send(404, "text/plain", "404: Page not found");
}

void unlockDoor() {
    Serial.println(">>> Unlocking door...");
    
    if (doorServo.attached()) {
        Serial.print("Moving servo to 90 degrees... ");
        doorServo.write(90); // Move to unlocked position (90 degrees)
        Serial.println("Command sent");
        delay(1000); // Increased delay to allow servo to complete movement
        Serial.println(">>> Door unlocked");
    } else {
        Serial.println("⚠ WARNING: Servo not attached, simulating unlock");
        delay(100);
    }
}

void lockDoor() {
    Serial.println(">>> Locking door...");
    
    if (doorServo.attached()) {
        Serial.print("Moving servo to 0 degrees... ");
        doorServo.write(0); // Move to locked position (0 degrees)
        Serial.println("Command sent");
        delay(1000); // Increased delay to allow servo to complete movement
        Serial.println(">>> Door locked");
    } else {
        Serial.println("⚠ WARNING: Servo not attached, simulating lock");
        delay(100);
    }
}

void handleAdminRoot() {
    server.send(200, "text/html", adminLoginPage);
}

void handleAdminLogin() {
    // Get admin credentials from POST request
    String username = server.arg("username");
    String password = server.arg("password");
    
    Serial.println("Admin authentication attempt:");
    Serial.print("Username: ");
    Serial.println(username);
    
    // Validate admin credentials
    if (username == ADMIN_USER && password == ADMIN_PASSWORD) {
        Serial.println("✓ Admin authentication successful!");
        // Redirect to admin panel
        server.sendHeader("Location", "/admin/panel");
        server.send(303);
    } else {
        Serial.println("✗ Admin authentication failed!");
        // Send error message
        String errorPage = adminLoginPage;
        errorPage.replace("</form>", 
            "<div class='error'>Invalid admin credentials!</div></form>");
        server.send(401, "text/html", errorPage);
    }
}

void handleAdminPanel() {
    // Show admin panel with current credentials
    String panel = adminPanelPage;
    panel.replace("CURRENT_USER", ROOM_USER);
    server.send(200, "text/html", panel);
}

void handleAdminUpdate() {
    // Get new credentials from POST request
    String newUsername = server.arg("newUsername");
    String newPassword = server.arg("newPassword");
    String confirmPassword = server.arg("confirmPassword");
    
    Serial.println("Admin attempting to update credentials:");
    Serial.print("New Username: ");
    Serial.println(newUsername);
    
    // Validate that passwords match
    if (newPassword != confirmPassword) {
        String errorPanel = adminPanelPage;
        errorPanel.replace("CURRENT_USER", ROOM_USER);
        errorPanel.replace("</form>", 
            "<div class='error'>Passwords do not match!</div></form>");
        server.send(400, "text/html", errorPanel);
        return;
    }
    
    // Validate password length (minimum 4 characters)
    if (newPassword.length() < 4) {
        String errorPanel = adminPanelPage;
        errorPanel.replace("CURRENT_USER", ROOM_USER);
        errorPanel.replace("</form>", 
            "<div class='error'>Password must be at least 4 characters!</div></form>");
        server.send(400, "text/html", errorPanel);
        return;
    }
    
    // Validate username length (minimum 3 characters)
    if (newUsername.length() < 3) {
        String errorPanel = adminPanelPage;
        errorPanel.replace("CURRENT_USER", ROOM_USER);
        errorPanel.replace("</form>", 
            "<div class='error'>Username must be at least 3 characters!</div></form>");
        server.send(400, "text/html", errorPanel);
        return;
    }
    
    // Update credentials
    ROOM_USER = newUsername;
    ROOM_PASSWORD = newPassword;
    
    Serial.println("✓ Credentials updated successfully!");
    Serial.print("New Username: ");
    Serial.println(ROOM_USER);
    Serial.println("New Password: [hidden]");
    
    // Reset failed attempts counter
    failedAttempts = 0;
    isSuspended = false;
    
    // Send success page
    String successPanel = adminPanelPage;
    successPanel.replace("CURRENT_USER", ROOM_USER);
    successPanel.replace("</form>", 
        "<div class='success'>✓ Credentials updated successfully!</div></form>");
    server.send(200, "text/html", successPanel);
}