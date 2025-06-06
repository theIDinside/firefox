/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// Tests the Javascript Tracing feature.

"use strict";

add_task(async function () {
  // This is preffed off for now, so ensure turning it on
  await pushPref("devtools.debugger.features.javascript-tracing", true);

  const dbg = await initDebugger("doc-scripts.html");

  // This test covers the Web Console, whereas it is no longer the default output
  await toggleJsTracerMenuItem(dbg, "#jstracer-menu-item-console");

  // Add an iframe before starting the tracer to later check for key event on it
  const preExistingIframeBrowsingContext = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async function () {
      const iframe = content.document.createElement("iframe");
      iframe.src = `data:text/html,<input type="text" value="pre existing iframe" onkeydown="console.log('keydown')" />`;
      content.document.body.appendChild(iframe);
      await new Promise(resolve => (iframe.onload = resolve));
      return iframe.contentWindow.browsingContext;
    }
  );

  info("Enable the tracing");
  await toggleJsTracer(dbg.toolbox);

  ok(
    dbg.toolbox.splitConsole,
    "Split console is automatically opened when tracing to the console"
  );

  await hasConsoleMessage(dbg, "Started tracing to Web Console");

  invokeInTab("main");

  info("Wait for console messages for the whole trace");
  // `main` calls `foo` which calls `bar`
  await hasConsoleMessage(dbg, "λ main");
  await hasConsoleMessage(dbg, "λ foo");
  await hasConsoleMessage(dbg, "λ bar");

  const linkEl = await waitForConsoleMessageLink(
    dbg.toolbox,
    "λ main",
    "simple1.js:1:17"
  );
  linkEl.click();
  info("Wait for the main function to be highlighted in the debugger");
  await waitForSelectedSource(dbg, "simple1.js");
  await waitForSelectedLocation(dbg, 1, 17);

  // Trigger a click to verify we do trace DOM events
  BrowserTestUtils.synthesizeMouseAtCenter(
    "button",
    {},
    gBrowser.selectedBrowser
  );

  await hasConsoleMessage(dbg, "DOM | node.click");
  await hasConsoleMessage(dbg, "λ simple");

  const iframeBrowsingContext = await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    async function () {
      const iframe = content.document.createElement("iframe");
      iframe.src = `data:text/html,<input type="text" value="new iframe" onkeypress="console.log('keypress')" />`;
      content.document.body.appendChild(iframe);
      await new Promise(resolve => (iframe.onload = resolve));
      iframe.contentWindow.document.querySelector("input").focus();
      return iframe.contentWindow.browsingContext;
    }
  );

  await BrowserTestUtils.synthesizeKey("x", {}, iframeBrowsingContext);
  await hasConsoleMessage(dbg, "DOM | node.keypress");
  await hasConsoleMessage(dbg, "λ onkeypress");

  await SpecialPowers.spawn(
    preExistingIframeBrowsingContext,
    [],
    async function () {
      content.document.querySelector("input").focus();
    }
  );
  await BrowserTestUtils.synthesizeKey(
    "x",
    {},
    preExistingIframeBrowsingContext
  );
  await hasConsoleMessage(dbg, "DOM | node.keydown");
  await hasConsoleMessage(dbg, "λ onkeydown");

  // Test Blackboxing
  info("Clear the console from previous traces");
  const { hud } = await dbg.toolbox.getPanel("webconsole");
  hud.ui.clearOutput();
  await waitFor(
    async () => !(await findConsoleMessages(dbg.toolbox, "λ main")).length,
    "Wait for console to be cleared"
  );

  info(
    "Now blackbox only the source where main function is (simple1.js), but foo and bar are in another module"
  );
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "BLACKBOX_WHOLE_SOURCES");

  info("Trigger some code from simple1 and simple2");
  invokeInTab("main");

  info("Only methods from simple2 are logged");
  await hasConsoleMessage(dbg, "λ foo");
  await hasConsoleMessage(dbg, "λ bar");
  is(
    (await findConsoleMessages(dbg.toolbox, "λ main")).length,
    0,
    "Traces from simple1.js, related to main function are not logged"
  );

  info("Revert blackboxing");
  await clickElement(dbg, "blackbox");
  await waitForDispatch(dbg.store, "UNBLACKBOX_WHOLE_SOURCES");

  // Test Disabling tracing
  info("Disable the tracing");
  await toggleJsTracer(dbg.toolbox);

  invokeInTab("inline_script2");

  // Let some time for the tracer to appear if we failed disabling the tracing
  await wait(1000);

  const messages = await findConsoleMessages(dbg.toolbox, "inline_script2");
  is(
    messages.length,
    0,
    "We stopped recording traces, an the function call isn't logged in the console"
  );

  // Test Navigations
  await navigate(dbg, "doc-sourcemaps2.html", "main.js", "main.min.js");

  info("Re-enable the tracing after navigation");
  await toggleJsTracer(dbg.toolbox);

  invokeInTab("logMessage");

  // Test clicking on the function to open the precise related location
  const linkEl2 = await waitForConsoleMessageLink(
    dbg.toolbox,
    "λ logMessage",
    "main.js:4:3"
  );
  linkEl2.click();

  info("Wait for the 'logMessage' function to be highlighted in the debugger");
  await waitForSelectedSource(dbg, "main.js");
  await waitForSelectedLocation(dbg, 4, 3);
  ok(true, "The selected source and location is on the original file");

  await dbg.toolbox.closeToolbox();
});

