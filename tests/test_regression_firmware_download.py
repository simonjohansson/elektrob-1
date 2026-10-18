"""Official firmware discovery and archive handling, without network access."""
import io
import sys
import unittest
from pathlib import Path
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fetch_firmware import firmware_payload, latest_download


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

    def test_only_the_expected_payload_is_read(self):
        def archive(files):
            data = io.BytesIO()
            with ZipFile(data, 'w') as zipped:
                for name in files:
                    zipped.writestr(name, b'payload')
            return data.getvalue()

        self.assertEqual(firmware_payload(archive(['release/Digitakt_OS1.54.syx',
                                                  '__MACOSX/Digitakt_OS1.54.syx', 'readme.txt']), '1.54'), b'payload')
        for files in (['Digitakt_OS1.53.syx'], ['a/Digitakt_OS1.54.syx', 'b/Digitakt_OS1.54.syx']):
            with self.assertRaisesRegex(ValueError, 'exactly one'):
                firmware_payload(archive(files), '1.54')


if __name__ == '__main__':
    unittest.main()
