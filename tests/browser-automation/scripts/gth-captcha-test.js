#!/usr/bin/env node

/**
 * GTH CAPTCHA Test
 *
 * This script tests GTH's CAPTCHA integration.
 * Run: node scripts/gth-captcha-test.js
 *
 * Configure via .env:
 *   TARGET_URL=http://localhost:8080
 *   HEADLESS=false
 */

import GTHBrowserAutomation from '../src/index.js';
import { config } from '../src/config.js';

async function testGTHCaptcha() {
  console.log('🎮 GTH CAPTCHA Integration Test');
  console.log('='.repeat(60));
  console.log(`Configuration:`);
  console.log(`  Target: ${config.target.targetUrl}`);
  console.log(`  Headless: ${config.browser.headless}`);
  console.log(`  Test Mode: ${config.captcha.testModeEnabled}`);
  console.log('='.repeat(60) + '\n');

  const automation = new GTHBrowserAutomation({
    browser: {
      ...config.browser,
      headless: false, // Show browser for GTH testing
    },
  });

  const logger = automation.getLogger();

  try {
    logger.info('GTH_TEST', 'Starting GTH CAPTCHA test');

    // Run the CAPTCHA workflow
    const result = await automation.runCaptchaTest();

    console.log('\n' + '='.repeat(60));
    console.log('📊 GTH Test Result:');
    console.log('='.repeat(60));
    console.log(`Success: ${result.success}`);
    console.log(`CAPTCHA Required: ${result.captchaRequired}`);
    console.log(`Status: ${result.state}`);
    console.log(`Message: ${result.message}`);
    if (result.elapsedTime) {
      console.log(`Time: ${result.elapsedTime}ms`);
    }
    console.log('='.repeat(60));

    console.log(`\n✓ Test log: ${automation.getLogFile()}`);
    console.log(`✓ Session ID: ${automation.getSessionId()}`);

    return result.success;
  } catch (error) {
    console.error('\n❌ GTH test error:', error.message);
    logger.errorEvent('GTH_TEST', 'Test execution failed', error);
    console.log(`\n✓ Test log: ${automation.getLogFile()}`);
    return false;
  }
}

// Run test
testGTHCaptcha().then(success => {
  process.exit(success ? 0 : 1);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
