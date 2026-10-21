# Project Structure & Overview

Complete breakdown of the GTH Browser Automation Framework.

## Directory Tree

```
browser-automation/
├── Documentation
│   ├── README.md                  # Main documentation
│   ├── SETUP.md                   # Installation guide
│   ├── CAPTCHA_TESTING.md         # CAPTCHA testing guide
│   ├── QUICK_REFERENCE.md         # Quick reference
│   └── PROJECT_STRUCTURE.md       # This file
│
├── Source Code
│   └── src/
│       ├── index.js               # Main entry point & orchestration
│       ├── config.js              # Configuration management & validation
│       ├── AutomationLogger.js     # Logging system
│       ├── BrowserManager.js       # Browser lifecycle management
│       ├── PageController.js       # Page interaction & navigation
│       ├── CaptchaMonitor.js       # CAPTCHA detection & monitoring
│       └── SessionManager.js       # Test session orchestration
│
├── Tests
│   ├── tests/
│   │   ├── CaptchaMonitor.test.js # Unit tests for CAPTCHA monitor
│   │   └── integration.test.js    # Integration tests
│   │
│   └── scripts/
│       ├── manual-interaction-test.js       # Manual CAPTCHA test
│       ├── captcha-workflow-test.js         # Automated workflow test
│       └── custom-interaction-sequence.js   # Custom interaction example
│
├── Configuration
│   ├── .env.example               # Configuration template
│   ├── .env                       # Your configuration (created from template)
│   └── package.json               # Dependencies & scripts
│
├── Runtime Directories (Auto-created)
│   ├── logs/                      # Timestamped test logs
│   ├── screenshots/               # Test screenshots
│   ├── session-data/              # Browser session/cookies
│   └── videos/                    # Optional video recordings
│
└── Root Files
    ├── package.json               # NPM configuration
    ├── .env.example               # Configuration template
    └── README.md                  # Main documentation
```

## File-by-File Breakdown

### Configuration Files

#### `package.json`
- Project metadata
- Dependencies (Playwright, dotenv)
- NPM scripts for running tests
- Node version requirement (18+)

#### `.env.example`
- Template for all configuration options
- Comprehensive comments for each setting
- Copy to `.env` and customize for your environment

#### `.env`
- Your personalized configuration
- Never commit to version control
- Contains sensitive paths and URLs

### Source Code (src/)

#### `index.js` - Main Entry Point
**Class:** `GTHBrowserAutomation`

**Purpose:** Orchestrates all components
- Creates and initializes all modules
- Provides high-level API
- Manages cleanup

**Key Methods:**
```javascript
constructor(customConfig)        // Initialize with config
async initialize()               // Set up browser & components
async runCaptchaTest()           // Run full CAPTCHA workflow
async runInteractionSequence()   // Execute custom steps
async cleanup()                  // Clean shutdown
getSessionManager()              // Access session manager
getPageController()              // Access page controller
getLogger()                      // Access logger
```

**Usage:**
```javascript
const automation = new GTHBrowserAutomation();
await automation.initialize();
const result = await automation.runCaptchaTest();
```

#### `config.js` - Configuration Management
**Exports:** `config` object, `isAllowedHost()` function

**Purpose:** Centralized configuration
- Load from environment variables
- Provide defaults for all settings
- Validate security boundaries
- Type coercion for numeric values

**Sections:**
- `config.browser` - Browser settings
- `config.target` - Test target settings
- `config.captcha` - CAPTCHA configuration
- `config.interaction` - Mouse/keyboard timing
- `config.logging` - Log settings
- `config.retry` - Retry configuration
- `config.debug` - Debug flags

**Function:**
```javascript
isAllowedHost(url)  // Returns true if URL host is allowed
```

#### `AutomationLogger.js` - Logging System
**Class:** `AutomationLogger`

**Purpose:** Comprehensive logging with timestamps, levels, and file output
- Timestamped log entries
- Console + file output
- Log levels: DEBUG, INFO, WARN, ERROR
- Screenshot saving
- Session tracking

**Key Methods:**
```javascript
debug()              // Debug level logging
info()               // Info level logging
warn()               // Warning level logging
error()              // Error level logging
navigationEvent()    // Log navigation
captchaEvent()       // Log CAPTCHA state changes
interactionEvent()   // Log user interactions
saveScreenshot()     // Save screenshot to file
getLogFile()         // Get path to log file
getSessionId()       // Get unique session ID
```

#### `BrowserManager.js` - Browser Lifecycle
**Class:** `BrowserManager`

**Purpose:** Manage Playwright browser instance
- Launch/close browser
- Create contexts (persistent/non-persistent)
- Create pages
- Handle browser errors
- Support custom executable paths

