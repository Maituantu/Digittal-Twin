#!/usr/bin/env python3
"""Build source-only handover archives, inner manifest and outer checksums."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import stat
import tarfile
import tomllib
import zipfile

TOP_FILES = ('.gitignore', 'AGENTS.md', 'README.md', 'HANDOVER.md',
             'pyproject.toml', 'requirements.lock.txt')
CONFIG_FILES = ('pi.example.yaml', 'pi-15fps.example.yaml', 'pi-30fps.example.yaml')
TREES = {'src': {'.py'}, 'scripts': {'.py', '.sh'}, 'tests': {'.py'},
         'docs': {'.md'}, 'deploy': {'.service'}}
EXCLUDED = {'.git', '.venv', '__pycache__', '.pytest_cache', '.ruff_cache',
            'artifacts', 'build', 'dist'}


def collect_files(root):
    paths = [root / name for name in TOP_FILES]
    paths += [root / 'config' / name for name in CONFIG_FILES]
    for folder, suffixes in TREES.items():
        for path in (root / folder).rglob('*'):
            relative = path.relative_to(root)
            if any(part in EXCLUDED or part.endswith('.egg-info') for part in relative.parts):
                continue
            if path.is_file() and path.suffix in suffixes:
                paths.append(path)
    result = {}
    for path in sorted(set(paths)):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError(f'Expected a regular source file: {relative}')
        mode = 0o755 if path.suffix in {'.py', '.sh'} and path.stat().st_mode & 0o111 else 0o644
        result[relative] = (path.read_bytes(), mode)
    if 'docs/handover.md' not in result or 'docs/integration-contract.md' not in result:
        raise ValueError('Handover documentation is required')
    return result


def build(root, output, release):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', release):
        raise ValueError('Release name may contain letters, digits, dots, underscores and hyphens only')
    prefix = f'digital_twin_cam-{release}'
    output.mkdir(parents=True, exist_ok=True)
    targets = [output / (prefix + suffix) for suffix in ('.tar.gz', '.zip')]
    checksums = output / f'{prefix}.SHA256SUMS'
    if any(path.exists() for path in targets + [checksums]):
        raise ValueError('Release files already exist; choose another release label or move old files first')
    files = collect_files(root)
    now = datetime.now(timezone.utc)
    project = tomllib.loads((root / 'pyproject.toml').read_text())['project']
    metadata = {'release': release, 'package_version': project['version'],
                'created_at_utc': now.isoformat(), 'status': 'demo',
                'default_handover_profile': 'config/pi-15fps.example.yaml',
                'experimental_profile': 'config/pi-30fps.example.yaml',
                'hardware_target': 'Pi 3B / Linux V4L2 M2M H264; verify on receiving device',
                'python_requires': project['requires-python'],
                'media_topology': 'publisher Pi -> browser; server auth/signaling only',
                'source_file_count': len(files)}
    files['RELEASE.json'] = ((json.dumps(metadata, indent=2) + '\n').encode(), 0o644)
    manifest = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                       for name, (data, _) in sorted(files.items()))
    files['MANIFEST.sha256'] = (manifest.encode(), 0o644)
    with tarfile.open(targets[0], 'w:gz') as archive:
        for name, (data, mode) in sorted(files.items()):
            info = tarfile.TarInfo(f'{prefix}/{name}')
            info.size, info.mode, info.mtime = len(data), mode, int(now.timestamp())
            archive.addfile(info, io.BytesIO(data))
    with zipfile.ZipFile(targets[1], 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, (data, mode) in sorted(files.items()):
            info = zipfile.ZipInfo(f'{prefix}/{name}', now.timetuple()[:6])
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    checksums.write_text(''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
                                 for path in targets))
    return targets + [checksums]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', default='0.1.0-demo')
    parser.add_argument('--output-dir', default='dist')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        for path in build(root, Path(args.output_dir).resolve(), args.release):
            print(path)
    except (OSError, ValueError) as exc:
        parser.exit(1, f'{type(exc).__name__}: {exc}\n')


if __name__ == '__main__':
    main()
