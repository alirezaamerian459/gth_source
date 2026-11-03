import { isAllowedHost } from './config.js';

export class PageController {
  constructor(page, config, logger) {
    this.page = page;
    this.config = config;
    this.logger = logger;
  }

  // Navigate to a URL with validation
  async navigate(url) {
    // Validate host against security boundary
    if (!isAllowedHost(url)) {
      throw new Error(`Host not allowed: ${url}`);
    }

    const startTime = Date.now();
    try {
      await this.page.goto(url, {
        waitUntil: 'domcontentloaded',
        timeout: this.config.target.navigationTimeout,
      });

      const duration = Date.now() - startTime;
      this.logger.navigationEvent(url, duration, 'success');
      return true;
    } catch (error) {
      const duration = Date.now() - startTime;
      this.logger.navigationEvent(url, duration, 'failed');
      this.logger.errorEvent('PageController', `Navigation failed: ${url}`, error);
      throw error;
    }
  }

  // Wait for page to be fully loaded and ready
  async waitForPageReady() {
    try {
      await this.page.waitForLoadState('networkidle', {
        timeout: this.config.target.pageReadyTimeout,
      });
      this.logger.info('PAGE', 'Page ready (network idle)');
      return true;
    } catch (error) {
      this.logger.warn('PAGE', 'Page ready timeout (continuing anyway)', {
        timeout_ms: this.config.target.pageReadyTimeout,
      });
      // Don't throw - page might be ready enough for interaction
      return false;
    }
  }

  // Wait for a specific element to appear
  async waitForElement(selector, timeout = null) {
    const actualTimeout = timeout || this.config.target.pageReadyTimeout;
    try {
      await this.page.waitForSelector(selector, { timeout: actualTimeout });
      this.logger.debug('PAGE', `Element found: ${selector}`);
      return true;
    } catch (error) {
      this.logger.warn('PAGE', `Element not found: ${selector}`, {
        timeout_ms: actualTimeout,
      });
      return false;
    }
  }

  // Check if element exists without waiting
  async elementExists(selector) {
    try {
      const element = await this.page.$(selector);
      return element !== null;
    } catch {
      return false;
    }
  }

  // Click on an element
  async click(selector) {
    const startTime = Date.now();
    try {
      await this.page.click(selector, {
        force: false,
        timeout: this.config.target.pageReadyTimeout,
      });
      const duration = Date.now() - startTime;
      this.logger.interactionEvent('CLICK', selector, duration);
      await this._delay(this.config.interaction.clickWaitDelay);
      return true;
    } catch (error) {
      const duration = Date.now() - startTime;
      this.logger.errorEvent('PageController', `Click failed on ${selector}`, error);
      throw error;
    }
  }

  // Type text into an element
  async type(selector, text, options = {}) {
    const startTime = Date.now();
    try {
      await this.page.fill(selector, text);
      const duration = Date.now() - startTime;
      this.logger.interactionEvent('TYPE', selector, duration);
      await this._delay(this.config.interaction.interactionDelay);
      return true;
    } catch (error) {
      const duration = Date.now() - startTime;
      this.logger.errorEvent('PageController', `Type failed on ${selector}`, error);
      throw error;
    }
  }

  // Move mouse to element with configurable timing
  async moveMouse(selector) {
    const startTime = Date.now();
    try {
      const element = await this.page.$(selector);
      if (!element) {
        throw new Error(`Element not found: ${selector}`);
      }

      const box = await element.boundingBox();
      if (!box) {
        throw new Error(`Element has no bounding box: ${selector}`);
      }

      const x = box.x + box.width / 2;
      const y = box.y + box.height / 2;

      await this.page.mouse.move(x, y);
      await this._delay(this.config.interaction.mouseMoveDelay);

      const duration = Date.now() - startTime;
      this.logger.interactionEvent('MOUSE_MOVE', selector, duration);
      return { x, y };
    } catch (error) {
      const duration = Date.now() - startTime;
      this.logger.errorEvent('PageController', `Mouse move failed on ${selector}`, error);
      throw error;
    }
  }

  // Convenience interaction: move, pause, then click
  async interactWithElement(selector) {
    try {
      await this.moveMouse(selector);
      await this._delay(this.config.interaction.interactionDelay);
      await this.click(selector);
      return true;
    } catch (error) {
      this.logger.errorEvent('PageController', `Realistic interaction failed on ${selector}`, error);
      throw error;
    }
  }

  // Get element text content
  async getElementText(selector) {
    try {
      const element = await this.page.$(selector);
      if (!element) {
        return null;
      }
      return await element.textContent();
    } catch (error) {
      this.logger.debug('PAGE', `Failed to get text from ${selector}`, { error: error.message });
      return null;
    }
  }

  // Get element attribute
  async getElementAttribute(selector, attribute) {
    try {
      const element = await this.page.$(selector);
      if (!element) {
        return null;
      }
      return await element.getAttribute(attribute);
    } catch (error) {
      this.logger.debug('PAGE', `Failed to get attribute from ${selector}`, { error: error.message });
      return null;
    }
  }

  // Execute JavaScript in page context
  async evaluate(script, ...args) {
    try {
      return await this.page.evaluate(script, ...args);
    } catch (error) {
      this.logger.errorEvent('PageController', 'Script evaluation failed', error);
      throw error;
    }
  }

  // Take screenshot
  async takeScreenshot(name = 'screenshot') {
    try {
      const data = await this.page.screenshot({ fullPage: true });
      const filepath = this.logger.saveScreenshot(data, name);
      return filepath;
    } catch (error) {
      this.logger.errorEvent('PageController', 'Screenshot failed', error);
      throw error;
    }
  }

  // Get page URL
  async getUrl() {
    return this.page.url();
  }

  // Wait for function to return true
  async waitFor(fn, timeout = null) {
    const actualTimeout = timeout || this.config.target.pageReadyTimeout;
    const startTime = Date.now();

    return new Promise((resolve, reject) => {
      const interval = setInterval(async () => {
        try {
          const result = await fn();
          if (result) {
            clearInterval(interval);
            clearTimeout(timeoutHandle);
            resolve(result);
          }
        } catch (error) {
          // Ignore errors during polling
        }
      }, 100);

      const timeoutHandle = setTimeout(() => {
        clearInterval(interval);
        const elapsed = Date.now() - startTime;
        reject(new Error(`Wait timeout after ${elapsed}ms`));
      }, actualTimeout);
    });
  }

  // Internal delay utility
  async _delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  // Close page
  async close() {
    if (this.page && !this.page.isClosed()) {
      await this.page.close();
    }
  }
}

export default PageController;
