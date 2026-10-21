export class SessionManager {
  constructor(config, logger) {
    this.config = config;
    this.logger = logger;
    this.browserManager = null;
    this.pageController = null;
    this.captchaMonitor = null;
    this.isActive = false;
    this.sessionStartTime = null;
  }

  // Initialize session with all components
  async initialize(browserManager, pageController, captchaMonitor) {
    try {
      this.browserManager = browserManager;
      this.pageController = pageController;
      this.captchaMonitor = captchaMonitor;
      this.isActive = true;
      this.sessionStartTime = Date.now();

      this.logger.logSession();
      this.logger.info('SESSION', 'Session initialized', {
        persistent_context: this.config.browser.persistentContext,
        target_url: this.config.target.targetUrl,
      });

      return true;
    } catch (error) {
      this.logger.errorEvent('SessionManager', 'Initialization failed', error);
      return false;
    }
  }

  // Navigate to test URL
  async navigateToTest() {
    if (!this.isActive) {
      throw new Error('Session not active');
    }

    try {
      await this.pageController.navigate(this.config.target.targetUrl);
      await this.pageController.waitForPageReady();
      return true;
    } catch (error) {
      this.logger.errorEvent('SessionManager', 'Navigation to test URL failed', error);
      throw error;
    }
  }

  // Execute CAPTCHA test workflow
  async testCaptchaWorkflow() {
    if (!this.isActive) {
      throw new Error('Session not active');
    }

    try {
      this.logger.info('WORKFLOW', 'Starting CAPTCHA test workflow');

      // Step 1: Navigate to test page
      this.logger.info('WORKFLOW', 'Step 1: Navigating to test page');
      await this.navigateToTest();

      // Step 2: Wait for CAPTCHA to appear
      this.logger.info('WORKFLOW', 'Step 2: Waiting for CAPTCHA detection');
      const captchaDetected = await this.captchaMonitor.waitForCaptcha();

      if (!captchaDetected) {
        this.logger.warn('WORKFLOW', 'CAPTCHA not detected, continuing');
        return {
          success: true,
          captchaRequired: false,
          message: 'No CAPTCHA detected',
        };
      }

      // Step 3: Wait for manual completion
      this.logger.info('WORKFLOW', 'Step 3: CAPTCHA detected - waiting for manual completion');
      this.logger.info('WORKFLOW', 'Browser window is visible - please complete the CAPTCHA manually');

      const completed = await this.captchaMonitor.waitForCompletion();

      if (!completed) {
        return {
          success: false,
          captchaRequired: true,
          state: this.captchaMonitor.getState(),
          message: `CAPTCHA ${this.captchaMonitor.getState()}`,
        };
      }

      this.logger.info('WORKFLOW', 'Step 4: CAPTCHA completed successfully');
      return {
        success: true,
        captchaRequired: true,
        state: 'COMPLETED',
        message: 'CAPTCHA workflow completed',
        elapsedTime: this.captchaMonitor.getTimeSinceDetection(),
      };
    } catch (error) {
      this.logger.errorEvent('SessionManager', 'CAPTCHA workflow error', error);
      throw error;
    }
  }

  // Execute custom interaction sequence
  async executeInteractionSequence(steps) {
    if (!this.isActive) {
      throw new Error('Session not active');
    }

    const results = [];

    for (let i = 0; i < steps.length; i++) {
      const step = steps[i];
      try {
        this.logger.info('WORKFLOW', `Executing step ${i + 1}/${steps.length}: ${step.action}`);

        let result;
        switch (step.action) {
          case 'navigate':
            result = await this.pageController.navigate(step.url);
            break;

          case 'waitForElement':
            result = await this.pageController.waitForElement(step.selector, step.timeout);
            break;

          case 'click':
            result = await this.pageController.click(step.selector);
            break;

          case 'type':
            result = await this.pageController.type(step.selector, step.text);
            break;

          case 'moveMouse':
            result = await this.pageController.moveMouse(step.selector);
            break;

          case 'interact':
            result = await this.pageController.interactWithElement(step.selector);
            break;

          case 'screenshot':
            result = await this.pageController.takeScreenshot(step.name);
            break;

          case 'waitForCaptcha':
            result = await this.captchaMonitor.waitForCaptcha();
            break;

          case 'waitForCaptchaCompletion':
            result = await this.captchaMonitor.waitForCompletion();
            break;

          case 'delay':
            await new Promise(resolve => setTimeout(resolve, step.ms));
            result = true;
            break;

          default:
            throw new Error(`Unknown action: ${step.action}`);
        }

        results.push({
          step: i + 1,
          action: step.action,
          success: true,
          result: result,
        });

        this.logger.info('WORKFLOW', `Step ${i + 1} completed: ${step.action}`);
      } catch (error) {
        this.logger.errorEvent('SessionManager', `Step ${i + 1} failed`, error);

        if (step.continueOnError) {
          results.push({
            step: i + 1,
            action: step.action,
            success: false,
            error: error.message,
          });
        } else {
          throw new Error(`Workflow failed at step ${i + 1}: ${error.message}`);
        }
      }
    }

    return results;
  }

  // Take screenshot
  async screenshot(name = 'session-screenshot') {
    if (!this.isActive) {
      throw new Error('Session not active');
    }

    return await this.pageController.takeScreenshot(name);
  }

  // Get session duration
  getSessionDuration() {
    if (!this.sessionStartTime) return null;
    return Date.now() - this.sessionStartTime;
  }

  // Cleanup and close session
  async close() {
    if (!this.isActive) {
      return;
    }

    try {
      this.logger.info('SESSION', 'Closing session', {
        duration_ms: this.getSessionDuration(),
      });

      if (this.pageController) {
        await this.pageController.close();
      }

      if (this.browserManager) {
        await this.browserManager.close();
      }

      this.isActive = false;
      this.logger.logSessionEnd();
    } catch (error) {
      this.logger.errorEvent('SessionManager', 'Error during session close', error);
    }
  }

  // Pause execution and keep browser open (for debugging)
  async pauseForDebug(message = 'Automation paused for debugging') {
    this.logger.info('DEBUG', message);
    console.log('\n' + '='.repeat(60));
    console.log(message);
    console.log('Press ENTER to continue...');
    console.log('='.repeat(60));

    return new Promise(resolve => {
      process.stdin.once('data', () => {
        resolve();
      });
    });
  }

  // Check if session is active
  isSessionActive() {
    return this.isActive;
  }

  // Get session info
  getSessionInfo() {
    return {
      active: this.isActive,
      duration_ms: this.getSessionDuration(),
      startTime: this.sessionStartTime,
      captchaState: this.captchaMonitor?.getState(),
    };
  }
}

export default SessionManager;
