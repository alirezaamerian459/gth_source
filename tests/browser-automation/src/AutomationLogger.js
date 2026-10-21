import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const LogLevel = {
  DEBUG: 'DEBUG',
  INFO: 'INFO',
  WARN: 'WARN',
  ERROR: 'ERROR',
};

export class AutomationLogger {
  constructor(config) {
    this.config = config;
    this.logLevel = config.logging.level;
    this.logDir = config.logging.logDir;
    this.sessionId = this._generateSessionId();
    this.logFile = path.join(this.logDir, `automation-${this.sessionId}.log`);

    // Ensure log directory exists
    this._ensureDirectory(this.logDir);
  }

  _generateSessionId() {
    return new Date().toISOString().replace(/[:.]/g, '-').split('T').join('_');
  }

  _ensureDirectory(dir) {
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
  }

  _getTimestamp() {
    return new Date().toISOString();
  }

  _formatMessage(level, category, message, data) {
    const timestamp = this._getTimestamp();
    let output = `[${timestamp}] [${level}] [${category}] ${message}`;
    if (data) {
      output += ` ${JSON.stringify(data, null, 2)}`;
    }
    return output;
  }

  _shouldLog(level) {
    const levels = [LogLevel.DEBUG, LogLevel.INFO, LogLevel.WARN, LogLevel.ERROR];
    const currentLevelIndex = levels.indexOf(this.logLevel);
    const messageLevelIndex = levels.indexOf(level);
    return messageLevelIndex >= currentLevelIndex;
  }

  _writeLog(message) {
    // Write to file
    fs.appendFileSync(this.logFile, message + '\n', 'utf8');

    // Also log to console
    console.log(message);
  }

  debug(category, message, data) {
    if (this._shouldLog(LogLevel.DEBUG)) {
      const formatted = this._formatMessage(LogLevel.DEBUG, category, message, data);
      this._writeLog(formatted);
    }
  }

  info(category, message, data) {
    if (this._shouldLog(LogLevel.INFO)) {
      const formatted = this._formatMessage(LogLevel.INFO, category, message, data);
      this._writeLog(formatted);
    }
  }

  warn(category, message, data) {
    if (this._shouldLog(LogLevel.WARN)) {
      const formatted = this._formatMessage(LogLevel.WARN, category, message, data);
      this._writeLog(formatted);
    }
  }

  error(category, message, error) {
    if (this._shouldLog(LogLevel.ERROR)) {
      let data = null;
      if (error) {
        data = {
          message: error.message,
          stack: error.stack,
        };
      }
      const formatted = this._formatMessage(LogLevel.ERROR, category, message, data);
      this._writeLog(formatted);
    }
  }

  navigationEvent(url, duration, status) {
    this.info('NAVIGATION', `Navigated to ${url}`, {
      duration_ms: duration,
      status: status,
    });
  }

  captchaEvent(state, details) {
    this.info('CAPTCHA', `CAPTCHA state changed: ${state}`, details);
  }

  interactionEvent(action, target, duration) {
    this.debug('INTERACTION', `${action} on ${target}`, {
      duration_ms: duration,
    });
  }

  timeoutEvent(reason, timeout_ms) {
    this.warn('TIMEOUT', `Timeout occurred: ${reason}`, {
      timeout_ms: timeout_ms,
    });
  }

  errorEvent(component, reason, error) {
    this.error('ERROR', `${component}: ${reason}`, error);
  }

  saveScreenshot(data, name) {
    const screenshotDir = this.config.logging.screenshotDir;
    this._ensureDirectory(screenshotDir);

    const filename = `${this.sessionId}_${name}.png`;
    const filepath = path.join(screenshotDir, filename);
    fs.writeFileSync(filepath, data);

    this.info('SCREENSHOT', `Saved screenshot: ${filename}`);
    return filepath;
  }

  logSession() {
    this.info('SESSION', 'Automation session started', {
      session_id: this.sessionId,
      config: {
        target_url: this.config.target.targetUrl,
        headless: this.config.browser.headless,
        persistent_context: this.config.browser.persistentContext,
      },
    });
  }

  logSessionEnd() {
    this.info('SESSION', 'Automation session ended', {
      session_id: this.sessionId,
      log_file: this.logFile,
    });
  }

  getLogFile() {
    return this.logFile;
  }

  getSessionId() {
    return this.sessionId;
  }
}

export default AutomationLogger;
