import path from 'path';
import { fileURLToPath } from 'url';
import dotenv from 'dotenv';

// Load environment variables from .env file if it exists
dotenv.config();

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export const config = {
  // Browser configuration
  browser: {
    // Comma-separated list of allowed test domains (security boundary)
    allowedHosts: (process.env.ALLOWED_HOSTS || 'localhost,127.0.0.1,gth.local').split(',').map(h => h.trim()),

    // Browser executable path - leave null for default Chromium
    executablePath: process.env.BROWSER_EXECUTABLE_PATH || null,

    // Launch in headless mode (false for debugging)
    headless: process.env.HEADLESS !== 'false',

    // Use persistent context (reuses session across navigation)
    persistentContext: process.env.PERSISTENT_CONTEXT !== 'false',

    // User data directory for persistent context
    userDataDirectory: process.env.USER_DATA_DIR ||
      path.join(__dirname, '..', 'session-data'),

    // Browser window size for visual testing
    viewportWidth: parseInt(process.env.VIEWPORT_WIDTH || '1280', 10),
    viewportHeight: parseInt(process.env.VIEWPORT_HEIGHT || '720', 10),
  },

  // Test target configuration
  target: {
    // Base URL for test target
    targetUrl: process.env.TARGET_URL || 'http://localhost:8080',

    // Navigation timeout in milliseconds
    navigationTimeout: parseInt(process.env.NAVIGATION_TIMEOUT || '30000', 10),

    // Wait for page ready timeout
    pageReadyTimeout: parseInt(process.env.PAGE_READY_TIMEOUT || '10000', 10),
  },

  // CAPTCHA configuration
  captcha: {
    // CAPTCHA detection timeout (how long to wait for CAPTCHA to appear)
    detectionTimeout: parseInt(process.env.CAPTCHA_DETECTION_TIMEOUT || '15000', 10),

    // CAPTCHA completion timeout (how long to wait for manual completion)
    completionTimeout: parseInt(process.env.CAPTCHA_COMPLETION_TIMEOUT || '300000', 10), // 5 minutes

    // CAPTCHA test mode enabled (looks for test-mode signals)
    testModeEnabled: process.env.CAPTCHA_TEST_MODE === 'true',

    // CAPTCHA success signal selector (in test mode)
    successSelector: process.env.CAPTCHA_SUCCESS_SELECTOR || '[data-captcha-success]',

    // CAPTCHA completion signal selector
    completionSelector: process.env.CAPTCHA_COMPLETION_SELECTOR || '[data-captcha-completed]',

    // CAPTCHA failure signal selector
    failureSelector: process.env.CAPTCHA_FAILURE_SELECTOR || '[data-captcha-failed]',
  },

  // Interaction configuration
  interaction: {
    // Optional pause between mouse actions (milliseconds)
    interactionDelay: parseInt(process.env.INTERACTION_DELAY || '100', 10),

    // Mouse move duration in milliseconds
    mouseMoveDelay: parseInt(process.env.MOUSE_MOVE_DELAY || '300', 10),

    // Wait time after click before next action
    clickWaitDelay: parseInt(process.env.CLICK_WAIT_DELAY || '200', 10),
  },

  // Logging configuration
  logging: {
    // Log directory
    logDir: process.env.LOG_DIR || path.join(__dirname, '..', 'logs'),

    // Log level: 'debug', 'info', 'warn', 'error'
    level: process.env.LOG_LEVEL || 'info',

    // Enable screenshot capture on errors
    screenshotOnError: process.env.SCREENSHOT_ON_ERROR !== 'false',

    // Screenshot directory
    screenshotDir: process.env.SCREENSHOT_DIR ||
      path.join(__dirname, '..', 'screenshots'),

    // Enable video recording
    recordVideo: process.env.RECORD_VIDEO === 'true',

    // Video directory
    videoDir: process.env.VIDEO_DIR ||
      path.join(__dirname, '..', 'videos'),
  },

  // Retry configuration
  retry: {
    // Maximum retries for failed operations
    maxRetries: parseInt(process.env.MAX_RETRIES || '3', 10),

    // Delay between retries in milliseconds
    retryDelay: parseInt(process.env.RETRY_DELAY || '1000', 10),
  },

  // Debug configuration
  debug: {
    // Enable verbose debug logging
    verbose: process.env.DEBUG === 'true',

    // Print network traffic
    networkDebug: process.env.NETWORK_DEBUG === 'true',

    // Keep browser open after test (for debugging)
    keepBrowserOpen: process.env.KEEP_BROWSER_OPEN === 'true',
  },
};

// Validate host against allowed list
export function isAllowedHost(url) {
  try {
    const urlObj = new URL(url);
    const hostname = urlObj.hostname;
    return config.browser.allowedHosts.some(host => {
      if (host === '*') return true; // Allow all (not recommended)
      if (host.startsWith('*.')) {
        // Wildcard domain matching
        return hostname.endsWith(host.substring(1));
      }
      return hostname === host || hostname.endsWith(`.${host}`);
    });
  } catch (err) {
    return false;
  }
}

export default config;
