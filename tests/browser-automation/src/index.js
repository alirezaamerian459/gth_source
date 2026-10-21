import config from './config.js';
import AutomationLogger from './AutomationLogger.js';
import BrowserManager from './BrowserManager.js';
import PageController from './PageController.js';
import CaptchaMonitor, { CaptchaState } from './CaptchaMonitor.js';
import SessionManager from './SessionManager.js';

export class GTHBrowserAutomation {
  constructor(customConfig = {}) {
    this.config = { ...config, ...customConfig };
    this.logger = new AutomationLogger(this.config);
    this.browserManager = new BrowserManager(this.config, this.logger);
    this.pageController = null;
    this.captchaMonitor = null;
    this.sessionManager = new SessionManager(this.config, this.logger);
  }

  // Initialize automation framework
  async initialize() {
    try {
      // Launch browser
      await this.browserManager.launch();

      // Create page
      const page = await this.browserManager.createPage();

      // Initialize controllers
      this.pageController = new PageController(page, this.config, this.logger);
      this.captchaMonitor = new CaptchaMonitor(page, this.config, this.logger);

      // Initialize session manager
      await this.sessionManager.initialize(
        this.browserManager,
        this.pageController,
        this.captchaMonitor
      );

      return true;
    } catch (error) {
      this.logger.errorEvent('GTHBrowserAutomation', 'Initialization failed', error);
      throw error;
    }
  }

  // Run CAPTCHA test workflow
  async runCaptchaTest() {
    try {
      await this.initialize();
      const result = await this.sessionManager.testCaptchaWorkflow();
      return result;
    } finally {
      if (!this.config.debug.keepBrowserOpen) {
        await this.cleanup();
      } else {
        this.logger.info('DEBUG', 'Browser kept open for debugging');
      }
    }
  }

  // Run custom interaction sequence
  async runInteractionSequence(steps) {
    try {
      await this.initialize();
      const results = await this.sessionManager.executeInteractionSequence(steps);
      return results;
    } finally {
      if (!this.config.debug.keepBrowserOpen) {
        await this.cleanup();
      }
    }
  }

  // Get components for advanced usage
  getSessionManager() {
    return this.sessionManager;
  }

  getPageController() {
    return this.pageController;
  }

  getCaptchaMonitor() {
    return this.captchaMonitor;
  }

  getBrowserManager() {
    return this.browserManager;
  }

  getLogger() {
    return this.logger;
  }

  // Cleanup
  async cleanup() {
    try {
      await this.sessionManager.close();
    } catch (error) {
      this.logger.errorEvent('GTHBrowserAutomation', 'Cleanup error', error);
    }
  }

  // Get log file location
  getLogFile() {
    return this.logger.getLogFile();
  }

  // Get session ID
  getSessionId() {
    return this.logger.getSessionId();
  }
}

// Export all components for advanced usage
export {
  config,
  AutomationLogger,
  BrowserManager,
  PageController,
  CaptchaMonitor,
  CaptchaState,
  SessionManager,
};

export default GTHBrowserAutomation;
