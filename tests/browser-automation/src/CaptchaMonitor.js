export const CaptchaState = {
  NOT_PRESENT: 'NOT_PRESENT',
  DETECTED: 'DETECTED',
  WAITING_FOR_USER: 'WAITING_FOR_USER',
  COMPLETED: 'COMPLETED',
  FAILED: 'FAILED',
  EXPIRED: 'EXPIRED',
  UNKNOWN: 'UNKNOWN',
};

export class CaptchaMonitor {
  constructor(page, config, logger) {
    this.page = page;
    this.config = config;
    this.logger = logger;
    this.currentState = CaptchaState.NOT_PRESENT;
    this.detectedAt = null;
  }

  // Detect if CAPTCHA is present on the page
  async detectCaptcha() {
    try {
      // Check common CAPTCHA framework selectors and data attributes
      const selectors = [
        '[data-captcha]',
        '[data-recaptcha]',
        '[class*="captcha"]',
        '[id*="captcha"]',
        '.g-recaptcha',
        '.h-captcha',
        '[data-hcaptcha]',
        'iframe[title*="captcha" i]',
        'iframe[src*="recaptcha" i]',
        'iframe[src*="hcaptcha" i]',
      ];

      for (const selector of selectors) {
        const exists = await this.page.$(selector);
        if (exists) {
          this.logger.debug('CAPTCHA', `Detected CAPTCHA using selector: ${selector}`);
          this.currentState = CaptchaState.DETECTED;
          this.detectedAt = Date.now();
          this.logger.captchaEvent(this.currentState, { selector });
          return true;
        }
      }

      this.currentState = CaptchaState.NOT_PRESENT;
      return false;
    } catch (error) {
      this.logger.errorEvent('CaptchaMonitor', 'CAPTCHA detection failed', error);
      this.currentState = CaptchaState.UNKNOWN;
      return false;
    }
  }

  // Wait for CAPTCHA to appear
  async waitForCaptcha() {
    const timeout = this.config.captcha.detectionTimeout;
    const startTime = Date.now();

    return new Promise((resolve) => {
      const checkInterval = setInterval(async () => {
        const detected = await this.detectCaptcha();
        const elapsed = Date.now() - startTime;

        if (detected) {
          clearInterval(checkInterval);
          this.logger.info('CAPTCHA', 'CAPTCHA detected and waiting for user', {
            detection_time_ms: elapsed,
          });
          this.currentState = CaptchaState.WAITING_FOR_USER;
          resolve(true);
        }

        if (elapsed > timeout) {
          clearInterval(checkInterval);
          this.logger.warn('CAPTCHA', 'CAPTCHA detection timeout', {
            timeout_ms: timeout,
            elapsed_ms: elapsed,
          });
          this.logger.captchaEvent(CaptchaState.NOT_PRESENT, {
            reason: 'timeout',
          });
          resolve(false);
        }
      }, 200);
    });
  }

  // Wait for CAPTCHA completion (manual or automatic)
  async waitForCompletion() {
    if (this.currentState === CaptchaState.NOT_PRESENT) {
      this.logger.info('CAPTCHA', 'No CAPTCHA present, skipping completion wait');
      return true;
    }

    const timeout = this.config.captcha.completionTimeout;
    const startTime = Date.now();

    return new Promise((resolve) => {
      const checkInterval = setInterval(async () => {
        const status = await this.checkCompletion();
        const elapsed = Date.now() - startTime;

        if (status === CaptchaState.COMPLETED) {
          clearInterval(checkInterval);
          this.logger.info('CAPTCHA', 'CAPTCHA completed successfully', {
            total_time_ms: elapsed,
          });
          resolve(true);
        }

        if (status === CaptchaState.FAILED || status === CaptchaState.EXPIRED) {
          clearInterval(checkInterval);
          this.logger.warn('CAPTCHA', `CAPTCHA ${status}`, {
            total_time_ms: elapsed,
          });
          this.currentState = status;
          this.logger.captchaEvent(status, { elapsed_ms: elapsed });
          resolve(false);
        }

        if (elapsed > timeout) {
          clearInterval(checkInterval);
          this.logger.warn('CAPTCHA', 'CAPTCHA completion timeout', {
            timeout_ms: timeout,
            elapsed_ms: elapsed,
            state: this.currentState,
          });
          this.logger.captchaEvent(CaptchaState.EXPIRED, {
            reason: 'manual_completion_timeout',
          });
          resolve(false);
        }
      }, 500);
    });
  }

  // Check current CAPTCHA completion status
  async checkCompletion() {
    try {
      if (this.config.captcha.testModeEnabled) {
        // In test mode, look for explicit test signals
        const successElement = await this.page.$(this.config.captcha.successSelector);
        if (successElement) {
          this.currentState = CaptchaState.COMPLETED;
          return this.currentState;
        }

        const failureElement = await this.page.$(this.config.captcha.failureSelector);
        if (failureElement) {
          this.currentState = CaptchaState.FAILED;
          return this.currentState;
        }

        const completionElement = await this.page.$(this.config.captcha.completionSelector);
        if (completionElement) {
          this.currentState = CaptchaState.COMPLETED;
          return this.currentState;
        }
      } else {
        // In production mode, check for common completion indicators
        // These are framework-specific and may need adjustment

        // Check for hidden CAPTCHA element (often hidden after success)
        const captchaElement = await this.page.$('[data-captcha]');
        if (!captchaElement) {
          // Element might have been removed after success
          this.currentState = CaptchaState.COMPLETED;
          return this.currentState;
        }

        // Check for success class/attribute
        const successAttr = await this.page.$('[data-captcha-success]');
        if (successAttr) {
          this.currentState = CaptchaState.COMPLETED;
          return this.currentState;
        }

        // Check for error/expired class
        const errorAttr = await this.page.$('[data-captcha-error], [data-captcha-expired]');
        if (errorAttr) {
          this.currentState = CaptchaState.FAILED;
          return this.currentState;
        }
      }

      return this.currentState;
    } catch (error) {
      this.logger.debug('CAPTCHA', 'Error checking completion status', {
        error: error.message,
      });
      return CaptchaState.UNKNOWN;
    }
  }

  // Get current state
  getState() {
    return this.currentState;
  }

  // Set state (for testing)
  setState(state) {
    this.currentState = state;
    this.logger.captchaEvent(state, { manually_set: true });
  }

  // Get time since detection
  getTimeSinceDetection() {
    if (!this.detectedAt) return null;
    return Date.now() - this.detectedAt;
  }

  // Reset monitor for next CAPTCHA
  reset() {
    this.currentState = CaptchaState.NOT_PRESENT;
    this.detectedAt = null;
    this.logger.debug('CAPTCHA', 'Monitor reset');
  }
}

export default CaptchaMonitor;
