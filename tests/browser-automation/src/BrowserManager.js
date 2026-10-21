import { chromium } from 'playwright';
import path from 'path';

export class BrowserManager {
  constructor(config, logger) {
    this.config = config;
    this.logger = logger;
    this.browser = null;
    this.context = null;
    this.page = null;
  }

  // Launch browser with specified configuration
  async launch() {
    try {
      const launchOptions = {
        headless: this.config.browser.headless,
      };

      // Set executable path if specified (non-default Chrome/Chromium)
      if (this.config.browser.executablePath) {
        launchOptions.executablePath = this.config.browser.executablePath;
        this.logger.debug('BROWSER', 'Using custom executable path', {
          path: this.config.browser.executablePath,
        });
      }

      this.browser = await chromium.launch(launchOptions);
      this.logger.info('BROWSER', 'Browser launched successfully', {
        headless: this.config.browser.headless,
      });

      return this.browser;
    } catch (error) {
      this.logger.errorEvent('BrowserManager', 'Failed to launch browser', error);
      throw new Error(`Failed to launch browser: ${error.message}`);
    }
  }

  // Create a context (persistent or non-persistent)
  async createContext() {
    if (!this.browser) {
      throw new Error('Browser not launched. Call launch() first.');
    }

    try {
      const contextOptions = {
        viewport: {
          width: this.config.browser.viewportWidth,
          height: this.config.browser.viewportHeight,
        },
      };

      // If using persistent context, set user data directory
      if (this.config.browser.persistentContext) {
        this.context = await this.browser.newContext({
          ...contextOptions,
          // Store cookies and session data
        });
        this.logger.info('BROWSER', 'Created new context (session data managed)', {
          persistent: true,
        });
      } else {
        this.context = await this.browser.newContext(contextOptions);
        this.logger.info('BROWSER', 'Created new context (non-persistent)', {
          persistent: false,
        });
      }

      return this.context;
    } catch (error) {
      this.logger.errorEvent('BrowserManager', 'Failed to create context', error);
      throw new Error(`Failed to create context: ${error.message}`);
    }
  }

  // Create a page within the context
  async createPage() {
    if (!this.context) {
      await this.createContext();
    }

    try {
      this.page = await this.context.newPage();
      this.logger.info('BROWSER', 'Page created successfully');

      // Set up event listeners for logging
      this.page.on('console', msg => {
        if (this.config.debug.verbose) {
          this.logger.debug('PAGE_CONSOLE', msg.type(), { text: msg.text() });
        }
      });

      this.page.on('crash', () => {
        this.logger.error('PAGE', 'Page crashed');
      });

      return this.page;
    } catch (error) {
      this.logger.errorEvent('BrowserManager', 'Failed to create page', error);
      throw new Error(`Failed to create page: ${error.message}`);
    }
  }

  // Get the current page (create if not exists)
  async getPage() {
    if (!this.page) {
      await this.createPage();
    }
    return this.page;
  }

  // Close the page
  async closePage() {
    if (this.page) {
      try {
        await this.page.close();
        this.page = null;
        this.logger.info('BROWSER', 'Page closed');
      } catch (error) {
        this.logger.warn('BROWSER', 'Error closing page', { error: error.message });
      }
    }
  }

  // Close the context
  async closeContext() {
    if (this.context) {
      try {
        await this.context.close();
        this.context = null;
        this.logger.info('BROWSER', 'Context closed');
      } catch (error) {
        this.logger.warn('BROWSER', 'Error closing context', { error: error.message });
      }
    }
  }

  // Close browser
  async close() {
    try {
      await this.closePage();
      await this.closeContext();

      if (this.browser) {
        await this.browser.close();
        this.browser = null;
        this.logger.info('BROWSER', 'Browser closed');
      }
    } catch (error) {
      this.logger.errorEvent('BrowserManager', 'Error during browser close', error);
    }
  }

  // Reuse existing browser session
  reuseBrowser(browser, context, page) {
    this.browser = browser;
    this.context = context;
    this.page = page;
    this.logger.info('BROWSER', 'Browser session reused');
  }

  // Get browser instance
  getBrowser() {
    return this.browser;
  }

  // Get context instance
  getContext() {
    return this.context;
  }

  // Get page instance
  getPage() {
    return this.page;
  }

  // Check if browser is alive
  isAlive() {
    return this.browser !== null && !this.browser.isConnected?.() === false;
  }
}

export default BrowserManager;
