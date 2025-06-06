#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import datetime
import json
import os
import subprocess
import sys

# load modules from parent dir
sys.path.insert(1, os.path.dirname(sys.path[0]))

from mozharness.base.log import WARNING
from mozharness.base.script import BaseScript, PreScriptAction
from mozharness.mozilla.automation import TBPL_RETRY
from mozharness.mozilla.mozbase import MozbaseMixin
from mozharness.mozilla.testing.android import AndroidMixin
from mozharness.mozilla.testing.codecoverage import CodeCoverageMixin
from mozharness.mozilla.testing.testbase import TestingMixin, testing_config_options

SUITE_DEFAULT_E10S = ["geckoview-junit", "mochitest", "reftest"]
SUITE_NO_E10S = ["cppunittest", "gtest", "jittest"]
SUITE_REPEATABLE = ["mochitest", "reftest", "xpcshell"]


class AndroidHardwareTest(
    TestingMixin, BaseScript, MozbaseMixin, CodeCoverageMixin, AndroidMixin
):
    config_options = [
        [["--test-suite"], {"action": "store", "dest": "test_suite", "default": None}],
        [
            ["--adb-path"],
            {
                "action": "store",
                "dest": "adb_path",
                "default": None,
                "help": "Path to adb",
            },
        ],
        [
            ["--total-chunk"],
            {
                "action": "store",
                "dest": "total_chunks",
                "default": None,
                "help": "Number of total chunks",
            },
        ],
        [
            ["--this-chunk"],
            {
                "action": "store",
                "dest": "this_chunk",
                "default": None,
                "help": "Number of this chunk",
            },
        ],
        [
            ["--log-raw-level"],
            {
                "action": "store",
                "dest": "log_raw_level",
                "default": "info",
                "help": "Set log level (debug|info|warning|error|critical|fatal)",
            },
        ],
        [
            ["--log-tbpl-level"],
            {
                "action": "store",
                "dest": "log_tbpl_level",
                "default": "info",
                "help": "Set log level (debug|info|warning|error|critical|fatal)",
            },
        ],
        [
            ["--disable-e10s"],
            {
                "action": "store_false",
                "dest": "e10s",
                "default": True,
                "help": "Run tests without multiple processes (e10s).",
            },
        ],
        [
            ["--disable-fission"],
            {
                "action": "store_true",
                "dest": "disable_fission",
                "default": False,
                "help": "Run with Fission disabled.",
            },
        ],
        [
            ["--repeat"],
            {
                "action": "store",
                "type": "int",
                "dest": "repeat",
                "default": 0,
                "help": "Repeat the tests the given number of times. Supported "
                "by mochitest, reftest, crashtest, ignored otherwise.",
            },
        ],
        [
            [
                "--setpref",
            ],
            {
                "action": "append",
                "dest": "extra_prefs",
                "default": [],
                "help": "Extra user prefs.",
            },
        ],
        [
            ["--jittest-flags"],
            {
                "action": "store",
                "dest": "jittest_flags",
                "default": "debug",
                "help": "Flags to run with jittest (all, debug, etc.).",
            },
        ],
        [
            ["--tag"],
            {
                "action": "append",
                "default": [],
                "dest": "test_tags",
                "help": "Filter out tests that don't have the given tag. Can be used multiple "
                "times in which case the test must contain at least one of the given tags.",
            },
        ],
    ] + copy.deepcopy(testing_config_options)

    def __init__(self, require_config_file=False):
        super(AndroidHardwareTest, self).__init__(
            config_options=self.config_options,
            all_actions=[
                "clobber",
                "download-and-extract",
                "create-virtualenv",
                "verify-device",
                "install",
                "run-tests",
            ],
            require_config_file=require_config_file,
            config={
                "virtualenv_modules": [],
                "virtualenv_requirements": [],
                "require_test_zip": True,
                # IP address of the host as seen from the device.
                "remote_webserver": os.environ["HOST_IP"],
            },
        )

        # these are necessary since self.config is read only
        c = self.config
        self.installer_url = c.get("installer_url")
        self.installer_path = c.get("installer_path")
        self.test_url = c.get("test_url")
        self.test_packages_url = c.get("test_packages_url")
        self.test_manifest = c.get("test_manifest")
        suite = c.get("test_suite")
        self.test_suite = suite
        self.this_chunk = c.get("this_chunk")
        self.total_chunks = c.get("total_chunks")
        self.xre_path = None
        self.log_raw_level = c.get("log_raw_level")
        self.log_tbpl_level = c.get("log_tbpl_level")
        self.disable_e10s = c.get("disable_e10s")
        self.disable_fission = c.get("disable_fission")
        self.extra_prefs = c.get("extra_prefs")
        self.jittest_flags = c.get("jittest_flags")
        self.test_tags = c.get("test_tags")

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super(AndroidHardwareTest, self).query_abs_dirs()
        dirs = {}
        dirs["abs_test_install_dir"] = os.path.join(abs_dirs["abs_work_dir"], "tests")
        dirs["abs_test_bin_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "tests", "bin"
        )
        dirs["abs_modules_dir"] = os.path.join(dirs["abs_test_install_dir"], "modules")
        dirs["abs_blob_upload_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "blobber_upload_dir"
        )
        dirs["abs_mochitest_dir"] = os.path.join(
            dirs["abs_test_install_dir"], "mochitest"
        )
        dirs["abs_reftest_dir"] = os.path.join(dirs["abs_test_install_dir"], "reftest")
        dirs["abs_xpcshell_dir"] = os.path.join(
            dirs["abs_test_install_dir"], "xpcshell"
        )
        work_dir = os.environ.get("MOZ_FETCHES_DIR") or abs_dirs["abs_work_dir"]
        dirs["abs_xre_dir"] = os.path.join(work_dir, "hostutils")

        for key in dirs.keys():
            if key not in abs_dirs:
                abs_dirs[key] = dirs[key]
        self.abs_dirs = abs_dirs
        return self.abs_dirs

    def _query_tests_dir(self):
        dirs = self.query_abs_dirs()
        try:
            test_dir = self.config["suite_definitions"][self.test_suite]["testsdir"]
        except Exception:
            test_dir = self.test_suite
        return os.path.join(dirs["abs_test_install_dir"], test_dir)

    def _build_command(self):
        c = self.config
        dirs = self.query_abs_dirs()

        if self.test_suite not in self.config["suite_definitions"]:
            self.fatal("Key '%s' not defined in the config!" % self.test_suite)

        cmd = [
            self.query_python_path("python"),
            "-u",
            os.path.join(
                self._query_tests_dir(),
                self.config["suite_definitions"][self.test_suite]["run_filename"],
            ),
        ]

        raw_log_file, error_summary_file = self.get_indexed_logs(
            dirs["abs_blob_upload_dir"], self.test_suite
        )

        # LambdaTest provides a list of recommended ports via env var:
        #     UserPorts=port/27045,port/27046,port/27047,port/27048,port/27049
        # These are only for android, so no need to put in mozprofile
        # NOTE: mozprofile.DEFAULT_PORTS has http:8888.
        DEFAULT_PORTS = {"http": 8854, "https": 4454, "ws": 9988, "wss": 4454}
        ports = [p.split("/")[-1] for p in os.environ.get("UserPorts", "").split(",")]
        if len(ports) > 3:
            DEFAULT_PORTS = {
                "http": ports[0],
                "https": ports[1],
                "ws": ports[2],
                "wss": ports[3],
            }

        str_format_values = {
            "device_serial": self.device_serial,
            "remote_webserver": c["remote_webserver"],
            "xre_path": self.xre_path,
            "utility_path": self.xre_path,
            "http_port": DEFAULT_PORTS[
                "http"
            ],  # starting http port  to use for the mochitest server
            "ssl_port": DEFAULT_PORTS[
                "https"
            ],  # starting ssl port to use for the server
            "certs_path": os.path.join(dirs["abs_work_dir"], "tests/certs"),
            # TestingMixin._download_and_extract_symbols() will set
            # self.symbols_path when downloading/extracting.
            "symbols_path": self.symbols_path,
            "modules_dir": dirs["abs_modules_dir"],
            "installer_path": self.installer_path,
            "raw_log_file": raw_log_file,
            "log_tbpl_level": self.log_tbpl_level,
            "log_raw_level": self.log_raw_level,
            "error_summary_file": error_summary_file,
            "xpcshell_extra": c.get("xpcshell_extra", ""),
            "jittest_flags": self.jittest_flags,
            "test_tags": self.test_tags,
        }

        user_paths = json.loads(os.environ.get("MOZHARNESS_TEST_PATHS", '""'))
        confirm_paths = json.loads(os.environ.get("MOZHARNESS_CONFIRM_PATHS", '""'))

        for option in self.config["suite_definitions"][self.test_suite]["options"]:
            opt = option.split("=")[0]
            # override configured chunk options with script args, if specified
            if opt in ("--this-chunk", "--total-chunks"):
                if (
                    user_paths
                    or getattr(self, opt.replace("-", "_").strip("_"), None) is not None
                ):
                    continue

            if "%(app)" in option:
                # only query package name if requested
                cmd.extend([option % {"app": self.query_package_name()}])
            else:
                option = option % str_format_values
                if option:
                    cmd.extend([option])

        if not self.verify_enabled and not user_paths:
            if self.this_chunk is not None:
                cmd.extend(["--this-chunk", self.this_chunk])
            if self.total_chunks is not None:
                cmd.extend(["--total-chunks", self.total_chunks])

        if "mochitest" in self.test_suite:
            category = "mochitest"
        elif "reftest" in self.test_suite or "crashtest" in self.test_suite:
            category = "reftest"
        else:
            category = self.test_suite
        if c.get("repeat"):
            if category in SUITE_REPEATABLE:
                cmd.extend(["--repeat=%s" % c.get("repeat")])
            else:
                self.log(f"--repeat not supported in {category}", level=WARNING)

        if category not in SUITE_NO_E10S:
            if category in SUITE_DEFAULT_E10S and not c["e10s"]:
                cmd.append("--disable-e10s")
            elif category not in SUITE_DEFAULT_E10S and c["e10s"]:
                cmd.append("--e10s")

        if self.disable_fission and category not in SUITE_NO_E10S:
            cmd.append("--disable-fission")

        cmd.extend([f"--setpref={p}" for p in self.extra_prefs])

        cmd.extend([f"--tag={t}" for t in self.test_tags])

        try_options, try_tests = self.try_args(self.test_suite)
        if try_options:
            cmd.extend(try_options)

        if user_paths:
            # reftest on android-hw uses a subset (reftest-qr) of tests,
            # but scheduling only knows about 'reftest'
            suite = self.test_suite
            if suite == "reftest-qr":
                suite = "reftest"

            if user_paths.get(suite, []):
                suite_test_paths = user_paths.get(suite, [])
                # NOTE: we do not want to prepend 'tests' if a single path
                if confirm_paths and confirm_paths.get(suite, []):
                    suite_test_paths = confirm_paths.get(suite, [])
                suite_test_paths = [os.path.join("tests", p) for p in suite_test_paths]
                cmd.extend(suite_test_paths)

        elif not self.verify_enabled and not self.per_test_coverage:
            cmd.extend(
                self.query_tests_args(
                    self.config["suite_definitions"][self.test_suite].get("tests"),
                    None,
                    try_tests,
                )
            )

        if self.config.get("restartAfterFailure", False):
            cmd.append("--restartAfterFailure")

        return cmd

    def _query_suites(self):
        if self.test_suite:
            return [(self.test_suite, self.test_suite)]
        # per-test mode: determine test suites to run
        all = [
            (
                "mochitest",
                {
                    "mochitest-plain": "mochitest-plain",
                    "mochitest-plain-gpu": "mochitest-plain-gpu",
                },
            ),
            ("reftest", {"reftest": "reftest", "crashtest": "crashtest"}),
            ("xpcshell", {"xpcshell": "xpcshell"}),
        ]
        suites = []
        for category, all_suites in all:
            cat_suites = self.query_per_test_category_suites(category, all_suites)
            for k in cat_suites.keys():
                suites.append((k, cat_suites[k]))
        return suites

    def _query_suite_categories(self):
        if self.test_suite:
            categories = [self.test_suite]
        else:
            # per-test mode
            categories = ["mochitest", "reftest", "xpcshell"]
        return categories

    ##########################################
    # Actions for AndroidHardwareTest        #
    ##########################################

    def preflight_install(self):
        # in the base class, this checks for mozinstall, but we don't use it
        pass

    @PreScriptAction("create-virtualenv")
    def pre_create_virtualenv(self, action):
        dirs = self.query_abs_dirs()
        requirements = None
        suites = self._query_suites()
        if ("mochitest-media", "mochitest-media") in suites:
            # mochitest-media is the only thing that needs this
            requirements = os.path.join(
                dirs["abs_mochitest_dir"],
                "websocketprocessbridge",
                "websocketprocessbridge_requirements_3.txt",
            )
        if requirements:
            self.register_virtualenv_module(requirements=[requirements])

    def download_and_extract(self):
        """
        Download and extract product APK, tests.zip, and host utils.
        """
        super(AndroidHardwareTest, self).download_and_extract(
            suite_categories=self._query_suite_categories()
        )
        dirs = self.query_abs_dirs()
        self.xre_path = dirs["abs_xre_dir"]

    def install(self):
        """
        Install APKs on the device.
        """
        install_needed = (not self.test_suite) or self.config["suite_definitions"][
            self.test_suite
        ].get("install")
        if install_needed is False:
            self.info("Skipping apk installation for %s" % self.test_suite)
            return
        assert (
            self.installer_path is not None
        ), "Either add installer_path to the config or use --installer-path."
        self.uninstall_android_app()
        self.install_android_app(self.installer_path)
        self.info("Finished installing apps for %s" % self.device_name)

    def run_tests(self):
        """
        Run the tests
        """
        self.start_time = datetime.datetime.now()
        max_per_test_time = datetime.timedelta(minutes=60)

        per_test_args = []
        suites = self._query_suites()
        minidump = self.query_minidump_stackwalk()
        for per_test_suite, suite in suites:
            self.test_suite = suite

            try:
                cwd = self._query_tests_dir()
            except Exception:
                self.fatal("Don't know how to run --test-suite '%s'!" % self.test_suite)
            env = self.query_env()
            if minidump:
                env["MINIDUMP_STACKWALK"] = minidump
            env["MOZ_UPLOAD_DIR"] = self.query_abs_dirs()["abs_blob_upload_dir"]
            env["MINIDUMP_SAVE_PATH"] = self.query_abs_dirs()["abs_blob_upload_dir"]
            env["RUST_BACKTRACE"] = "full"

            summary = None
            for per_test_args in self.query_args(per_test_suite):
                if (datetime.datetime.now() - self.start_time) > max_per_test_time:
                    # Running tests has run out of time. That is okay! Stop running
                    # them so that a task timeout is not triggered, and so that
                    # (partial) results are made available in a timely manner.
                    self.info(
                        "TinderboxPrint: Running tests took too long: "
                        "Not all tests were executed.<br/>"
                    )
                    # Signal per-test time exceeded, to break out of suites and
                    # suite categories loops also.
                    return

                cmd = self._build_command()
                final_cmd = copy.copy(cmd)
                if len(per_test_args) > 0:
                    # in per-test mode, remove any chunk arguments from command
                    for arg in final_cmd:
                        if "total-chunk" in arg or "this-chunk" in arg:
                            final_cmd.remove(arg)
                final_cmd.extend(per_test_args)

                self.info(
                    "Running on %s the command %s"
                    % (self.device_name, subprocess.list2cmdline(final_cmd))
                )
                self.info("##### %s log begins" % self.test_suite)

                suite_category = self.test_suite
                parser = self.get_test_output_parser(
                    suite_category,
                    config=self.config,
                    log_obj=self.log_obj,
                    error_list=[],
                )
                self.run_command(final_cmd, cwd=cwd, env=env, output_parser=parser)
                tbpl_status, log_level, summary = parser.evaluate_parser(0, summary)
                parser.append_tinderboxprint_line(self.test_suite)

                self.info("##### %s log ends" % self.test_suite)

                if len(per_test_args) > 0:
                    self.record_status(tbpl_status, level=log_level)
                    self.log_per_test_status(per_test_args[-1], tbpl_status, log_level)
                    if tbpl_status == TBPL_RETRY:
                        self.info("Per-test run abandoned due to RETRY status")
                        return
                else:
                    self.record_status(tbpl_status, level=log_level)
                    # report as INFO instead of log_level to avoid extra Treeherder lines
                    self.info(
                        "The %s suite: %s ran with return status: %s"
                        % (suite_category, suite, tbpl_status),
                    )


if __name__ == "__main__":
    test = AndroidHardwareTest()
    test.run_and_exit()
