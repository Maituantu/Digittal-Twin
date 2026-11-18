"""Handover safety: private files stay out; units follow the receiving Pi."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from uuid import uuid4
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def script(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'scripts' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseTests(unittest.TestCase):
    def test_archives_exclude_private_files_and_verify_every_hash(self):
        builder = script('package')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'source'
            shutil.copytree(ROOT, root, ignore=shutil.ignore_patterns(
                '.git', '.venv', '__pycache__', '*.egg-info', 'dist', 'artifacts'))
            secret = ('fixture-' + uuid4().hex).encode()
            forbidden = ['config/local.yaml', '.env', 'docs/private.key',
                         'artifacts/camera.h264', '.venv/bin/python',
                         'src/digital_twin_cam/__pycache__/leak.py']
            for name in forbidden:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(secret)
            outputs = builder.build(root, Path(directory) / 'output', 'test-release')
            prefix = 'digital_twin_cam-test-release/'
            with tarfile.open(outputs[0]) as archive:
                files = {}
                for member in archive.getmembers():
                    self.assertTrue(member.isfile())
                    self.assertTrue(member.name.startswith(prefix))
                    name = member.name.removeprefix(prefix)
                    self.assertNotIn('..', Path(name).parts)
                    files[name] = archive.extractfile(member).read()
            self.assertTrue(all(name not in files for name in forbidden))
            self.assertTrue(all(secret not in data for data in files.values()))
            manifest = files['MANIFEST.sha256'].decode().splitlines()
            self.assertEqual(len(manifest), len(files) - 1)
            for line in manifest:
                digest, name = line.split('  ', 1)
                self.assertEqual(hashlib.sha256(files[name]).hexdigest(), digest)
            self.assertEqual(json.loads(files['RELEASE.json'])['status'], 'demo')
            with zipfile.ZipFile(outputs[1]) as archive:
                self.assertEqual(set(archive.namelist()), {prefix + name for name in files})
                for name, data in files.items():
                    self.assertEqual(archive.read(prefix + name), data)
            for line in outputs[2].read_text().splitlines():
                digest, name = line.split('  ', 1)
                self.assertEqual(hashlib.sha256((outputs[2].parent / name).read_bytes()).hexdigest(), digest)
            with self.assertRaises(ValueError):
                builder.build(root, outputs[0].parent, 'test-release')

    def test_source_symlink_is_rejected(self):
        builder = script('package')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'source'
            shutil.copytree(ROOT, root, ignore=shutil.ignore_patterns(
                '.git', '.venv', '__pycache__', '*.egg-info', 'dist', 'artifacts'))
            external = Path(directory) / 'external.py'
            external.write_text('private fixture')
            (root / 'src/digital_twin_cam/linked.py').symlink_to(external)
            with self.assertRaises(ValueError):
                builder.collect_files(root)

    def test_service_uses_receiving_user_path_and_escaped_specifiers(self):
        renderer = script('render-service')
        user = pwd.getpwuid(os.getuid()).pw_name
        if user == 'root':
            user = 'nobody'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'Pi Cam%test'
            (root / '.venv/bin').mkdir(parents=True)
            (root / 'deploy/systemd').mkdir(parents=True)
            (root / '.venv/bin/dt-cam').write_text('#!/bin/sh\nexit 0\n')
            (root / '.venv/bin/dt-cam').chmod(0o755)
            shutil.copyfile(ROOT / 'deploy/systemd/digital-twin-cam.service',
                            root / 'deploy/systemd/digital-twin-cam.service')
            config = root / 'my config.yaml'
            config.write_text('fixture')
            text = renderer.render(root, user, config)
            self.assertIn(f'User={user}\n', text)
            self.assertNotIn('/home/thang/', text)
            self.assertIn('Pi Cam%%test', text)
            self.assertIn(f'WorkingDirectory={str(root).replace("%", "%%")}', text)
            self.assertIn('After=network-online.target zerotier-one.service', text)
            if shutil.which('systemd-analyze'):
                unit = root / 'digital-twin-cam.service'
                unit.write_text(text)
                # Provide the dependency to make this parser check independent
                # of ZeroTier being installed on a CI/test host.
                (root / 'zerotier-one.service').write_text('[Service]\nType=oneshot\nExecStart=/bin/true\n')
                checked = subprocess.run(['systemd-analyze', 'verify', str(unit)],
                                         capture_output=True, text=True, timeout=15)
                self.assertEqual(checked.returncode, 0, checked.stderr)
            with self.assertRaises(ValueError):
                renderer.render(root, 'root', config)
