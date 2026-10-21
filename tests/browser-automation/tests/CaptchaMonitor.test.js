import test from 'node:test';
import assert from 'node:assert';
import { CaptchaMonitor, CaptchaState } from '../src/CaptchaMonitor.js';
import { AutomationLogger } from '../src/AutomationLogger.js';
import { config } from '../src/config.js';

// Mock page object
const createMockPage = (captchaPresent = false, completed = false) => {
  return {
    $: async (selector) => {
      if (captchaPresent && selector.includes('captcha')) {
        return completed ? null : { getAttribute: async () => null };
      }
      if (completed && selector === '[data-captcha-success]') {
        return {};
      }
      return null;
    },
  };
};

test('CaptchaMonitor - Initial state is NOT_PRESENT', () => {
  const logger = new AutomationLogger(config);
  const monitor = new CaptchaMonitor(createMockPage(), config, logger);

  assert.strictEqual(monitor.getState(), CaptchaState.NOT_PRESENT);
});

test('CaptchaMonitor - Can set state manually', () => {
  const logger = new AutomationLogger(config);
  const monitor = new CaptchaMonitor(createMockPage(), config, logger);

  monitor.setState(CaptchaState.DETECTED);
  assert.strictEqual(monitor.getState(), CaptchaState.DETECTED);

  monitor.setState(CaptchaState.COMPLETED);
  assert.strictEqual(monitor.getState(), CaptchaState.COMPLETED);
});

test('CaptchaMonitor - Reset clears state', () => {
  const logger = new AutomationLogger(config);
  const monitor = new CaptchaMonitor(createMockPage(), config, logger);

  monitor.setState(CaptchaState.COMPLETED);
  monitor.reset();

  assert.strictEqual(monitor.getState(), CaptchaState.NOT_PRESENT);
});

test('CaptchaMonitor - Can track time since detection', async () => {
  const logger = new AutomationLogger(config);
  const monitor = new CaptchaMonitor(createMockPage(), config, logger);

  monitor.setState(CaptchaState.DETECTED);
  monitor.detectedAt = Date.now() - 1000; // 1 second ago

  const time = monitor.getTimeSinceDetection();
  assert.ok(time >= 1000 && time < 1100, `Expected ~1000ms, got ${time}ms`);
});

test('CaptchaMonitor - State transitions', () => {
  const logger = new AutomationLogger(config);
  const monitor = new CaptchaMonitor(createMockPage(), config, logger);

  const states = [
    CaptchaState.DETECTED,
    CaptchaState.WAITING_FOR_USER,
    CaptchaState.COMPLETED,
  ];

  for (const state of states) {
    monitor.setState(state);
    assert.strictEqual(monitor.getState(), state);
  }
});

console.log('CaptchaMonitor tests completed');
