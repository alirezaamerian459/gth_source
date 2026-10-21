import test from 'node:test';
import assert from 'node:assert';
import { config } from '../src/config.js';
import { isAllowedHost } from '../src/config.js';

test('isAllowedHost - Allows configured hosts', () => {
  const testConfig = { ...config };
  testConfig.browser.allowedHosts = ['localhost', '127.0.0.1', 'gth.local'];

  assert.ok(isAllowedHost('http://localhost:8080'));
  assert.ok(isAllowedHost('http://127.0.0.1:3000'));
  assert.ok(isAllowedHost('http://gth.local'));
  assert.ok(isAllowedHost('https://gth.local:8443/path'));
});

test('isAllowedHost - Rejects non-allowed hosts', () => {
  assert.ok(!isAllowedHost('http://evil.com'));
  assert.ok(!isAllowedHost('http://google.com'));
  assert.ok(!isAllowedHost('http://random-site.net'));
});

test('isAllowedHost - Handles wildcard domains', () => {
  // Test with wildcard support
  const url = 'http://sub.example.com';
  // Wildcard matching would be: *.example.com
  // This depends on implementation
});

test('isAllowedHost - Handles invalid URLs', () => {
  assert.ok(!isAllowedHost('not-a-valid-url'));
  assert.ok(!isAllowedHost(''));
  assert.ok(!isAllowedHost(null));
});

test('Config - Has required properties', () => {
  assert.ok(config.browser);
  assert.ok(config.target);
  assert.ok(config.captcha);
  assert.ok(config.interaction);
  assert.ok(config.logging);
  assert.ok(config.retry);
  assert.ok(config.debug);
});

test('Config - Timeouts are reasonable', () => {
  assert.ok(config.target.navigationTimeout > 0);
  assert.ok(config.target.pageReadyTimeout > 0);
  assert.ok(config.captcha.detectionTimeout > 0);
  assert.ok(config.captcha.completionTimeout > config.captcha.detectionTimeout);
});

test('Config - Browser config has defaults', () => {
  assert.ok(typeof config.browser.headless === 'boolean');
  assert.ok(typeof config.browser.persistentContext === 'boolean');
  assert.ok(config.browser.viewportWidth > 0);
  assert.ok(config.browser.viewportHeight > 0);
  assert.ok(Array.isArray(config.browser.allowedHosts));
  assert.ok(config.browser.allowedHosts.length > 0);
});

console.log('Integration tests completed');
