"""Download the current supported Digitakt mk1 OS from Elektron."""
import re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

from elekloader import formats


PAGE = 'https://www.elektron.se/support-downloads/digitakt'
SUPPORTED_OS = '1.54'


class Downloads(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = set()

    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            href = dict(attrs).get('href')
            if href:
                self.links.add(urljoin(PAGE, href))


def latest_download(html):
    page = Downloads()
    page.feed(html)
    candidates = []
    for url in page.links:
        parsed = urlparse(url)
        match = re.fullmatch(r'Digitakt_OS(\d+)\.(\d+)([A-Za-z]?)_dist\.zip', Path(parsed.path).name)
        if match and parsed.scheme == 'https' and parsed.hostname in ('elektron.se', 'www.elektron.se'):
            major, minor, suffix = match.groups()
            candidates.append(((int(major), int(minor), suffix.upper()), url,
                               f'{major}.{minor}{suffix}'))
    if not candidates:
        raise ValueError('No official Digitakt mk1 OS download found')
    _, url, version = max(candidates)
    if version != SUPPORTED_OS:
        raise ValueError(f'Latest Digitakt OS is {version}; SDK and regression support must be reviewed before upgrading from {SUPPORTED_OS}')
    return url, version


def fetch(url):
    request = Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urlopen(request, timeout=60) as response:
        return response.read()


def firmware_payload(archive, version):
    stock, device, release = formats.load(archive)
    if device.key != 'digitakt-mk1' or release.version != version:
        raise ValueError(f'Expected Digitakt mk1 OS {version}; found {device.name} OS {release.version}')
    return stock.raw


def main():
    url, version = latest_download(fetch(PAGE).decode('utf-8'))
    payload = firmware_payload(fetch(url), version)
    output = Path(__file__).resolve().parents[1] / 'firmware' / f'Digitakt_OS{version}.syx'
    output.parent.mkdir(exist_ok=True)
    output.write_bytes(payload)
    print(f'Downloaded Digitakt OS {version} from {url} to {output}')


if __name__ == '__main__':
    main()