add_task(async function testPersitentLogMethod() {
  let dbg = await initDebugger("doc-scripts.html");

  is(
    dbg.commands.tracerCommand.getTracingOptions().logMethod,
    "console",
    "By default traces are logged to the console"
  );

  info("Change the log method to stdout");
  await toggleJsTracerMenuItem(dbg, "#jstracer-menu-item-stdout");

  await dbg.toolbox.closeToolbox();

  dbg = await initDebugger("doc-scripts.html");
  is(
    dbg.commands.tracerCommand.getTracingOptions().logMethod,
    "stdout",
    "The new setting has been persisted"
  );

  info("Reset back to the default value");
  await toggleJsTracerMenuItem(dbg, "#jstracer-menu-item-console");
});

add_task(async function testPageKeyShortcut() {
  // Ensures that the key shortcut emitted in the content process bubbles up to the parent process
  await pushPref("test.events.async.enabled", true);

  // Fake DevTools being opened by a real user interaction.
  // Tests are bypassing DevToolsStartup to open the tools by calling gDevTools directly.
  // By doing so DevToolsStartup considers itself as uninitialized,
  // whereas we want it to handle the key shortcut we trigger in this test.
  const DevToolsStartup = Cc["@mozilla.org/devtools/startup-clh;1"].getService(
    Ci.nsISupports
  ).wrappedJSObject;
  DevToolsStartup.initialized = true;
  registerCleanupFunction(() => {
    DevToolsStartup.initialized = false;
  });

  const dbg = await initDebuggerWithAbsoluteURL("data:text/html,key-shortcut");

  const button = dbg.toolbox.doc.getElementById("command-button-jstracer");
  ok(!button.classList.contains("checked"), "The trace button is off on start");

  info(
    "Focus the page in order to assert that the page keeps the focus when enabling the tracer"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    content.focus();
  });
  await waitFor(
    () => Services.focus.focusedElement == gBrowser.selectedBrowser
  );
  is(
    Services.focus.focusedElement,
    gBrowser.selectedBrowser,
    "The tab is still focused before enabling tracing"
  );

  info("Toggle ON the tracing via the key shortcut from the web page");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    EventUtils.synthesizeKey(
      "VK_5",
      { ctrlKey: true, shiftKey: true },
      content
    );
  });

  info("Wait for tracing to be enabled");
  await waitFor(() => button.classList.contains("checked"));

  is(
    Services.focus.focusedElement,
    gBrowser.selectedBrowser,
    "The tab is still focused after enabling tracing"
  );

  info("Toggle it back off, with the same shortcut");
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    EventUtils.synthesizeKey(
      "VK_5",
      { ctrlKey: true, shiftKey: true },
      content
    );
  });

  info("Wait for tracing to be disabled");
  await waitFor(() => !button.classList.contains("checked"));
});

add_task(async function testPageKeyShortcutWithoutDebugger() {
  // Ensures that the key shortcut emitted in the content process bubbles up to the parent process
  await pushPref("test.events.async.enabled", true);

  // Fake DevTools being opened by a real user interaction.
  // Tests are bypassing DevToolsStartup to open the tools by calling gDevTools directly.
  // By doing so DevToolsStartup considers itself as uninitialized,
  // whereas we want it to handle the key shortcut we trigger in this test.
  const DevToolsStartup = Cc["@mozilla.org/devtools/startup-clh;1"].getService(
    Ci.nsISupports
  ).wrappedJSObject;
  DevToolsStartup.initialized = true;
  registerCleanupFunction(() => {
    DevToolsStartup.initialized = false;
  });

  const toolbox = await openNewTabAndToolbox(
    "data:text/html,tracer",
    "webconsole"
  );

  info(
    "Focus the page in order to assert that the page keeps the focus when enabling the tracer"
  );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    content.focus();
  });
  await waitFor(
    () => Services.focus.focusedElement == gBrowser.selectedBrowser
  );
  is(
    Services.focus.focusedElement,
    gBrowser.selectedBrowser,
    "The tab is still focused before enabling tracing"
  );

  info("Toggle ON the tracing via the key shortcut from the web page");
  const { resourceCommand } = toolbox.commands;
  const { onResource: onTracingStateEnabled } =
    await resourceCommand.waitForNextResource(
      resourceCommand.TYPES.JSTRACER_STATE,
      {
        ignoreExistingResources: true,
        predicate(resource) {
          return resource.enabled;
        },
      }
    );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    EventUtils.synthesizeKey(
      "VK_5",
      { ctrlKey: true, shiftKey: true },
      content
    );
  });
  info("Wait for tracing to be enabled");
  await onTracingStateEnabled;

  info("Toggle it back off, with the same shortcut");
  const { onResource: onTracingStateDisabled } =
    await resourceCommand.waitForNextResource(
      resourceCommand.TYPES.JSTRACER_STATE,
      {
        ignoreExistingResources: true,
        predicate(resource) {
          return !resource.enabled;
        },
      }
    );
  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    EventUtils.synthesizeKey(
      "VK_5",
      { ctrlKey: true, shiftKey: true },
      content
    );
  });

  info("Wait for tracing to be disabled");
  await onTracingStateDisabled;
});
