import GTHBrowserAutomation from '../src/index.js';
import { config } from '../src/config.js';

/**
 * CAPTCHA Workflow Test
 *
 * Automated test of the complete CAPTCHA workflow including:
 * - Navigation to test page
 * - CAPTCHA detection
 * - Manual completion detection
 * - Workflow completion
 *
 * Usage:
 *   node scripts/captcha-workflow-test.js
 *
 * Configure via .env:
 *   TARGET_URL=http://your-test-page.local
 *   CAPTCHA_TEST_MODE=true (or false for production CAPTCHA)
 *   HEADLESS=false (to watch the test)
 */

async function testCaptchaWorkflow() {
  const automation = new GTHBrowserAutomation({
    browser: {
      ...config.browser,
      headless: false, // Show browser for manual CAPTCHA completion
    },
  });

  const logger = automation.getLogger();

  try {
    console.log('🧪 CAPTCHA Workflow Test');
    console.log('='.repeat(60));
    console.log(`Configuration:`);
    console.log(`  Target URL: ${config.target.targetUrl}`);
    console.log(`  Test Mode: ${config.captcha.testModeEnabled}`);
    console.log(`  Headless: ${config.browser.headless}`);
    console.log(`  Detection Timeout: ${config.captcha.detectionTimeout}ms`);
    console.log(`  Completion Timeout: ${config.captcha.completionTimeout}ms`);
    console.log('='.repeat(60) + '\n');

    // Run the CAPTCHA workflow
    const result = await automation.runCaptchaTest();

    console.log('\n' + '='.repeat(60));
    console.log('📊 Workflow Result:');
    console.log('='.repeat(60));
    console.log(`Success: ${result.success}`);
    console.log(`CAPTCHA Required: ${result.captchaRequired}`);
    console.log(`State: ${result.state}`);
    console.log(`Message: ${result.message}`);
    if (result.elapsedTime) {
      console.log(`Elapsed Time: ${result.elapsedTime}ms`);
    }
    console.log('='.repeat(60));

    console.log(`\n📋 Log file: ${automation.getLogFile()}`);
    console.log(`Session ID: ${automation.getSessionId()}`);

    return result.success;
  } catch (error) {
    console.error('\n❌ Workflow test failed:', error.message);
    logger.errorEvent('WORKFLOW_TEST', 'Test failed', error);
    console.log(`\n📋 Log file: ${automation.getLogFile()}`);
    return false;
  }
}

// Run test
testCaptchaWorkflow().then(success => {
  process.exit(success ? 0 : 1);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
