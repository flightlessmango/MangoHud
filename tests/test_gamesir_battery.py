import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch


SCRIPT = Path(__file__).resolve().parents[1] / "bin" / "gamesir-battery.py"
spec = importlib.util.spec_from_file_location("gamesir_battery", SCRIPT)
battery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(battery)


def report(percent=100, charging=False):
    data = [0] * 64
    data[0] = 0x12
    data[1:5] = [128] * 4
    data[35] = int(charging)
    data[36] = percent
    return data


class BatteryTests(unittest.TestCase):
    def test_percentage_and_charging(self):
        for percent in (0, 1, 5, 50, 99, 100):
            self.assertEqual(battery.parse_report(report(percent)), f"{percent}%")
        self.assertEqual(battery.parse_report(report(76, True)), "76% charging")

    def test_unrelated_short_empty_and_invalid_reports(self):
        for data in ([], [0x12], report()[:36], report() + [0],
                     [0x12] + [0] * 63, report(101), report(255),
                     [0x10] + report()[1:]):
            self.assertIsNone(battery.parse_report(data))

    def test_atomic_cache_and_stale_status(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "battery.txt"
            self.assertEqual(battery.status(output), "Disconnected")
            battery.publish(output, "75%")
            self.assertEqual(battery.status(output), "75%")
            battery.publish(output, "74% charging")
            self.assertEqual(battery.status(output), "74% charging")
            self.assertEqual(list(output.parent.iterdir()), [output])
            os.utime(output, (1, 1))
            self.assertEqual(battery.status(output), "Disconnected")

    def test_default_output_respects_xdg(self):
        with patch.dict(os.environ, {"XDG_CACHE_HOME": "/custom/cache"}):
            self.assertEqual(battery.default_output(),
                             Path("/custom/cache/MangoHud/gamesir-battery.txt"))

    def test_once_reads_without_writing_cache(self):
        device = Mock()
        device.write.return_value = 64
        device.read.side_effect = [[0x10] + [0] * 63, report(82)]
        with patch.object(battery, "publish") as publish, io.StringIO() as output:
            with contextlib.redirect_stdout(output):
                self.assertTrue(battery.read_device(device, Path("unused"),
                                                    threading.Event(), once=True))
            self.assertEqual(output.getvalue(), "82%\n")
            publish.assert_not_called()
        device.write.assert_called_once_with(battery.HEARTBEAT)
        device.set_nonblocking.assert_called_once_with(True)

    def test_heartbeat_is_rate_limited_and_unchanged_cache_is_refreshed(self):
        device = Mock()
        device.write.return_value = 64
        device.read.return_value = report(67)
        stop = threading.Event()
        ticks = iter([0, 0, .25, .5, .75, 1.0])

        def clock():
            try:
                return next(ticks)
            except StopIteration:
                stop.set()
                return 1.1

        with patch.object(battery.time, "monotonic", side_effect=clock), \
                patch.object(battery, "publish") as publish:
            battery.read_device(device, Path("unused"), stop)
        self.assertEqual(device.write.call_count, 3)
        self.assertEqual(publish.call_count, 2)

    def test_invalid_stream_times_out_instead_of_staying_connected(self):
        device = Mock()
        device.write.return_value = 64
        device.read.return_value = [0x12] + [0] * 63
        with patch.object(battery.time, "monotonic", side_effect=[0, 0, 5]), \
                patch.object(battery, "publish") as publish:
            with self.assertRaisesRegex(OSError, "No valid GameSir"):
                battery.read_device(device, Path("unused"), threading.Event())
            publish.assert_not_called()

    def test_failed_heartbeat_is_reported(self):
        device = Mock()
        device.write.return_value = -1
        with self.assertRaisesRegex(OSError, "heartbeat"):
            battery.read_device(device, Path("unused"), threading.Event())

    def test_enumeration_deduplicates_hid_usages_and_closes_handle(self):
        hid = Mock()
        hid.enumerate.return_value = [{"path": b"/dev/hidraw0"}] * 3
        with patch.object(battery, "read_device", return_value=True):
            self.assertEqual(battery.run(hid, Path("unused"),
                                         threading.Event(), once=True), 0)
        hid.enumerate.assert_called_once_with(0x3537, 0x10C6)
        hid.device.assert_called_once()
        hid.device.return_value.close.assert_called_once()

    def test_permission_failure_closes_handle_and_returns_failure(self):
        hid = Mock()
        hid.enumerate.return_value = [{"path": b"/dev/hidraw0"}]
        hid.device.return_value.open_path.side_effect = OSError("Permission denied")
        with self.assertLogs(level="WARNING"):
            self.assertEqual(battery.run(hid, Path("unused"),
                                         threading.Event(), once=True), 1)
        hid.device.return_value.close.assert_called_once()

    def test_disconnect_publishes_status_and_retries_after_hotplug(self):
        hid = Mock()
        hid.enumerate.side_effect = [[], [{"path": b"/dev/hidraw0"}]]
        stop = Mock()
        stop.is_set.side_effect = [False, False, True]
        with patch.object(battery, "read_device", side_effect=OSError("unplugged")), \
                patch.object(battery, "publish") as publish, \
                self.assertLogs(level="WARNING"):
            self.assertEqual(battery.run(hid, Path("unused"), stop), 0)
        self.assertEqual(hid.enumerate.call_count, 2)
        hid.device.return_value.close.assert_called_once()
        self.assertEqual(publish.call_args.args[1], "Disconnected")
        self.assertEqual(stop.wait.call_count, 2)


if __name__ == "__main__":
    unittest.main()