**Key Methods:**
```javascript
async launch()       // Launch browser
async createContext() // Create browser context
async createPage()   // Create page within context
async getPage()      // Get current page
async close()        // Close everything
getBrowser()         // Access browser instance
getContext()         // Access context
isAlive()           // Check if browser is running
```

#### `PageController.js` - Page Interactions
**Class:** `PageController`

**Purpose:** Handle all page interactions and navigation
- Navigate to URLs with validation
- Wait for page ready
- Mouse movements with realistic timing
- Click, type, screenshot
- Element detection and inspection
- JavaScript execution

**Key Methods:**
```javascript
async navigate(url)                    // Navigate to URL
async waitForPageReady()               // Wait for networkidle
async waitForElement(selector)         // Wait for element
async click(selector)                  // Click element
async type(selector, text)             // Type text
async moveMouse(selector)              // Move mouse with timing
async interactWithElement(selector)    // Realistic: move + click
async takeScreenshot(name)             // Capture screenshot
async evaluate(script, ...args)        // Execute JavaScript
async getElementText(selector)         // Get element text
```

#### `CaptchaMonitor.js` - CAPTCHA Detection
**Class:** `CaptchaMonitor`
**Enum:** `CaptchaState`

**Purpose:** Detect and monitor CAPTCHA state
- Detect CAPTCHA presence
- Track state transitions
- Wait for manual completion
- Handle timeouts and failures

**States:**
```javascript
NOT_PRESENT          // No CAPTCHA detected
DETECTED            // CAPTCHA found
WAITING_FOR_USER    // Waiting for manual completion
COMPLETED           // CAPTCHA successfully solved
FAILED              // CAPTCHA failed/expired
EXPIRED             // Timeout waiting for completion
UNKNOWN             // Unknown state
```

**Key Methods:**
```javascript
async detectCaptcha()            // Check if CAPTCHA present
async waitForCaptcha()           // Wait for CAPTCHA to appear
async waitForCompletion()        // Wait for CAPTCHA completion
async checkCompletion()          // Check current completion state
getState()                       // Get current state
setState(state)                  // Set state (testing)
getTimeSinceDetection()          // Time elapsed since detection
reset()                          // Reset for next CAPTCHA
```

#### `SessionManager.js` - Session Management
**Class:** `SessionManager`

**Purpose:** Orchestrate test sessions and workflows
- Initialize components
- Execute CAPTCHA workflow
- Run custom interaction sequences
- Manage session lifecycle

**Key Methods:**
```javascript
async initialize(browserManager, pageController, captchaMonitor)
async navigateToTest()           // Navigate to test URL
async testCaptchaWorkflow()      // Full CAPTCHA test workflow
async executeInteractionSequence(steps) // Run custom steps
async screenshot(name)           // Take screenshot
async pauseForDebug(message)     // Pause for debugging
async close()                    // End session
getSessionDuration()             // Time elapsed
getSessionInfo()                 // Session metadata
isSessionActive()                // Check if running
```

### Tests (tests/ & scripts/)

#### `tests/CaptchaMonitor.test.js` - Unit Tests
- Tests initial state
- Tests state transitions
- Tests time tracking
- Tests reset functionality
- Uses Node's built-in `test` module

**Run:** `npm run test`

#### `tests/integration.test.js` - Integration Tests
- Tests host validation
- Tests configuration
- Tests timeout values
- Tests browser configuration defaults

**Run:** `npm run test:integration`

#### `scripts/manual-interaction-test.js` - Manual Test
- Opens browser window
- Navigates to test page
- Detects CAPTCHA
- Waits for user to complete CAPTCHA
- Takes screenshots at each step
- Reports results

**Run:** `npm run test:manual`

**Use for:**
- Development testing
- Verifying real CAPTCHA implementation
- Integration testing with production CAPTCHA

#### `scripts/captcha-workflow-test.js` - Automated Test
- Runs complete CAPTCHA workflow
- Uses test mode signals
- No manual interaction required
- Reports success/failure

**Run:** `npm run test:captcha`

**Use for:**
- CI/CD pipelines
- Automated testing
- Regression testing

#### `scripts/custom-interaction-sequence.js` - Custom Example
- Demonstrates custom interaction sequences
- Shows all available actions
- Navigation, clicking, typing, screenshots
- CAPTCHA testing within custom flow

**Run:** `npm run test:integration`

**Use for:**
- Building custom test scenarios
- Reference for interaction syntax

## Data Flow

### Initialization Flow

```
GTHBrowserAutomation.initialize()
├── BrowserManager.launch()
│   └── chromium.launch()
├── BrowserManager.createPage()
│   └── context.newPage()
├── PageController.__init__(page, config, logger)
├── CaptchaMonitor.__init__(page, config, logger)
└── SessionManager.initialize()
```

