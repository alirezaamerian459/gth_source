import GTHBrowserAutomation, { CaptchaState } from '../src/index.js';
import { config } from '../src/config.js';

/**
 * Manual Interaction Test
 *
 * This script tests the browser automation framework with manual CAPTCHA completion.
 * The browser window will be visible and you can interact with the CAPTCHA manually.
 *
 * Usage:
 *   node scripts/manual-interaction-test.js
 *
 * Configuration:
 *   - Set TARGET_URL in .env to point to your test page
 *   - Set HEADLESS=false in .env to see the browser window
 *   - Set CAPTCHA_COMPLETION_TIMEOUT for how long to wait for manual completion
 */

async function runManualTest() {
  const automation = new GTHBrowserAutomation({
    browser: {
      ...config.browser,
      headless: false, // Show browser window
    },
    debug: {
      ...config.debug,
      verbose: true,
      keepBrowserOpen: true, // Keep browser open after test
    },
  });

  try {
    console.log('🚀 Starting manual CAPTCHA interaction test...\n');
    console.log(`📍 Target URL: ${config.target.targetUrl}`);
    console.log(`⏱️  CAPTCHA timeout: ${config.captcha.completionTimeout}ms\n`);

    // Initialize automation
    await automation.initialize();
    const session = automation.getSessionManager();
    const logger = automation.getLogger();

    logger.info('TEST', 'Manual interaction test started');

    // Navigate to test page
    console.log('📄 Navigating to test page...');
    await session.navigateToTest();
    console.log('✓ Page loaded\n');

    // Take initial screenshot
    const screenshot1 = await session.screenshot('01-initial-page');
    logger.info('TEST', `Screenshot saved: ${screenshot1}`);

    // Wait for and detect CAPTCHA
    console.log('🔍 Waiting for CAPTCHA...');
    const captchaMonitor = automation.getCaptchaMonitor();
    const captchaDetected = await captchaMonitor.waitForCaptcha();

    if (!captchaDetected) {
      console.log('❌ No CAPTCHA detected on page');
      logger.info('TEST', 'No CAPTCHA detected - test complete');
      return {
        success: false,
        message: 'No CAPTCHA found on page',
      };
    }

    console.log('✓ CAPTCHA detected\n');
    console.log('='*60);
    console.log('👤 Please complete the CAPTCHA manually');
    console.log(`⏱️  Waiting up to ${config.captcha.completionTimeout/1000} seconds...`);
    console.log('='*60 + '\n');

    // Take screenshot with CAPTCHA visible
    const screenshot2 = await session.screenshot('02-captcha-visible');
    logger.info('TEST', `Screenshot saved: ${screenshot2}`);

    // Wait for user to complete CAPTCHA
    const startTime = Date.now();
    const completed = await captchaMonitor.waitForCompletion();
    const elapsedTime = Date.now() - startTime;

    if (completed) {
      console.log(`\n✓ CAPTCHA completed successfully in ${elapsedTime}ms\n`);

      // Take screenshot after completion
      const screenshot3 = await session.screenshot('03-captcha-completed');
      logger.info('TEST', `Screenshot saved: ${screenshot3}`);

      logger.info('TEST', 'Manual test successful');
      return {
        success: true,
        message: 'CAPTCHA completed successfully',
        elapsedTime: elapsedTime,
        state: captchaMonitor.getState(),
      };
    } else {
      const state = captchaMonitor.getState();
      console.log(`\n❌ CAPTCHA failed: ${state}\n`);
      logger.info('TEST', `Manual test failed: ${state}`);

      // Take screenshot of failure
      const screenshot4 = await session.screenshot('04-captcha-failed');
      logger.info('TEST', `Screenshot saved: ${screenshot4}`);

      return {
        success: false,
        message: `CAPTCHA ${state}`,
        elapsedTime: elapsedTime,
        state: state,
      };
    }
  } catch (error) {
    console.error('\n❌ Test failed with error:', error.message);
    automation.getLogger().errorEvent('TEST', 'Manual test error', error);
    return {
      success: false,
      message: error.message,
      error: error,
    };
  } finally {
    console.log('\n📋 Test Summary:');
    console.log(`Log file: ${automation.getLogFile()}`);
    console.log(`Session ID: ${automation.getSessionId()}`);

    // Browser kept open for debugging (use config.debug.keepBrowserOpen)
    if (!config.debug.keepBrowserOpen) {
      console.log('\nCleaning up...');
      await automation.cleanup();
      console.log('Done!');
    } else {
      console.log('\n⏸️  Browser kept open for debugging');
      console.log('Press ENTER to close browser and exit...');
      await new Promise(resolve => {
        process.stdin.once('data', resolve);
      });
      await automation.cleanup();
    }
  }
}

// Run test
runManualTest().then(result => {
  console.log('\n' + '='.repeat(60));
  if (result.success) {
    console.log('✓ TEST PASSED');
  } else {
    console.log('✗ TEST FAILED');
  }
  console.log('='.repeat(60));
  process.exit(result.success ? 0 : 1);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
