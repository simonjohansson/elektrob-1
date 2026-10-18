"""Official firmware discovery and archive handling, without network access."""
import io
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fetch_firmware import firmware_payload, latest_download, main


class FirmwareDownloadTests(unittest.TestCase):
    def test_latest_mk1_link_and_supported_version(self):
        html = '''<a href="../wp-content/uploads/Digitakt_OS1.53_dist.zip">old</a>
          <a href="https://example.com/Digitakt_OS9.99_dist.zip">untrusted</a>
          <a href="../wp-content/uploads/Digitakt_II_OS9.99_dist.zip">other device</a>
          <a href="../wp-content/uploads/Digitakt_OS1.54_dist.zip">current</a>'''
        self.assertEqual(latest_download(html),
                         ('https://www.elektron.se/wp-content/uploads/Digitakt_OS1.54_dist.zip', '1.54'))
        with self.assertRaisesRegex(ValueError, 'No official'):
            latest_download('<a href="https://example.com/Digitakt_OS1.54_dist.zip">fake</a>')
        with self.assertRaisesRegex(ValueError, 'Latest Digitakt OS is 1.55'):
            latest_download(html + '<a href="/Digitakt_OS1.55_dist.zip">new</a>')

    def test_loaded_firmware_must_match_device_and_release(self):
        stock = SimpleNamespace(raw=b'known stock')
        for device, version in [('digitakt-mk1', '1.54'), ('digitakt-mk1', '1.53'),
                                ('digitone-mk1', '1.54')]:
            with self.subTest(device=device, version=version), patch(
                    'fetch_firmware.formats.load', return_value=(
                        stock, SimpleNamespace(key=device, name=device), SimpleNamespace(version=version))) as load:
                if device == 'digitakt-mk1' and version == '1.54':
                    self.assertEqual(firmware_payload(b'archive', '1.54'), stock.raw)
                else:
                    with self.assertRaisesRegex(ValueError, 'Expected Digitakt mk1 OS 1.54'):
                        firmware_payload(b'archive', '1.54')
                load.assert_called_once_with(b'archive')

    def test_unknown_content_is_rejected_even_with_expected_filename(self):
        data = io.BytesIO()
        with ZipFile(data, 'w') as zipped:
            zipped.writestr('Digitakt_OS1.54.syx', b'unknown firmware')
        with self.assertRaisesRegex(ValueError, 'no stock firmware elekloader knows'):
            firmware_payload(data.getvalue(), '1.54')
        with self.assertRaisesRegex(ValueError, 'zip cannot be read'):
            firmware_payload(b'PK\x03\x04broken zip', '1.54')

    def test_invalid_download_does_not_overwrite_firmware(self):
        html = b'<a href="/Digitakt_OS1.54_dist.zip">current</a>'
        with patch('fetch_firmware.fetch', side_effect=[html, b'unknown firmware']), \
                patch('fetch_firmware.Path.write_bytes') as write:
            with self.assertRaises(ValueError):
                main()
            write.assert_not_called()


if __name__ == '__main__':
    unittest.main()