### CAPTCHA Test Workflow

```
SessionManager.testCaptchaWorkflow()
├── navigateToTest()
│   └── PageController.navigate() → Page loads
├── CaptchaMonitor.waitForCaptcha()
│   └── Check for CAPTCHA element (every 200ms)
├── CaptchaMonitor.waitForCompletion()
│   └── Check for success signal (every 500ms)
│       └── User manually solves CAPTCHA
└── Return results
```

### Custom Sequence Execution

```
SessionManager.executeInteractionSequence(steps)
├── For each step:
│   ├── Parse action type
│   ├── Execute appropriate PageController/CaptchaMonitor method
│   ├── Log result
│   └── Continue or error
└── Return results array
```

## Component Dependencies

```
GTHBrowserAutomation
├── depends on: BrowserManager, PageController, CaptchaMonitor, SessionManager
│
SessionManager
├── depends on: BrowserManager, PageController, CaptchaMonitor
│
PageController
├── depends on: Playwright Page, config, AutomationLogger
│
CaptchaMonitor
├── depends on: Playwright Page, config, AutomationLogger
│
BrowserManager
├── depends on: Playwright chromium, config, AutomationLogger
│
AutomationLogger
├── depends on: fs, path (Node.js standard)
│
config
└── depends on: dotenv, environment variables
```

## Configuration Cascade

```
Defaults (src/config.js)
     ↓
Environment Variables (.env)
     ↓
Constructor Config (GTHBrowserAutomation)
     ↓
Active Configuration
```

## Logging Architecture

```
All Components
     ↓ (log events)
AutomationLogger
     ├→ Console output
     ├→ File: ./logs/automation-{sessionId}.log
     └→ Screenshots: ./screenshots/{sessionId}_{name}.png
```

## Security Model

**Host Validation:**
```
any URL
     ↓
PageController.navigate(url)
     ↓
isAllowedHost(url)
     ├─ Success → navigate
     └─ Failure → throw error
```

## Timeouts & Delays

```
Navigation           → NAVIGATION_TIMEOUT (30s)
Page Ready          → PAGE_READY_TIMEOUT (10s)
CAPTCHA Detection   → CAPTCHA_DETECTION_TIMEOUT (15s)
CAPTCHA Completion  → CAPTCHA_COMPLETION_TIMEOUT (5min)
Mouse Move          → MOUSE_MOVE_DELAY (300ms)
Interaction Delay   → INTERACTION_DELAY (100ms)
Click Wait          → CLICK_WAIT_DELAY (200ms)
```

## Performance Characteristics

| Operation | Typical Duration |
|-----------|-----------------|
| Browser launch | 500-2000ms |
| Page navigation | 1000-5000ms |
| CAPTCHA detection | 200-500ms |
| Element click | 50-100ms |
| Text input | 50-150ms per character |
| Screenshot | 100-300ms |
| Manual CAPTCHA solve | 5-30 seconds |

## Error Handling

All components include:
- Try/catch error handling
- Descriptive error messages
- Logging of errors with context
- Graceful degradation where possible
- Cleanup on failure

**Error Flow:**
```
Error occurs
     ↓
Caught by component
     ↓
Logged with context
     ↓
Propagated or handled gracefully
     ↓
Session cleanup if needed
```

## Testing Strategy

1. **Unit Tests** - Test individual components in isolation
2. **Integration Tests** - Test component interactions
3. **Manual Tests** - Test with real CAPTCHA
4. **Workflow Tests** - Test complete workflows
5. **Custom Tests** - Test application-specific scenarios

## Extension Points

To extend the framework:

1. **Add new component:** Extend classes in `src/`
2. **Add new interaction:** Add method to `PageController`
3. **Add new action:** Add case to `SessionManager.executeInteractionSequence()`
4. **Add new logging:** Use `AutomationLogger` methods
5. **Add new test:** Create script in `scripts/`

## Security Boundaries

1. **Host validation** - Only allowed hosts can be tested
2. **Configuration** - No hardcoded secrets
3. **No bypass logic** - Cannot defeat external CAPTCHAs
4. **Manual completion** - Real CAPTCHA requires human interaction
5. **Test signals only** - Automation works with test mode signals

## Future Enhancements

Possible additions:
- Video recording during tests
- Performance metrics collection
- Multi-browser support (Firefox, WebKit)
- Cloud-based testing integration
- Advanced reporting and dashboards
- API server for remote control
- WebSocket support for real-time monitoring
- Screenshot comparison for visual regression
- Mobile device emulation
- Network throttling simulation
