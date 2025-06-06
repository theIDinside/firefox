/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export default [
  {
    rules: {
      "block-scoped-var": "error",
      complexity: ["error", { max: 22 }],
      "max-nested-callbacks": ["error", 3],
      "no-extend-native": "error",
      "no-multi-str": "error",
      "no-return-assign": "error",
      "no-unused-vars": [
        "error",
        { argsIgnorePattern: "^_", caughtErrors: "none", vars: "all" },
      ],
      strict: ["error", "global"],
      yoda: "error",
    },
  },
  {
    files: ["**/head*.js"],
    rules: {
      "no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          caughtErrors: "none",
          vars: "local",
        },
      ],
    },
  },
];
