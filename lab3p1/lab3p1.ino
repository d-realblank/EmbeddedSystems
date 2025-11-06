#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "soc/rtc_cntl_reg.h" // Needed for brownout detector functions

// WiFi credentials - Update these with your network details
const char* ssid = "DayveidT-T";
const char* password = "12345678";

// Web server on port 80
WebServer server(80);

// Pin definitions
const int GREEN_LED_PIN = 22;  // Update with your actual pin
const int RED_LED_PIN = 21;    // Update with your actual pin
const int SERVO_PIN = 13;      // Update with your actual pin

// Servo object
Servo doorServo;

// Authentication credentials (stored locally) - using String for dynamic updates
String ROOM_USER = "dayveid";
String ROOM_PASSWORD = "1234";

// Admin credentials (for changing the access code)
const String ADMIN_USER = "admin";
const String ADMIN_PASSWORD = "admin123";

// Authentication tracking
int failedAttempts = 0;
unsigned long suspensionStartTime = 0;
const unsigned long SUSPENSION_DURATION = 120000; // 2 minutes in milliseconds
bool isSuspended = false;

// HTML page with login form
const char* loginPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Room Access Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h2 {
            text-align: center;
            color: #333;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            margin: 8px 0;
            box-sizing: border-box;
            border: 1px solid #ddd;
            border-radius: 4px;
        }
        input[type="submit"] {
            width: 100%;
            background-color: #4CAF50;
            color: white;
            padding: 14px;
            margin: 8px 0;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
        }
        input[type="submit"]:hover {
            background-color: #45a049;
        }
        .error {
            color: red;
            text-align: center;
            margin-top: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Room Access Control</h2>
        <form action="/login" method="POST">
            <label for="username">Username:</label>
            <input type="text" id="username" name="username" required>
            
            <label for="password">Password:</label>
            <input type="password" id="password" name="password" required>
            
            <input type="submit" value="Authenticate">
        </form>
    </div>
</body>
</html>
)rawliteral";

// Suspension page
const char* suspensionPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Access Suspended</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            text-align: center;
        }
        h2 {
            color: #d32f2f;
        }
        p {
            color: #666;
            line-height: 1.6;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Access Suspended</h2>
        <p>Too many failed login attempts.</p>
        <p>Please wait 2 minutes before trying again.</p>
    </div>
</body>
</html>
)rawliteral";

// Success page
const char* successPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Access Granted</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            text-align: center;
        }
        h2 {
            color: #4CAF50;
        }
        p {
            color: #666;
            line-height: 1.6;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Access Granted</h2>
        <p>Welcome! The door is now unlocked.</p>
    </div>
</body>
</html>
)rawliteral";

// Admin login page
const char* adminLoginPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Admin Login</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h2 {
            text-align: center;
            color: #333;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            margin: 8px 0;
            box-sizing: border-box;
            border: 1px solid #ddd;
            border-radius: 4px;
        }
        input[type="submit"] {
            width: 100%;
            background-color: #FF9800;
            color: white;
            padding: 14px;
            margin: 8px 0;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
        }
        input[type="submit"]:hover {
            background-color: #F57C00;
        }
        .error {
            color: red;
            text-align: center;
            margin-top: 10px;
        }
        .back-link {
            text-align: center;
            margin-top: 15px;
        }
        .back-link a {
            color: #666;
            text-decoration: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Admin Access</h2>
        <form action="/admin/login" method="POST">
            <label for="username">Admin Username:</label>
            <input type="text" id="username" name="username" required>
            
            <label for="password">Admin Password:</label>
            <input type="password" id="password" name="password" required>
            
            <input type="submit" value="Admin Login">
        </form>
        <div class="back-link">
            <a href="/">Back to User Login</a>
        </div>
    </div>
</body>
</html>
)rawliteral";

// Admin panel page
const char* adminPanelPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Admin Panel</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h2 {
            text-align: center;
            color: #333;
        }
        .info-box {
            background-color: #E3F2FD;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        .info-box p {
            margin: 5px 0;
            color: #1976D2;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            margin: 8px 0;
            box-sizing: border-box;
            border: 1px solid #ddd;
            border-radius: 4px;
        }
        input[type="submit"] {
            width: 100%;
            background-color: #FF9800;
            color: white;
            padding: 14px;
            margin: 8px 0;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
        }
        input[type="submit"]:hover {
            background-color: #F57C00;
        }
        .success {
            color: green;
            text-align: center;
            margin-top: 10px;
            font-weight: bold;
        }
        .back-link {
            text-align: center;
            margin-top: 15px;
        }
        .back-link a {
            color: #666;
            text-decoration: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>⚙️ Admin Panel</h2>
        <div class="info-box">
            <p><strong>Current Username:</strong> CURRENT_USER</p>
            <p><strong>Current Password:</strong> ••••••</p>
        </div>
        <h3>Change Access Credentials</h3>
        <form action="/admin/update" method="POST">
            <label for="newUsername">New Username:</label>
            <input type="text" id="newUsername" name="newUsername" required>
            
            <label for="newPassword">New Password:</label>
            <input type="password" id="newPassword" name="newPassword" required>
            
            <label for="confirmPassword">Confirm Password:</label>
            <input type="password" id="confirmPassword" name="confirmPassword" required>
            
            <input type="submit" value="Update Credentials">
        </form>
        <div class="back-link">
            <a href="/">← Back to User Login</a>
        </div>
    </div>
</body>
</html>
)rawliteral";

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
    Serial.begin(115200);
    delay(1000); // Give serial monitor time to connect
    
    // Initialize LED pins FIRST, before servo
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    
    Serial.println("Initializing servo...");
    
    // Allow allocation of all timers
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    
    Serial.print("Attaching servo to pin ");
    Serial.println(SERVO_PIN);
    
    // Attach servo with standard parameters
    doorServo.setPeriodHertz(50);    // Standard 50 Hz servo
    doorServo.attach(SERVO_PIN, 500, 2400);
    
    // Check if servo is attached
    if (doorServo.attached()) {
        Serial.println("✓ Servo attached successfully!");
        
        // Set initial locked position
        doorServo.write(0);
        delay(500); // Wait for servo to reach position
        Serial.println("Servo initialized and locked at 0 degrees");
    } else {
        Serial.println("✗ ERROR: Servo failed to attach!");
        Serial.println("Try changing SERVO_PIN to: 12, 13, 14, 15, 16, 17, 18, 19, 25, 26, 27, or 32");
    }
    
    // Verify LEDs are still off after servo initialization
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        Serial.print(".");
        wifiTimeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/admin", handleAdminRoot);
    server.on("/admin/login", HTTP_POST, handleAdminLogin);
    server.on("/admin/panel", handleAdminPanel);
    server.on("/admin/update", HTTP_POST, handleAdminUpdate);
    server.onNotFound(handleNotFound);
    
    // Start server
    server.begin();
    Serial.println("Web server started");
    Serial.println("System ready for authentication");
}

void loop() {
    // Check if suspension period has ended
    if (isSuspended) {
        if (millis() - suspensionStartTime >= SUSPENSION_DURATION) {
            isSuspended = false;
            failedAttempts = 0;
            Serial.println("Suspension period ended. System ready.");
        }
    }
    
    // Handle web server requests
    server.handleClient();
}

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