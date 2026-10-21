import GTHBrowserAutomation from '../src/index.js';
import { config } from '../src/config.js';

/**
 * Custom Interaction Sequence Example
 *
 * Demonstrates how to automate a custom sequence of interactions
 * with your web application, including CAPTCHA testing.
 *
 * Usage:
 *   node scripts/custom-interaction-sequence.js
 */

async function runCustomSequence() {
  const automation = new GTHBrowserAutomation({
    browser: {
      ...config.browser,
      headless: false, // Set to false to see what's happening
    },
  });

  const logger = automation.getLogger();

  try {
    await automation.initialize();
    const session = automation.getSessionManager();

    console.log('🎯 Running custom interaction sequence...\n');

    // Define custom interaction steps
    const steps = [
      // Step 1: Navigate to test page
      {
        action: 'navigate',
        url: config.target.targetUrl,
      },

      // Step 2: Wait for page to load
      {
        action: 'waitForElement',
        selector: 'body',
        timeout: 10000,
      },

      // Step 3: Take screenshot of initial page
      {
        action: 'screenshot',
        name: 'initial-page',
      },

      // Step 4: Wait for CAPTCHA to appear
      {
        action: 'waitForCaptcha',
      },

      // Step 5: Take screenshot with CAPTCHA visible
      {
        action: 'screenshot',
        name: 'captcha-present',
      },

      // Step 6: Wait for CAPTCHA completion (user manual or automatic)
      {
        action: 'waitForCaptchaCompletion',
      },

      // Step 7: Take final screenshot
      {
        action: 'screenshot',
        name: 'after-captcha',
      },

      // Step 8: Optional - additional interaction after CAPTCHA
      // Example: Click a button that was behind CAPTCHA
      // {
      //   action: 'click',
      //   selector: '#submit-button',
      // },
      // {
      //   action: 'screenshot',
      //   name: 'after-submit',
      // },
    ];

    // Execute the sequence
    const results = await session.executeInteractionSequence(steps);

    // Display results
    console.log('\n' + '='.repeat(60));
    console.log('📊 Sequence Results:');
    console.log('='.repeat(60));

    results.forEach((result, index) => {
      const status = result.success ? '✓' : '✗';
      console.log(`${status} Step ${result.step}: ${result.action}`);
      if (!result.success) {
        console.log(`  Error: ${result.error}`);
      }
    });

    const allSuccess = results.every(r => r.success);
    console.log('='.repeat(60));
    console.log(`\nOverall: ${allSuccess ? '✓ SUCCESS' : '✗ FAILED'}`);

    return allSuccess;
  } catch (error) {
    console.error('\n❌ Sequence failed:', error.message);
    logger.errorEvent('SEQUENCE_TEST', 'Custom sequence failed', error);
    return false;
  } finally {
    console.log(`\n📋 Log file: ${automation.getLogFile()}`);
    console.log(`Session ID: ${automation.getSessionId()}`);
    await automation.cleanup();
  }
}

// Run sequence
runCustomSequence().then(success => {
  process.exit(success ? 0 : 1);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
