/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// ------------------------------------------------------------------------------
// Requirements
// ------------------------------------------------------------------------------

import rule from "../lib/rules/avoid-removeChild.mjs";
import { RuleTester } from "eslint";

const ruleTester = new RuleTester();

// ------------------------------------------------------------------------------
// Tests
// ------------------------------------------------------------------------------

function invalidCode(code, messageId = "useRemove") {
  return { code, errors: [{ messageId, type: "CallExpression" }] };
}

ruleTester.run("avoid-removeChild", rule, {
  valid: [
    "elt.remove();",
    "elt.parentNode.parentNode.removeChild(elt2.parentNode);",
    "elt.parentNode.removeChild(elt2);",
    "elt.removeChild(elt2);",
  ],
  invalid: [
    invalidCode("elt.parentNode.removeChild(elt);"),
    invalidCode("elt.parentNode.parentNode.removeChild(elt.parentNode);"),
    invalidCode("$(e).parentNode.removeChild($(e));"),
    invalidCode("$('e').parentNode.removeChild($('e'));"),
    invalidCode("elt.removeChild(elt.firstChild);", "useFirstChildRemove"),
  ],
});
