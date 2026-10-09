"""Compile the real WiFi driver against narrow SDK stubs and execute host tests."""
import pathlib
import sys
import argparse
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = HERE / "build"
sys.path.insert(0, str(HERE.parent))
from rv32_host import compile_elf
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--red", action="store_true", help="run the original missing-feature fixture")
ARGS, UNITTEST_ARGS = PARSER.parse_known_args()
SOURCE = HERE / "red_driver.c" if ARGS.red else ROOT / "second sol/main/driver/wifi/wifi.c"
RUNNER = compile_elf([SOURCE, HERE / "wifi_test.c"],
                     [HERE / "stubs", ROOT / "second sol/main"], BUILD / "wifi_tests.elf")


class WiFiTests(unittest.TestCase):
    pass


for name in ["test_invalid_configuration_has_no_side_effects",
             "test_nvs_failure_never_erases_user_data", "test_existing_global_loop_is_preserved",
             "test_ram_credentials_and_lifecycle", "test_each_sdk_failure_rolls_back_owned_resources",
             "test_existing_ap_netif_rejected", "test_existing_wifi_driver_is_not_taken_over", "test_open_ap_and_full_length_ssid",
             "test_stop_failure_keeps_running_state_and_can_retry", "test_deinit_failure_keeps_owned_resources_for_retry"]:
    def test(self, function=name):
        rc = RUNNER.call(function)
        self.assertEqual(rc, 0, f"{function}: assertion failed at wifi_test.c:{rc}")
    setattr(WiFiTests, name, test)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + UNITTEST_ARGS, verbosity=2)
